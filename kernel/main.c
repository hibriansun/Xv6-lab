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
  if(cpuid() == 0){  // 第一个核启动走这里
    consoleinit();   // 初始化控制台设备能接收 设备(外部)中断、软件中断、定时器中断
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();         // physical page allocator     /// 建立内核虚拟地址空间中可供用户、内核申请虚拟页的空闲页链表(end~PHYSTOP)
    kvminit();       // create kernel page table    /// 映射内核虚拟地址空间到物理地址，建立转换页表与页表项
    kvminithart();   // turn on paging              /// 开启分页
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller     // 设置PLIC 使得中断能被CPU感知
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode cache
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
    userinit();      // first user process [P.S. 只有CPU hartid为0的hart执行userinit]
    __sync_synchronize();
    started = 1;
  } else {           // 第2 3...核心走这里
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
