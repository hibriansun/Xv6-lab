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
pagetable_t kernel_pagetable;     // 内核虚拟内存的映射表，其中许多部分虚拟地址与物理地址相同

extern char etext[];  // kernel.ld sets this to end of kernel code.   etext是kernel text(内核的代码文本)的最后一个地址

extern char trampoline[]; // trampoline.S

// 以 kvm 开头的函数操作内核页表
/*
 * create a direct-map page table for the kernel.
 */
// 发生在xv6在RISC-V启用分页之前，地址直接指向物理内存，这个函数由main调用，为内核创建一个虚拟地址与物理地址相同的映射表(Pagetable)，后续将使用该表
void
kvminit()
{
  kernel_pagetable = (pagetable_t) kalloc();  // 分配一页物理内存来存放根页表页
  memset(kernel_pagetable, 0, PGSIZE);

  // 将内核所需要的硬件资源`映射`到物理地址(这些资源包括内核的指令和数据，KERNBASE到PHYSTOP（0x86400000）的物理内存，以及实际上是设备的内存范围)

  // uart registers
  // kvmap参数uint64 va, uint64 pa均为UART0，此内核页表虚拟地址等于物理地址
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
// main 调用 kvminit 后紧接着调用 kvminithart 来映射内核页表，其将根页表页的物理地址写入寄存器satp中，开启分页
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));    // 将刚刚init的内核pagetable的顶级地址给satp寄存器，接下来就可以使用内核页表
  sfence_vma();   // 刷新当前CPU的TLB缓存
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
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
// 通过虚拟地址得到最低层PTE的指针，这个PTE内容包含物理地址所在页页首地址（因此一定能被%PAGESIZE整除）

// 模仿RISC-V分页硬件查找虚拟地址的PTE。walk每次降低3级页表的9位。
// 它使用每一级的9位虚拟地址来查找下一级页表或最后一级（kernel/vm.c:78）的PTE。
// **为什么要采用9位虚拟地址索引一个页表页的PTE呢？**
//  2^9 = 512，一个页表页4K，一个页表项64位(8bytes)，那么一个页表页有512个页表项，正好可以使用512个地址给每个PTE索引
// 如果PTE无效，那么所需的物理页还没有被分配；如果alloc参数被设置true，walk会分配一个新的页表页，
// 并把它的物理地址放在PTE中。它返回在`树的最低层`的PTE地址 (It returns the address of the PTE in the lowest layer in the tree)
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  // 循环两次 -- 解析第0、第1级
  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];     // 9位bit是page的索引，负责定位出在这级pagetable的page中的PTE
    if(*pte & PTE_V) {                          // PTE是否存在：如果没有设置，对该页的引用会引起异常
      pagetable = (pagetable_t)PTE2PA(*pte);    // PTE指向新的下一级的pagetable的page物理地址
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)   // 造页
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  // 解析第2级页表
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
// 用户态虚拟地址 --> 物理地址(两地址一定能被PAGESIZE整除)
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);    // 读取PTE内容，即*(&pagetable[PX(0, va)])即pagetable[PX(0, va)]即虚拟地址指向的物理地址
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
    a += PGSIZE;
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
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
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
// 创建一个新用户页表，内含一页
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

// int gl = 0;    // Debug

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
// 遍历三级页表每级(一级页表至少一页，一页512个PTE)所有PTE，只要有PTE可以往下延伸，就继续递归
// 遍历完某页后再free掉
void
freewalk(pagetable_t pagetable)
{
  // printf("START============================================================%d\n", gl);
  // gl++;
  
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

  // gl--;
  // printf("END============================================================%d\n", gl);
}

// Free user memory pages,
// then free page-table pages.
// 全面free掉uvmalloc出的内存
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
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
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
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
// 将内核数据复制到用户虚拟地址
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

int
copyin_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len);

int
copyinstr_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max);

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
// 将用户虚拟地址的数据复制到内核空间地址，用户虚拟地址由系统调用的参数指定
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  return copyin_new(pagetable, dst, srcva, len);
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  return copyinstr_new(pagetable, dst, srcva, max);
}

void vmprint(pagetable_t pt, int level) {
  printf("Addr: %p\n", pt);
  
  // A pagetable page contains 512 PTEs
  for (int i = 0; i < 512; i++) {
    pte_t pte = pt[i];
    if (pte & PTE_V) {
      for (int j = level; j > 0; j--) {
        printf(".. ");
      }
      printf("..");
      printf("%d: pte %p pa %p\n", i, pte, PTE2PA(pte));

      uint64 child = PTE2PA(pte);
      if ((pte & (PTE_R | PTE_W | PTE_X)) == 0)   // (PTE_R | PTE_W | PTE_X)一旦被有一种被设置，那么说明这个PTE下面一定没有下一个页表页
        vmprint((pagetable_t)child, level + 1);
    }
  }
}

void uvmkpmap(pagetable_t kernel_pt, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kernel_pt, va, sz, pa, perm) != 0)
    panic("uvmkpmap");
}

// For simplicity, we just copy all PTEs from global kernel_pagetable except entry 0 in the 0th level of global kernel_pagetable
// (Because the entry 0 has ability to represent 1*(2^9)*(2^9)*4KB = 1GB physical pages. That enough to build mapping for user process memory limit.
// (https://pdos.csail.mit.edu/6.828/2020/labs/pgtbl.html#:~:text=You%27ll%20need%20to%20modify%20xv6%20to%20prevent%20user%20processes%20from%20growing%20larger%20than%20the%20PLIC%20address )
// Processes of kernel stacks, kernel self's data, kernel self's instruction and etc. are mapping by entry from entry 1 to entry 511(actually 255)
pagetable_t proc_user_kernel_pagetable() {
  pagetable_t kernel_pt = uvmcreate();
  if (!kernel_pt) {
    return 0;
  }

  // Unnecessary actually
    for (int i = 1; i < 512; i++) {
    kernel_pt[i] = kernel_pagetable[i];
  }

  // uart registers
  uvmkpmap(kernel_pt, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  uvmkpmap(kernel_pt, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  uvmkpmap(kernel_pt, CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  uvmkpmap(kernel_pt, PLIC, PLIC, 0x400000, PTE_R | PTE_W);


  return kernel_pt;
}

// For fork(), exec(), and sbrk() process space building
// Allocate new pages from oldsz(address) to newsz(address) because of user process memory start at 0x0
// 从提供的user pagetable和kernel user pagetable中在两页表找到va对应的最后级页表PTE，复制用户页表的该PTE值到用户内核页表最后级页表PTE
void uvmCopyUserPt2UkernelPt(pagetable_t userPt, pagetable_t ukPt, uint64 oldsz, uint newsz) {
  if (newsz >= PLIC) {
    panic("uvmCopyUserPt2UkernelPt: User process overflowed");
  }

  for (uint64 va = oldsz; va < newsz; va += PGSIZE) {
    pte_t* uPTE  = walk(userPt, va, 0);
    pte_t* ukPTE = walk(ukPt, va, 1);

    *ukPTE  = *uPTE;
    *ukPTE &= ~(PTE_U|PTE_W|PTE_X);   // 取消不必要权限，这里只需要R，防止用户态内存破坏内核
  }
}
