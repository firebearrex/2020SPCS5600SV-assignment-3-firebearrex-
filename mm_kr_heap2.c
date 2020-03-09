/*
 * mm_kr_heap2.c
 */


#include <stdio.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include "memlib.h"
#include "mm_heap.h"


/** Allocation unit for forward header of memory blocks */
typedef union FwdHeader {
    struct {
        union FwdHeader *nextPtr;  /** next block if on free list */
        size_t size;        /** size of this block including header */
        /** measured in multiple of header size */
    } s;
    max_align_t _align;     /** force alignment to max align boundary */
} FwdHeader;
//
///** Allocation unit for backward header of memory blocks */
//typedef union BwdHeader {
//    struct {
//        union BwdHeader *prevPtr;  /** previous block if on free list*/
//        size_t size;        /** size of this block including header */
//        /** measured in multiple of header size */
//    } s;
//    max_align_t _align;     /** force alignment to max align boundary */
//} BwdHeader;

// forward declarations
static FwdHeader *morecore(size_t);
void visualize(const char*);

/** Empty list to get started */
static FwdHeader base1;
static BwdHeader base2;

/** Start of free memory list */
static FwdHeader *freep = NULL;

/**
 * Initialize memory allocator
 */
void mm_init() {
    mem_init();
    base1.s.nextPtr = freep = &base1;
    base1.s.size = 0;
    base2.s.prevPtr = &base2;
    base2.s.size = base1.s.size;
}

/**
 * Reset memory allocator.
 */
void mm_reset(void) {
    mem_reset_brk();
    base1.s.nextPtr = freep = &base1;
    base1.s.size = 0;
    base2.s.prevPtr = &base2;
    base2.s.size = base1.s.size;
}

/**
 * De-initialize memory allocator.
 */
void mm_deinit(void) {
    mem_deinit();
    base1.s.nextPtr = freep = &base1;
    base1.s.size = 0;
    base2.s.prevPtr = &base2;
    base2.s.size = base1.s.size;
}

/**
 * Allocation units for nbytes bytes.
 *
 * @param nbytes number of bytes
 * @return number of units for nbytes
 */
inline static size_t mm_units(size_t nbytes) {
    /* smallest count of Header-sized memory chunks */
    /* (+2 additional chunk for the two Headers) needed to hold nbytes */
    return (nbytes + sizeof(FwdHeader) - 1) / sizeof(FwdHeader) + 2;
}

/**
 * Allocation bytes for nunits allocation units.
 *
 * @param nunits number of units
 * @return number of bytes for nunits
 */
inline static size_t mm_bytes(size_t nunits) {
    return nunits * sizeof(FwdHeader);
}

/**
 * Get pointer to block payload.
 *
 * @param bp the block
 * @return pointer to allocated payload
 */
inline static void *mm_payload(FwdHeader *bp) {
    return bp + 1;
}

/**
 * Get pointer to block for payload.
 *
 * @param ap the allocated payload pointer
 */
inline static FwdHeader *mm_block(void *ap) {
    return (FwdHeader*)ap - 1;
}



/**
 * Allocates size bytes of memory and returns a pointer to the
 * allocated memory, or NULL if request storage cannot be allocated.
 *
 * @param nbytes the number of bytes to allocate
 * @return pointer to allocated memory or NULL if not available.
 */
void *mm_malloc(size_t nbytes) {
    if (freep == NULL) {
        mm_init();
    }

    FwdHeader *prevp = freep;

    // smallest count of Header-sized memory chunks
    //  (+2 additional chunk for the two Headers) needed to hold nbytes
    size_t nunits = mm_units(nbytes);

    // traverse the circular list to find a block
    for (FwdHeader *p = prevp->s.nextPtr; true ; prevp = p, p = p->s.nextPtr) {
        if (p->s.size >= nunits) {          /* found block large enough */
            if (p->s.size == nunits) {

                // free block exact size
                prevp->s.nextPtr = p->s.nextPtr;
            } else {
                // split and allocate tail end
                p->s.size -= nunits; // adjust the size to split the block
                /* find the address to return */
                p += p->s.size;		 // address upper block to return
                p->s.size = nunits;	 // set size of block
            }
            p->s.nextPtr = NULL;  // no longer on free list
            freep = prevp;  /* move the head */
            return mm_payload(p);
        }

        /* back where we started and nothing found - we need to allocate */
        if (p == freep) {                    /* wrapped around free list */
            p = morecore(nunits);
            if (p == NULL) {
                errno = ENOMEM;
                return NULL;                /* none left */
            }
        }
    }

    assert(false);  // shouldn't happen
    return NULL;
}


/**
 * Deallocates the memory allocation pointed to by ap.
 * If ap is a NULL pointer, no operation is performed.
 *
 * @param ap the memory to free
 */
