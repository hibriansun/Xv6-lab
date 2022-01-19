#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "semaphore.h"

void 
initsemaphore(struct semaphore *sema, char *name, uint capacity) {
    initlock(&sema->lk, "semaphore");
    sema->name = name;
    sema->capacity = capacity;
    sema->pid = 0;
}

// acquire
// failure leads to wait(/pending/sleeping)
void
sema_p(struct semaphore *sema) {
    acquire(&sema->lk);
    sema->capacity--;
    while (sema->capacity < 0) {        // 使用while防止过多唤醒
        sleep(sema, &sema->lk);         // 进入睡眠会自动解锁 保存上下文 加入等待队列 被调度 醒来会加锁
    }
    sema->pid = myproc()->pid;
    release(&sema->lk);
}

// release
// signal (wakeup)
void
sema_v(struct semaphore *sema) {
    acquire(&sema->lk);
    sema->capacity++;
    sema->pid = 0;
    if (sema->capacity <= 0) {
        // wake a process with the same chan (in the same queue)
        wakeup(sema);
    }
    release(&sema->lk);
}

