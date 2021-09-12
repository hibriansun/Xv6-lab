#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
#if defined(LAB_PGTBL) || defined(LAB_LOCK)
    statsinit();
#endif
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();         // physical page allocator
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging 内核告诉MMU来使用刚刚设置好的page table，在这之前，我们使用的都是物理地址，在这之后，CPU将使用内核页表使用翻译虚拟地址。由于内核使用唯一映射，所以指令的虚拟地址将映射到正确的物理内存地址
    procinit();      // process table 为每个进程分配一个`内核栈*`
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode cache
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
#ifdef LAB_NET
    pci_init();
    sockinit();
#endif    
    userinit();      // first user process
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();        
}

// `内核栈*`
// 在每一个进程的生命周期中，必然会通过到系统调用陷入内核。在执行系统调用陷入内核之后，
// 这些内核代码所使用的栈并不是原先用户空间中的栈，用户栈也不安全，而是一个内核空间的栈，这个称作进程的“内核栈”。
// CPU的栈指针也会随着特权级的切换而从用户程序栈空间切换到该程序的内核栈，这个内核栈在xv6中在内核态在main函数的procinit中kalloc出的