void mm_free(void *ap) {
    // ignore null pointer
    if (ap == NULL) {
        return;
    }

    FwdHeader *bp = mm_block(ap);   /* point to block header */

    // validate size field of header block
    assert(bp->s.size > 0 && mm_bytes(bp->s.size) <= mem_heapsize());

    // find where to insert the free space
    // (bp > p && bp < p->s.nextPtr) => between two nodes
    // (p > p->s.nextPtr)            => this is the end of the list
    // (p == p->p.nextPtr)           => list is one element only
    FwdHeader *p = freep;
    for ( ; !(bp > p && bp < p->s.nextPtr); p = p->s.nextPtr) {
        if (p >= p->s.nextPtr && (bp > p || bp < p->s.nextPtr)) {
            // freed block at start or end of arena
            break;
        }
    }

    if (bp + bp->s.size == p->s.nextPtr) {
        // coalesce if adjacent to upper neighbor
        bp->s.size += p->s.nextPtr->s.size;
        bp->s.nextPtr = p->s.nextPtr->s.nextPtr;
    } else {
        // link in before upper block
        bp->s.nextPtr = p->s.nextPtr;
    }

    if (p + p->s.size == bp) {
        // coalesce if adjacent to lower block
        p->s.size += bp->s.size;
        p->s.nextPtr = bp->s.nextPtr;

    } else {
        // link in after lower block
        p->s.nextPtr = bp;
    }

    /* reset the start of the free list */
    freep = p;
}

/**
 * Tries to change the size of the allocation pointed to by ap
 * to size, and returns ap.
 *
 * If there is not enough room to enlarge the memory allocation
 * pointed to by ap, realloc() creates a new allocation, copies
 * as much of the old data pointed to by nextPtr as will fit to the
 * new allocation, frees the old allocation, and returns a pointer
 * to the allocated memory.
 *
 * If ap is NULL, realloc() is identical to a call to malloc()
 * for size bytes.  If size is zero and nextPtr is not NULL, a minimum
 * sized object is allocated and the original object is freed.
 *
 * @param ap pointer to allocated memory
 * @param newsize required new memory size in bytes
 * @return pointer to allocated memory at least required size
 *	with original content
 */
void* mm_realloc(void *ap, size_t newsize) {
    // NULL ap acts as malloc for size newsize bytes
    if (ap == NULL) {
        return mm_malloc(newsize);
    }

    FwdHeader* bp = mm_block(ap);    // point to block header
    if (newsize > 0) {
        // return this ap if allocated block large enough
        if (bp->s.size >= mm_units(newsize)) {
            return ap;
        }
    }

    // allocate new block
    void *newap = mm_malloc(newsize);
    if (newap == NULL) {
        return NULL;
    }
    // copy old block to new block
    size_t oldsize = mm_bytes(bp->s.size-1);
    memcpy(newap, ap, (oldsize < newsize) ? oldsize : newsize);
    mm_free(ap);
    return newap;
}


/**
 * Request additional memory to be added to this process.
 *
 * @param nu the number of Header units to be added
 * @return pointer to start additional memory added
 */
static FwdHeader *morecore(size_t nu) {
    // nalloc based on page size
    size_t nalloc = mem_pagesize()/sizeof(FwdHeader);

    /* get at least NALLOC Header-chunks from the OS */
    if (nu < nalloc) {
        nu = nalloc;
    }

    size_t nbytes = mm_bytes(nu); // number of bytes
    void* p = mem_sbrk(nbytes);
    if (p == (char *) -1) {	// no space
        return NULL;
    }

    FwdHeader* bp = (FwdHeader*)p;
    bp->s.size = nu;

    // add new space to the circular list
    mm_free(bp+1);

    return freep;
}

/**
 * Print the free list (debugging only)
 *
 * @msg the initial message to print
 */
void visualize(const char* msg) {
    fprintf(stderr, "\n--- Free list after \"%s\":\n", msg);

    if (freep == NULL) {                   /* does not exist */
        fprintf(stderr, "    List does not exist\n\n");
        return;
    }

    if (freep == freep->s.nextPtr) {          /* self-pointing list = empty */
        fprintf(stderr, "    List is empty\n\n");
        return;
    }

/*
    tmp = freep;                           // find the start of the list
    char* str = "    ";
    do {           // traverse the list
        fprintf(stderr, "%snextPtr: %10p size: %-3lu blks - %-5lu bytes\n",
        	str, (void *)tmp, tmp->s.size, mm_bytes(tmp->s.size));
        str = " -> ";
        tmp = tmp->s.nextPtr;
    }  while (tmp->s.nextPtr > freep);
*/
    char* str = "    ";
    for (FwdHeader *p = base.s.nextPtr; p != &base; p = p->s.nextPtr) {
        fprintf(stderr, "%snextPtr: %10p size: %3lu blks - %5lu bytes\n",
                str, (void *)p, p->s.size, mm_bytes(p->s.size));
        str = " -> ";
    }


    fprintf(stderr, "--- end\n\n");
}


/**
 * Calculate the total amount of available free memory.
 *
 * @return the amount of free memory in bytes
 */
size_t mm_getfree(void) {
    if (freep == NULL) {
        return 0;
    }

    // point to head of free list
    FwdHeader *tmp = freep;
    size_t res = tmp->s.size;

    // scan free list and count available memory
    while (tmp->s.nextPtr > tmp) {
        tmp = tmp->s.nextPtr;
        res += tmp->s.size;
    }

    // convert header units to bytes
    return mm_bytes(res);
}
