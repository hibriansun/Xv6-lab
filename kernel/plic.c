#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

//
// the riscv Platform Level Interrupt Controller (PLIC).
//

void
plicinit(void)
{
  // set desired IRQ priorities non-zero (otherwise disabled).
  // PLIC与外设一样，也占用了一个I/O地址 0xC000_0000
  // 设置PLIC会接收哪些中断，进而将中断路由到CPU
  *(uint32*)(PLIC + UART0_IRQ*4) = 1;   // 打开UART中断
  *(uint32*)(PLIC + VIRTIO0_IRQ*4) = 1; // 设置PLIC接收来自IO磁盘的中断
}

void
plicinithart(void)
{
  int hart = cpuid();
  
  // set uart's enable bit for this hart's S-mode. 
  // 每个CPU的核都表明自己对来自于UART和VIRTIO的中断感兴趣
  *(uint32*)PLIC_SENABLE(hart)= (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ);

  // set this hart's S-mode priority threshold to 0.
  // 因为我们忽略中断的优先级，所以我们将优先级设置为0
  *(uint32*)PLIC_SPRIORITY(hart) = 0;
}

// ask the PLIC what interrupt we should serve.
int
plic_claim(void)
{
  int hart = cpuid();
  int irq = *(uint32*)PLIC_SCLAIM(hart);
  return irq;
}

// tell the PLIC we've served this IRQ.
void
plic_complete(int irq)
{
  int hart = cpuid();
  *(uint32*)PLIC_SCLAIM(hart) = irq;
}
