#include "types.h"
#include "riscv.h"
#include "spinlock.h"
#include "param.h"
#include "proc.h"
#include "defs.h"


uint64 sys_sigalarm(void) {
    struct proc* p = myproc();

    // Param-0 intervel ticks
    if (argint(0, &(p->interval)) == 0) {
        return -1;
    }

    // Param-1 handler pointer
    if (argaddr(1, (uint64*)(&(p->handler))) == 0) {
        return -1;
    }
    
    return 0;
}

uint64 sys_sigreturn(void) {
    struct proc* p = myproc();

    p->tickPassed = 0;
    *p->trapframe = p->trapframeBackup;
    
    return 0;
}