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


/** Allocation unit for header of memory blocks */
typedef union Header {
    struct {
        union Header *ptr;  /** next block if on free list */
        size_t size;        /** size of this block including header */
        /** measured in multiple of header size */
    } s;
    max_align_t _align;     /** force alignment to max align boundary */
} Header/*, Footer*/;

// find the next free block from current Header
inline static Header *nextFree(Header *h) {
    return h->s.ptr;
}

// find the previous free block from current Header
inline static Header *prevFree(Header *h) {
    return Footer(h)->s.ptr;
}

// get Footer position of the current Header
inline static Header *Footer(Header *h) {
    return h+h->s.size-1;
}

// get Header position of the current Footer
//inline static Header *Header(Footer *f) {
inline static Header *Header(Header *f) {
    return f-f->s.size+1;
}

// equalizing the size in Footer
inline static void equalFooter(Header *h) {
    Footer(h)->s.size = h->s.size;
}

// find the upper block Header from current Header position
inline static Header *upperHeader(Header *h) {
    return h+h->s.size;
}

// find the upper block Footer from current Header position
inline static Header *upperFooter(Header *h) {
    return Footer(upperHeader(h));
}

// find the lower block Footer from current Header position
inline static Header *lowerFooter(Header *h) {
    return h-1;
}

// find the lower block Header from current Header position
inline static Header *lowerHeader(Header *h) {
    return Header(lowerFooter(h));
}

// forward declarations
static Header *morecore(size_t);
void visualize(const char*);

/** Empty list to get started */
static Header baseH;
//static Header base;
static Header baseF;

/** Start of free memory list */
static Header *freep = NULL;

/**
 * Initialize memory allocator
 */
void mm_init() {
    mem_init();

    baseH.s.ptr = freep = baseF.s.ptr = &baseH;
    baseH.s.size = baseF.s.size = 0;
}

/**
 * Reset memory allocator.
 */
void mm_reset(void) {
    mem_reset_brk();

//    base.s.ptr = freep = &base;
//    base.s.size = 0;
    baseH.s.ptr = freep = baseF.s.ptr = &baseH;
    baseH.s.size = baseF.s.size = 0;
}

/**
 * De-initialize memory allocator.
 */
void mm_deinit(void) {
    mem_deinit();

//    base.s.ptr = freep = &base;
//    base.s.size = 0;
    baseH.s.ptr = freep = baseF.s.ptr = &baseH;
    baseH.s.size = baseF.s.size = 0;
}

/**
 * Allocation units for nbytes bytes.
 *
 * @param nbytes number of bytes
 * @return number of units for nbytes
 */
inline static size_t mm_units(size_t nbytes) {
    /* smallest count of Header-sized memory chunks */
    /*  (+2 additional chunk for the Header and Footer) needed to hold nbytes */
    return (nbytes + sizeof(Header) - 1) / sizeof(Header) + 2;
}

/**
 * Allocation bytes for nunits allocation units.
 *
 * @param nunits number of units
 * @return number of bytes for nunits
 */
inline static size_t mm_bytes(size_t nunits) {
    return nunits * sizeof(Header);
}

/**
 * Get pointer to block payload.
 *
 * @param bp the block
 * @return pointer to allocated payload
 */
inline static void *mm_payload(Header *bp) {
    return bp + 1;
}

/**
 * Get pointer to block for payload.
 *
 * @param ap the allocated payload pointer
 */
