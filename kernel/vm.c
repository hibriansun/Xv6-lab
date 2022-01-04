#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

/*
 * create a direct-map page table for the kernel.   /// 为内核虚拟地址空间建立与物理地址空间'直接映射'的页表
 */
void
kvminit()
{
  kernel_pagetable = (pagetable_t) kalloc();
  memset(kernel_pagetable, 0, PGSIZE);

  // uart registers
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));    /// 设置根页表寄存器为内核页表起始地址
  sfence_vma();     /// flush the TLB.
}

// Return 'the address of the PTE' in page table pagetable
// that **corresponds to** virtual address 'va'.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
// 功能：返回包含虚拟地址对应物理地址所在物理页起始地址信息的PTE的地址
// 若alloc为1，那么当我们试图找一个不存在映射关系的虚拟地址时：
// 在根页表找不到对应合法PTE，那么就Alloc一页作为下级页表页，并建立本级PTE对新建页的PTE关联
// 下级页表页上操作类似，也是Alloc一页作为下级页表页，并建立本级PTE对新建页的PTE关联
// 最终返回最后一级页表页上包含虚拟地址对应物理地址所在物理页信息的PTE地址，而不会对不存在目标物理页生成(注意walk中for终止条件为>0不是>=0)
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];     // 获取虚拟地址的某'层'的九位PTE索引 在这层页表的页表页中定位到PTE的地址
                                                // PTE作为一个64位变量在页表页中自身有地址同时存着物理页地址
    if(*pte & PTE_V) {    // PTE存的地址是指向下级页表的某页
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {              // PTE指向不存在页，即va定位的下级页表不存在
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)     // pagetable被更新为下级新生成的物理页地址
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;       // 为下级页表新建一页，将该页起始物理地址变化填充到本级PTE中
    }
  }
  return &pagetable[PX(0, va)];               // 返回va对应的 指向其物理页起始物理地址 的PTE的地址
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
// 根据虚拟地址返回 物理地址所在 物理页 的起始地址
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0 || (*pte & PTE_V) == 0)  // Invaild address OR 合法虚拟地址空间地址但没被映射由于lazy allocation
  {
    if ((va >= myproc()->sz || va <= PGROUNDDOWN(myproc()->trapframe->sp))) {  // Invaild address
      return 0;
    }
    return -1;
  }

  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
// 对于内核页表，将一个虚拟地址范围映射到一个物理地址范围
void
kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
// 内核态虚拟地址 --> 物理地址
uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  pte = walk(kernel_pagetable, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

// `Create PTEs` for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
// `创建页表项`将虚拟地址映射到物理地址
// @Param: 在页表pagetable中，创建页表项(PTE(s))将虚拟地址va映射到物理地址pa上，连续占用size大小内存空间，页表项标志位为perm

// 将范围内地址分割成多页（忽略余数），每次映射一页的顶端地址。对于每个要映射的虚拟地址（页的顶端地址）
// mapages调用walk找到该地址的 最后一页表层级的PTE的指针。然后，再配置PTE，使其持有相关的物理页号、所需的权限(PTE_W、PTE_X和/或PTE_R)，
// 以及PTE_V来标记PTE为有效
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)    // 得到虚拟地址a对应所在页中的PTE地址
      return -1;
    if(*pte & PTE_V)
      panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)     // 从起始地址到分配页(Page(s))结束其中页个数(Page(s))可能需要多个PTE(s)表示 即size可能大于2^9bytes = 4KB
      break;
    a += PGSIZE;      // 一页一页地分配地址空间，一页一页地建立地址映射
    pa += PGSIZE;
  }
  return 0;
}

// ==== 以 uvm 开头的函数操作用户页表 ====

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
// 取消虚拟地址va开始n个页的映射，可选是否free物理内存
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      continue;
      // panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      continue;
    //   panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
// 新建一个用户虚拟地址空间的页表，大小为一页(即根级页表大小)
// fork/新exec
// allocproc()/exec() -> proc_pagetable() -> uvmcreate()
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need `not` be page aligned.  Returns new size or 0 on error.
// 实现扩容 -- 用户虚拟地址空间动态内存分配，heap增长是从低地址向高地址增长
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate(解除分配) user pages to bring the `process size from oldsz to
// newsz`.  oldsz and newsz need `not` be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
// 实现缩容
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
// 遍历三级页表每级(一级页表至少一页，一页512个PTE)所有PTE，只要有PTE可以往下延伸，就继续递归
// 遍历完某页后再free掉页表
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // printf("%p\n", PTE2PA(pte));
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
// free掉从用户虚拟地址0开始后sz大小空间及映射页表
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);    // 取消映射从用户虚拟地址0开始后sz大小空间
  freewalk(pagetable);    // 删除整个页表
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// """""Copies both the page table and the"""""
// """""physical memory."""""
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
// fork() -> uvmcopy()
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      continue;
      // panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      continue;
      // panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);

    if(pa0 == -1) {
      // 合法虚拟地址空间地址但没被映射由于lazy allocation
      char* mem = (char*)kalloc();
      if(mem == 0){
        panic("Lazy allocation failed: out of memory");
      }
      memset((void*)mem, 0, PGSIZE);
      // map
      if(mappages(pagetable, PGROUNDDOWN(va0), PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_U) != 0){
        kfree((void*)mem);
        uvmdealloc(pagetable, PGROUNDUP(va0), PGROUNDDOWN(va0));
        panic("Lazy allocation failed");
      }
      pa0 = PTE2PA(*walk(pagetable, va0, 0));
    }


    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    if(pa0 != -1)
      memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// 将内核数据复制到用户虚拟地址
// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}