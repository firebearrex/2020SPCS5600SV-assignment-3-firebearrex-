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
} Header;

inline static Header *Footer(Header *h) {
    return h+h->s.size+1;
}

// get Header position of the current Footer
//inline static Header *Header(Footer *f) {
inline static Header *getHeader(Header *f) {
    return f-f->s.size-1;
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

static void printFreeList(Header *p) {
    Header *current = p;

    do {
        printf("(%x, %d, %x)---->", current, current->s.size, current->s.ptr);
        current = current->s.ptr;
    } while (current != p);
    printf("\n");
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
    Header *nextFp;
    Header *prevFp;
    Header *fp;
    Header *newFp;

    // smallest count of Header-sized memory chunks
    //  (+2 additional chunk for the Header and Footer) needed to hold nbytes
    size_t nunits = mm_units(nbytes);
    //printf("Size of Header: %d\n", sizeof(Header));
    printf("\nMalloc: %d units\n", nunits);
    printFreeList(freep);

    // traverse the circular list to find a block
    for (Header *p = prevp->s.ptr; true ; prevp = p, p = p->s.ptr) {
        if (p->s.size >= nunits) {          /* found block large enough */
            fp = Footer(p);
            prevFp = Footer(prevp);
            nextFp = Footer(p->s.ptr);

            if (p->s.size == nunits) {
                // free block exact size
                printf("block size same as nunits at block: %x\n", p);

                prevp->s.ptr = p->s.ptr;
                nextFp->s.ptr = prevFp;

                //printf("End: block size same as nunits\n");
            } else {
                printf("block size greater than nunits\n");

                // split and allocate tail end
                // adjust the size to split the block
                // adjust additional header and footer created
                // after splitting the block
                p->s.size -= (nunits + 2);

                newFp = Footer(p);
                newFp->s.size = p->s.size;
                newFp->s.ptr = prevFp;
                nextFp->s.ptr = newFp;

                /* find the address to return */
                p += p->s.size + 2;		 // address upper block to return
                p->s.size = nunits;	 // set size of block
                fp->s.size = p->s.size;
                //printf("End: block size greater than nunits\n");
            }
            printf("Resetting next pointers\n");
            p->s.ptr = NULL;  // no longer on free list
            fp->s.ptr = NULL;
            //printf("End: Resetting next pointers\n");

            freep = prevp;  /* move the head */
            printf("Returning payload from block: %x\n", p);
            printFreeList(freep);
            return mm_payload(p);
        }

        /* back where we started and nothing found - we need to allocate */
        if (p == freep) {                    /* wrapped around free list */
            printf("Need more memory\n");
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
    Header *fp = Footer(bp);   /* point to block header */

    printf("\nFree: (%x, %d, %x)\n", bp, bp->s.size, fp);
    printFreeList(freep);

    // validate size field of header block
    assert(bp->s.size > 0 && mm_bytes(bp->s.size) <= mem_heapsize());

    Header *p = freep;
    Header *rp = Footer(freep);

    for( ; ; p = p->s.ptr, rp = rp->s.ptr) {
        if (p >= p->s.ptr && (bp > p || bp < p->s.ptr)) {
            // freed block at start or end of arena and p points to
            // header of last block in list
            printf("P: Insert freed block at start or end of arena\n");
            //printFreeList(p);
            rp = Footer(p->s.ptr);
            break;
        }

        if (rp <= rp->s.ptr && (fp < rp || fp > rp->s.ptr)) {
            // freed block at start or end of arena and rp points to
            // footer of first block in list
            printf("RP: Insert freed block at start or end of arena\n");
            //printFreeList(p);
            p = getHeader(rp->s.ptr);
            break;
        }

        if (bp > p && bp < p->s.ptr) {
            // found previous block to freed block and p points to
            // header of previous block to bp
            printf("P: found previous block to freed block\n");
            //printFreeList(p);
            rp = Footer(p->s.ptr);
            break;
        }

        if (fp < rp && fp > rp->s.ptr) {
            // found next block to freed block and rp points to
            // footer of next block to fp
            printf("RP: found previous block to freed block\n");
            //printFreeList(p);
            p = getHeader(rp->s.ptr);
            break;
        }
    }

    if ((bp + bp->s.size + 2) == p->s.ptr) {
        // coalesce if adjacent to upper neighbor
        printf("Coalesce if adjacent to upper neighbor\n");
        bp->s.size += p->s.ptr->s.size;
        bp->s.ptr = p->s.ptr->s.ptr;

        rp->s.size += fp->s.size;
        fp = rp;
        //printf("End: coalesce if adjacent to upper neighbor\n");
    } else {
        // link in before upper block
        printf("Link in before upper block\n");
        bp->s.ptr = p->s.ptr;

        rp->s.ptr = fp;
        //printf("End: link in before upper block\n");
    }

    if ((p + p->s.size + 2) == bp) {
        // coalesce if adjacent to lower block
        printf("Coalesce if adjacent to lower block\n");
        p->s.size += bp->s.size;
        p->s.ptr = bp->s.ptr;

        fp->s.size += rp->s.ptr->s.size;
        fp->s.ptr = rp->s.ptr->s.ptr;
        //printf("End: coalesce if adjacent to lower block\n");
    } else {
        // link in after lower block
        printf("Link in after lower block\n");
        p->s.ptr = bp;

        fp->s.ptr = rp;
        //printf("End: link in after lower block\n");
    }

    /* reset the start of the free list */
    freep = p;
    printFreeList(p);
    printf("Reset freep to p: %x\n", p);
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
    size_t oldsize = mm_bytes(bp->s.size-2);
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
    Header* fp = Footer(bp);
    bp->s.size = nu;
    fp->s.size = bp->s.size;
    bp->s.ptr = NULL;
    fp->s.ptr = NULL;

    //equalFooter(bp); /* set Footer size equal to Header size */

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
    for (Header *p = baseH.s.ptr; p != &baseH; p = p->s.ptr) {
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