inline static Header *mm_block(void *ap) {
    return (Header*)ap - 1;
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

    Header *prevp = freep;

    // smallest count of Header-sized memory chunks
    //  (+2 additional chunk for the Header and Footer) needed to hold nbytes
    size_t nunits = mm_units(nbytes);

    // traverse the circular list to find a block
    for (Header *p = prevp->s.ptr; true ; prevp = p, p = p->s.ptr) {
        if (p->s.size >= nunits) {          /* found block large enough */
            if (p->s.size == nunits) {
                // free block exact size
                prevp->s.ptr = p->s.ptr;
//                Footer(p->s.ptr)->s.ptr = Footer(p)->s.ptr;
                prevFree(nextFree(p)) = prevFree(p);
            } else {
                // split and allocate tail end
                Header *f = prevFree(p) /*Footer(p)->s.ptr*/; /* store the original Footer pointer */
                p->s.size -= nunits; // adjust the size (of the current free block after malloc operation) to (before)
                                        // split the block (in the next step)
                prevFree(p) /*Footer(p)->s.ptr*/ = f; /* make the Footer of the remaining free block point to wherever the original
                                         pointer pointed to */
                equalFooter(p); /* update the size in Footer of the remaining free block */
                /* find the address to return (by moving the *p pointer) */
                p += p->s.size;		 // address upper block to return
                p->s.size = nunits;	 // set size of block
                equalFooter(p); /* update the size in Footer of the allocated block */
            }
            nextFree(p) /*p->s.ptr*/ = NULL;  // no longer on free list

            prevFree(p) /*Footer(p)->s.ptr*/ = NULL; /* Footer pointer set to NULL */

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

    Header *bp = mm_block(ap);   /* point to block header */

    // validate size field of header block
    assert(bp->s.size > 0 && mm_bytes(bp->s.size) <= mem_heapsize());

    // find where to insert the free space
    // (bp > p && bp < p->s.ptr) => between two nodes
    // (p > p->s.ptr)            => this is the end of the list
    // (p == p->p.ptr)           => list is one element only
//    Header *p = freep;
//    for ( ; !(bp > p && bp < p->s.ptr); p = p->s.ptr) {
//        if (p >= p->s.ptr && (bp > p || bp < p->s.ptr)) {
//            // freed block at start or end of arena
//            break;
//        }
//    }
//
//    if (bp + bp->s.size == p->s.ptr) {
//        // coalesce if adjacent to upper neighbor
//        bp->s.size += p->s.ptr->s.size;
//        bp->s.ptr = p->s.ptr->s.ptr;
//    } else {
//        // link in before upper block
//        bp->s.ptr = p->s.ptr;
//    }
//
//    if (p + p->s.size == bp) {
//        // coalesce if adjacent to lower block
//        p->s.size += bp->s.size;
//        p->s.ptr = bp->s.ptr;
//
//    } else {
//        // link in after lower block
//        p->s.ptr = bp;
//    }
//
//    /* reset the start of the free list */
//    freep = p;

//
//  coalesce if the upper block is in the free list
//    if (nextFree(upperHeader(bp)) /*upperHeader(bp)->s.ptr*/ != NULL) {
//        nextFree(bp) = nextFree(upperHeader(bp)); /* let bp point to wherever the upper block points to */
//        prevFree(nextFree(upperHeader(bp))) = bp; /* let the next free block pointed by the upper block points to bp
//*                                                    as its previous free block */
//        bp->s.size += upperHeader(bp)->s.size; /* update the size of bp after coalescing */
//        equalFooter(bp); /* equalize its Footer size*/
//        bp->s.ptr = upperHeader(bp)->s.ptr;
//        Footer(upperHeader(bp)->s.ptr)->s.ptr = bp;
//    } else {
//    else if (lowerFooter(bp)->s.ptr != NULL)  {      /* coalesce if the lower block is in the free list*/
//        lowerHeader(bp)->s.size += bp->s.size;
//        equalFooter(lowerHeader(bp));
//
//        /* link in before upper free block */
//        bp->s.ptr = nextFree(freep) /*freep->s.ptr*/;
//        prevFree(nextFree(freep)) = bp;
//        Footer(bp->s.ptr)->s.ptr = bp;
//        Footer(bp)->s.ptr = freep;
//    }
//
//  coalesce if the lower block is in the free list
//    if (lowerFooter(bp)->s.ptr != NULL) {
//        prevFree(bp) = prevFree(lowerHeader(bp)); /* let the Footer of bp points to wherever the Footer of the lower
// *                                                   block points to */
//        nextFree(lowerHeader(bp)) = nextFree(bp); /* let the Header of the lower block points to wherever bp points to */
//        prevFree(nextFree(bp)) = lowerHeader(bp); /* let the Footer of the next free block after bp points to the
// *                                                   Header of the lower block */
//        lowerHeader(bp)->s.size += bp->s.size; /* update the size of the lower block after coalescing */
//        equalFooter(lowerHeader(bp)); /* equalize its Footer size */
//    }

// coalesce if both upper block and lower block are in the free list
    if ((nextFree(upperHeader(bp)) != NULL) && (nextFree(lowerHeader(bp)) != NULL)) {
        nextFree(lowerHeader(bp)) = nextFree(upperHeader(bp)); /* let the lower block Header points to wherever the
 *                                                                upper block Header points to */
        prevFree(upperHeader(bp)) = prevFree(lowerHeader(bp)); /* let the upper block Footer points to wherever the
 *                                                                lower block Footer points to */
        prevFree(nextFree(upperHeader(bp))) = lowerHeader(bp); /* let the Footer of next free block after the
 *                                                                upper block points to the Header of the lower
 *                                                                block */
        bp->s.siza += upperHeader(bp)->s.size; /* update the size after coalescing */
        lowerHeader(bp)->s.size += bp->s.size;
        equalFooter(lowerHeader(bp)); /* equalize the new Footer size */
    } else if ((nextFree(upperHeader(bp)) != NULL) && (nextFree(lowerHeader(bp)) == NULL) ) {
        nextFree(bp) = nextFree(upperHeader(bp)); /* let the Header of bp points to wherever the Header of the upper
 *                                                   block points to */
        prevFree(nextFree(upperHeader(bp))) = bp; /* let the Footer of the next free block after the upper block points
 *                                                   to bp */
        nextFree(prevFree(upperHeader(bp))) = bp; /* let the Header of the previous free block points to bp */
        bp->s.size += upperHeader(bp)->s.size; /* update the size after coalescing */
        equalFooter(bp); /* equalize the new Footer size */
    } else if ((nextFree(upperHeader(bp)) == NULL) && (nextFree(lowerHeader(bp)) != NULL) ) {
        prevFree(bp) = prevFree(lowerHeader(bp)); /* let the Footer of bp points to wherever the Footer of the lower
 *                                                   block points to */
        lowerHeader(bp)->s.size += bp->s.size; /* update the size after coalescing */
        equalFooter(lowerHeader(bp)); /* equalize the new Footer size */
    } else {
        Header *h = nextFree(freep);
        nextFree(freep) = bp;
        nextFree(bp) = h;
        prevFree(h) = bp;
        prevFree(bp) = freep;
    }
}

/**
 * Tries to change the size of the allocation pointed to by ap
 * to size, and returns ap.
 *
 * If there is not enough room to enlarge the memory allocation
 * pointed to by ap, realloc() creates a new allocation, copies
 * as much of the old data pointed to by ptr as will fit to the
 * new allocation, frees the old allocation, and returns a pointer
 * to the allocated memory.
 *
 * If ap is NULL, realloc() is identical to a call to malloc()
 * for size bytes.  If size is zero and ptr is not NULL, a minimum
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

    Header* bp = mm_block(ap);    // point to block header
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
static Header *morecore(size_t nu) {
    // nalloc based on page size
    size_t nalloc = mem_pagesize()/sizeof(Header);

    /* get at least NALLOC Header-chunks from the OS */
    if (nu < nalloc) {
        nu = nalloc;
    }

    size_t nbytes = mm_bytes(nu); // number of bytes
    void* p = mem_sbrk(nbytes);
    if (p == (char *) -1) {	// no space
        return NULL;
    }

    Header* bp = (Header*)p;
    bp->s.size = nu;

//    Footer(bp)->s.size = bp->s.size;

    equalFooter(bp); /* set Footer size equal to Header size */

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

    if (freep == freep->s.ptr) {          /* self-pointing list = empty */
        fprintf(stderr, "    List is empty\n\n");
        return;
    }

/*
    tmp = freep;                           // find the start of the list
    char* str = "    ";
    do {           // traverse the list
        fprintf(stderr, "%sptr: %10p size: %-3lu blks - %-5lu bytes\n",
        	str, (void *)tmp, tmp->s.size, mm_bytes(tmp->s.size));
        str = " -> ";
        tmp = tmp->s.ptr;
    }  while (tmp->s.ptr > freep);
*/
    char* str = "    ";
    for (Header *p = base.s.ptr; p != &base; p = p->s.ptr) {
        fprintf(stderr, "%sptr: %10p size: %3lu blks - %5lu bytes\n",
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
    Header *tmp = freep;
    size_t res = tmp->s.size;

    // scan free list and count available memory
    while (tmp->s.ptr > tmp) {
        tmp = tmp->s.ptr;
        res += tmp->s.size;
    }

    // convert header units to bytes
    return mm_bytes(res);
}
