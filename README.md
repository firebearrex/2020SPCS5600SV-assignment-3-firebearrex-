# assignment-3-sanjivani
assignment-3-sanjivani

Team members:

Name: Sanjivani Jethwaney
CCIS ID: sanjivani

Name: Cheng Zhao
CCIS ID: firebearrex

Comments about the assignments:
- In our solution, we have used union Header to store Footer info
  for each block. Footer maintains pointers to previous free block.
- Size of the block is stored in Header as well as Footer. Size of
  the block does not include Header and Footer. Only during allocating
  new block in morecore() function, we allocate additional memory for
  Header and Footer. Hence, pointers and size in Header and Footer
  of each block are set accordingly.

Current Problem with the Solution:
- mm_malloc() is able to allocate memory requested while maintaining
  free block list.
- However, after allocating memory for about 179 blocks, one of the
  free block in the free list points to incorrect memory address.
- Because of this, mm_alloc() fails to adjust necessary pointers.

Solution:
- Need to debug more.
- Probably, next pointers in Header and Footer are set incorrectly
  in one of the corner cases.
