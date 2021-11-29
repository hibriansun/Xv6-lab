// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);   // 从kernel(也是个可执行文件)后第一个地址开始一直到Kernel能使用的最大空间
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)     // 参数是某个页的起始 内核虚拟地址(直接映射物理页地址)
{
  struct run *r;

  // 可以被kfree的物理页
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;      // 对内核虚拟地址(直接映射物理页地址)强转成kmem链表中struct run节点

  acquire(&kmem.lock);      // 在空闲物理页链表kmem头部插入这个节点
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);      // 加锁
  r = kmem.freelist;        // 从空闲物理页链表头部拿一个节点(页)出来
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);      // 解锁

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;          // 返回这个页的 "起始内核虚拟地址(等于物理地址)"
}
