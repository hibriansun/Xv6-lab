// Mutual exclusion spin locks.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
// acquire自身是不能放弃CPU的，这里一开始就关中断了，目的是为了防止同一CPU上普通线程与中断争用同一把锁导致死锁(sys_sleep 与 clockintr)
void
acquire(struct spinlock *lk)
{
  // 如果不关闭中断，不保证从acquire到release同一把锁这段代码能一次性原子性地执行完，那么就可能导致
  push_off(); // disable interrupts to avoid deadlock.
  if(holding(lk))
    panic("acquire");       // 检测死锁

  // On RISC-V, sync_lock_test_and_set turns into an atomic swap:
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)

  // wrap the swap in a loop, retrying(spinning) until it has acquired the lock
  // amoswap  r, a  -- read the value from register a(address), and put the value into register r(address)
  // __sync_lock_test_and_set wrap the amoswap instruction and returning the old value the register it written

  // 对于synchronize指令，任何在它之前的load/store指令，都不能移动到它之后
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)      // 实现自旋 与 原子占用操作
    ;

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen strictly after the lock is acquired.
  // On RISC-V, this emits a fence instruction.
  // 禁止CPU和编译器在代码段使用锁时对指令流顺序进行re-order 可能导致锁窗口期的出现
  // 这里使用内存屏障
  // xv6中的屏障几乎在所有重要的情况下都会acquire和release强制顺序，因为xv6在访问共享数据的周围使用锁
  __sync_synchronize();

  // Record info about lock acquisition for holding() and debugging.
  lk->cpu = mycpu();
}

// Release the lock.
void
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  lk->cpu = 0;

  // Tell the C compiler and the CPU to not move loads or stores
  // past this point, to ensure that all the stores in the critical
  // section are visible to other CPUs before the lock is released,
  // and that loads in the critical section occur strictly before
  // the lock is released.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize();

  // Release the lock, equivalent to lk->locked = 0.
  // This code doesn't use a C assignment, since the C standard
  // implies that an assignment might be implemented with
  // multiple store instructions.
  // On RISC-V, sync_lock_release turns into an atomic swap:
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&lk->locked);     // C库提供的原子赋值

  pop_off();
}

// Check whether this cpu is holding the lock.
// Interrupts must be off.
int
holding(struct spinlock *lk)
{
  int r;
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}

// push_off/pop_off are like intr_off()/intr_on() except that they are matched:
// it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
// are initially off, then push_off, pop_off leaves them off.
// To track the nesting level of locks on the current CPU
void
push_off(void)
{
  int old = intr_get();

  intr_off();
  if(mycpu()->noff == 0)
    mycpu()->intena = old;  // 记录本次关中断且中断计数前中断开启与否状态
  mycpu()->noff += 1;       // 某个CPU核心上中断嵌套计数+1
}

void
pop_off(void)
{
  struct cpu *c = mycpu();
  if(intr_get())
    panic("pop_off - interruptible");
  if(c->noff < 1)
    panic("pop_off");
  c->noff -= 1;           // 某个CPU核心上中断嵌套计数-1
  if(c->noff == 0 && c->intena)
    intr_on();            // 某个CPU核心上中断嵌套计数等于0时开启中断
}
