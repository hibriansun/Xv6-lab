// Sleeping locks -- 即Mutex，锁的争用结果是睡眠等待直到可以用锁

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"

void
initsleeplock(struct sleeplock *lk, char *name)
{
  initlock(&lk->lk, "sleep lock");
  lk->name = name;
  lk->locked = 0;
  lk->pid = 0;
}

// sleeplock:
// 未争用到锁时：
// 1. 保存上下文
// 2. 插入运行队列(chan相当于标记同一个等待队列)
// 3. 修改进程状态为阻塞
// 4. 调度
void
acquiresleep(struct sleeplock *lk)
{
  acquire(&lk->lk);
  while (lk->locked) {
    sleep(lk, &lk->lk);   // 睡眠锁实现原理与Linux Mutex实现一样，当同一锁被二次争用时陷入到睡眠，睡眠中实现被调度
                          // https://blog.csdn.net/21cnbao/article/details/119708595
  }
  lk->locked = 1;
  lk->pid = myproc()->pid;
  release(&lk->lk);
}


// releasesleeplock:
// 1. 解锁
// 2. wakeup
//      |
//      |__ 1. 睡在chan上的移出等待队列
//          2. 改状态
//          3. 进就绪队列
void
releasesleep(struct sleeplock *lk)
{
  acquire(&lk->lk);
  lk->locked = 0;
  lk->pid = 0;
  wakeup(lk);
  release(&lk->lk);
}

int
holdingsleep(struct sleeplock *lk)
{
  int r;
  
  acquire(&lk->lk);
  r = lk->locked && (lk->pid == myproc()->pid);
  release(&lk->lk);
  return r;
}



