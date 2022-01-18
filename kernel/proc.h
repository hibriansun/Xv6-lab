// Saved registers for kernel context switches.
struct context {
// Register description: https://i.loli.net/2021/11/30/z6ayWqQiVONeEkn.png
  uint64 ra;    // return address
  uint64 sp;    // stack pointer

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// Per-CPU state. (每个CPU都有这样一个结构体 Per-CPU变量)
struct cpu {
  struct proc *proc;          // The process running on this cpu, or null.  // 该CPU运行任务了吗，是运行哪个进程
  struct context context;     // swtch() here to enter scheduler().         // 内核调度器线程的寄存器(上下文)(saved registers for the CPU’s scheduler thread)
  int noff;                   // Depth of push_off() nesting.               // to track the nesting level of locks on the current CPU
  int intena;                 // Were interrupts enabled before push_off()? // 关闭中断前中断开关状态
};

extern struct cpu cpus[NCPU];

// per-process data for the trap handling code in trampoline.S.
// sits in a page by itself just under the trampoline page in the
// user page table. not specially mapped in the kernel page table.
// the sscratch register points here.
// uservec in trampoline.S saves user registers in the trapframe,
// then initializes registers from the trapframe's
// kernel_sp, kernel_hartid, kernel_satp, and jumps to kernel_trap.
// usertrapret() and userret in trampoline.S set up
// the trapframe's kernel_*, restore user registers from the
// trapframe, switch to the user page table, and enter user space.
// the trapframe includes callee-saved user registers like s0-s11 because the
// return-to-user path via usertrapret() doesn't return through
// the entire kernel call stack.
struct trapframe {
  /*   0 */ uint64 kernel_satp;   // kernel page table
  /*   8 */ uint64 kernel_sp;     // top of process's kernel stack
  /*  16 */ uint64 kernel_trap;   // usertrap()
  /*  24 */ uint64 epc;           // saved user program counter
  /*  32 */ uint64 kernel_hartid; // saved kernel tp
  /*  40 */ uint64 ra;
  /*  48 */ uint64 sp;
  /*  56 */ uint64 gp;
  /*  64 */ uint64 tp;
  /*  72 */ uint64 t0;
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};

enum procstate { UNUSED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;        // Process state
  struct proc *parent;         // Parent process
  void *chan;                  // If non-zero, sleeping on chan 「chan相当于标记同一个等待队列 在睡眠唤醒时 唤醒会唤醒用一个等待队列上的」
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int pid;                     // Process ID

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack     // 用户进程的内核线程执行时使用的函数栈空间
  uint64 sz;                   // Size of process memory (bytes)      // 程序的heap向上拓展，sz即为sbrk拓展heap的位置 https://i.loli.net/2021/11/29/jInyDJB9Yog8QxN.png
  pagetable_t pagetable;       // User page table

  // 两类寄存器 -- 用户进程寄存器(保存至trapframe)  用户进程的内核线程的寄存器(保存至context) 还有一种调度器内核线程寄存器在CPU struct中
  struct trapframe *trapframe; // data page for trampoline.S          // 切入内核时需要保存到的"用户空间状态" 内含PC指针(program counter)
  struct context context;      // swtch() here to run process         // 用户进程进入内核运行其所属的内核线程，context是内核线程的内核寄存器
  
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
};
