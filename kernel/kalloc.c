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

// 每个空闲页的链表元素是一个结构体run
struct run {
  struct run *next;
};

struct {
  struct spinlock lock;     // 自旋锁   (空闲页链表和锁被包裹在一个结构体中，以明确锁保护的是结构体中的字段)
  struct run *freelist;     // 空闲页链表
} kmem;

// main函数调用kinit来 初始化物理页分配器
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);   // 初始空闲页链表，以保存内核地址结束到PHYSTOP之间的每一页 (假设内存为128而不是检验硬件)
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);   // 确保它只添加 对齐的(一页/4096bytes/4KB) 页物理地址到空闲链表中
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// 操作单位：A page  合法操作范围：end of kernel text&data ~ PHYSTOPs (`Free` memory part)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // 添加内存到空闲页链表
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// kcalloc分配的页是从物理地址：内核地址结束到PHYSTOP之间在freelist上的空间(Page)
// 一次分配一个页 (4KB)
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
