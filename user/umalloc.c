#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

// Memory allocator by Kernighan and Ritchie,
// The C programming Language, 2nd ed.  Section 8.7.

// 当有申请请求时，malloc 将扫描空闲块链表，直到找到一个足够大的块为止。该算法称
// 为“首次适应”（first fit）；与之相对的算法是“最佳适应”（best fit），它寻找满足条件的最小
// 块。如果该块恰好与请求的大小相符合，则将它从链表中移走并返回给用户。如果该块太大，
// 则将它分成两部分：大小合适的块返回给用户，剩下的部分留在空闲块链表中。如果找不到
// 一个足够大的块，则向操作系统申请一个大块并加入到空闲块链表中

// Block organization: https://s2.loli.net/2022/03/26/EnrZyUxTGpdm1gj.png
// Header: https://s2.loli.net/2022/03/26/ZWnwgh5ltoafRcq.png

// 空闲块包含一个指向链表中下一个块的指针、一个块大小的记录和
// 一个指向空闲空间本身的指针(free data start address)
typedef long Align;   /* for alignment to long boundary */

union header {        /* block header (free or used) */
  struct {
    union header *ptr;  /* next block if on free list */
    uint size;          /* size of this block */
  } s;
  Align x;              /* force alignment of blocks (never used) */
};

typedef union header Header;

static Header base;   /* 空闲块链表的头部 */
static Header *freep; /* 上一次找到空闲块的地方 */

void
free(void *ap)
{
  Header *bp, *p;

  bp = (Header*)ap - 1;
  for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
    if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break;
  if(bp + bp->s.size == p->s.ptr){
    bp->s.size += p->s.ptr->s.size;
    bp->s.ptr = p->s.ptr->s.ptr;
  } else
    bp->s.ptr = p->s.ptr;
  if(p + p->s.size == bp){
    p->s.size += bp->s.size;
    p->s.ptr = bp->s.ptr;
  } else
    p->s.ptr = bp;
  freep = p;
}

static Header*
morecore(uint nu)
{
  char *p;
  Header *hp;

  if(nu < 4096)
    nu = 4096;
  p = sbrk(nu * sizeof(Header));
  if(p == (char*)-1)
    return 0;
  hp = (Header*)p;
  hp->s.size = nu;
  free((void*)(hp + 1));
  return freep;
}

void*
malloc(uint nbytes)
{
  Header *p, *prevp;
  uint nunits;

  nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;
  if((prevp = freep) == 0){     /* no free list yet */
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  /* 找符合条件的空闲块 */
  for(p = prevp->s.ptr; ; prevp = p, p = p->s.ptr){
    if(p->s.size >= nunits){    /* 找到足够大的空闲块 */
      if(p->s.size == nunits)   /* 大小正好 */
        prevp->s.ptr = p->s.ptr;
      else {                    /* If malloc() will use a part of a free segement, this is split into two parts. */
                                /* The first remains free, the second will be the one returned */
        p->s.size -= nunits;
        p += p->s.size;
        p->s.size = nunits;
      }
      freep = prevp;
      return (void*)(p + 1);    /* 1是头部的大小 */
    }
    if(p == freep)
      if((p = morecore(nunits)) == 0)
        return 0;
  }
}
