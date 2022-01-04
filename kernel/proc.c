#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];      // 全局变量: CPUs的状态信息集合 (CPU Table)

struct proc proc[NPROC];    // 全局变量: 进程Processes的状态信息集合 (Process Table)

struct proc *initproc;

int nextpid = 1;            // 分配新进程pid所用
struct spinlock pid_lock;   // 对pid进行锁保护

extern void forkret(void);
static void wakeup1(struct proc *chan);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// initialize the proc table at boot time.
// 初始化"每个CPU上正在跑的进程记录表"
// 初始化锁、分配进程的内核栈
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");

      // Allocate a page for the process's kernel stack.
      // Map it high in memory, followed by an invalid
      // guard page.
      char *pa = kalloc();
      if(pa == 0)
        panic("kalloc");
      uint64 va = KSTACK((int) (p - proc));           // 内核虚拟地址空间地址
      kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
      p->kstack = va;   // 记录进程内核栈地址
  }
  kvminithart();        // TODO 为什么还要再开启一遍呢
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();   // disable interrupt to avoid processes yeilding because of timer interrupt
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();   // enable interrupt
  return p;
}

int
allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  // 建立Trapframe和trampoline页表映射
  // trapframe在上面已分配空间，且trampoline所有用户程序和内核都共享同一块物理内存
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;    // 对于新建的用户进程，其内核线程被调度线程调度运行时从调度线程切换到该进程的内核线程的那个地方在这里被设置
  p->context.sp = p->kstack + PGSIZE; // 同上，被再次swtch执行该内核线程时使用的函数调用内核栈在这里被记录恢复时从这里读取加载到sp寄存器

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process,
// with "no user memory", but with "trampoline pages and trapframe".
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();  // 新建一个用户虚拟地址空间的页表，大小为一页(即根级页表大小)
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
// 建立    第一个用户进程 (其他用户进程的创建都是通过fork)
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();    // 获取当前进程PCB

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  // 拷贝both the page table and the physical memory.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;             // 进程虚拟内存大小设置

  np->parent = p;             // 建立父子进程关系

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  np->state = RUNNABLE;

  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold p->lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    // this code uses pp->parent without holding pp->lock.
    // acquiring the lock first could cause a deadlock
    // if pp or a child of pp were also in exit()
    // and about to try to lock p.
    if(pp->parent == p){
      // pp->parent can't change between the check and the acquire()
      // because only the parent changes it, and we're the parent.
      acquire(&pp->lock);
      pp->parent = initproc;
      // we should wake up init here, but that would require
      // initproc->lock, which would be a deadlock, since we hold
      // the lock on one of init's children (pp). this is why
      // exit() always wakes init (before acquiring any locks).
      release(&pp->lock);
    }
  }
}

// sleep和wakeup可以用于许多种需要等待的情况。在xv6book第1章中介绍的一个有趣的例子是，一个子进程的exit和其父进程的wait之间的交互。
// 在子进程退出的时候，父进程可能已经在wait中睡眠了，也可能在做别的事情；在后一种情况下，后续的wait调用必须观察子进程的退出，也许是在它调用exit之后很久。
// xv6在wait观察到子进程退出之前，记录子进程退出的方式是让exit将调用进程设置为ZOMBIE状态，在那里停留，直到父进程的wait注意到它，
// 将子进程的状态改为UNUSED，复制子进程的退出状态，并将子进程的进程ID返回给父进程。如果父进程比子进程先退出，父进程就把子进程交给init进程，
// 而init进程则循环的调用wait；这样每个子进程都有一个“父进程”来清理。主要的实现挑战是父进程和子进程的wait和exit，
// 以及exit和exit之间可能出现竞争和死锁的情况。

// Xv6中两种结束进程的方式 -- kill and exit
// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
// 每个退出exit的进程都有来自父进程的wait
// 要退出的这个子进程不能释放全部自己的资源供重新利用，因为自己还在执行，会到父进程中wait再释放这个释放半截的子进程
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  // we might re-parent a child to init. we can't be precise about
  // waking up init, since we can't acquire its lock once we've
  // acquired any other proc lock. so wake up init whether that's
  // necessary or not. init may miss this wakeup, but that seems
  // harmless.
  // 任何一个进程的退出必须有父进程的等待，如果某一个进程有为子进程(即本身为父进程)，那么这个父进程退出时需要把自己所有的子进程reparent
  // 这里是给要退出的"子进程"的子进程们找新的父进程(init进程)
  acquire(&initproc->lock);
  wakeup1(initproc);
  release(&initproc->lock);

  // grab a copy of p->parent, to ensure that we unlock the same
  // parent we locked. in case our parent gives us away to init while
  // we're waiting for the parent lock. we may then race with an
  // exiting parent, but the result will be a harmless spurious wakeup
  // to a dead or wrong process; proc structs are never re-allocated
  // as anything else.
  // 处理罕见情况：该进程和该进程的父进程可能同时退出
  acquire(&p->lock);
  struct proc *original_parent = p->parent;
  release(&p->lock);
  
  // we need the parent's lock in order to wake it up from wait().
  // the parent-then-child rule says we have to lock it first.
  acquire(&original_parent->lock);

  acquire(&p->lock);  // 使得父进程(original_parent)看不见子进程p (父进程执行wait()时acquire(&np->lock)看不到该子进程 会spin等待)

  // Give any children to init.
  reparent(p);

  // Parent 'might be' sleeping in wait().
  wakeup1(original_parent);

  p->xstate = status;
  p->state = ZOMBIE;      // 这个进程很多资源都还没有被清理，因此我们设置一个中间状态ZOMBIE，待到**wait**中再清理占用资源、改变状态并可供重新利用

  release(&original_parent->lock);

  // 截止到现在 Child也没有free所有的resources，因为其还在执行，父进程此时清除子进程执行所需要的资源在wait中

  // Jump into the scheduler, never to return.
  sched();                // 会释放子进程的锁，此时父进程的wait可以看到该子进程并获取它的锁并予以释放资源回收利用
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
// 每个退出exit的进程都有来自父进程的wait
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  // hold p->lock for the whole time to avoid lost
  // wakeups from a child's exit().
  acquire(&p->lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      // this code uses np->parent without holding np->lock.
      // acquiring the lock first would cause a deadlock,
      // since np might be an ancestor, and we already hold p->lock.
      if(np->parent == p){    // np值可能等于p，因此不能先对np加锁，否则可能导致死锁。但不用考虑数据安全性吗：一个进程的父进程字段只有“父亲“改变，所以如果np->parent==p为真，除非当前进程改变它，否则该值就不会改变。
        // np->parent can't change between the check and the acquire()
        // because only the parent changes it, and we're the parent.
        acquire(&np->lock);
        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&p->lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&p->lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&p->lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &p->lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler. (每个CPU都有一个Scheduler，每个CPU遍历所有进程调度)
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    
    int nproc = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
    // >>> p->lock: 对选出的新进程处理 <<<
    // 选择一个进程将其投入运行时，会将该进程的内核线程的context加载到寄存器中，这个阶段不能进入中断
    // 否则进程被修改成running后，但其寄存器值没有被加载全转而就去执行中断，中断又对该内核线程寄存器进行不完整保存到context对象，形成错误
    // 在这种情况下，切换到一个新进程的过程中，也需要获取新进程的锁以确保其他的CPU核不能看到这个进程
      acquire(&p->lock);      // 这里的acquir会在返回后某地释放(yield sleep forkret)
      if(p->state != UNUSED) {
        nproc++;
      }
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);    // 执行swtch后下一步执行的就是ra，也就是放弃CPU时进程执行的代码的位置sched()

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
    if(nproc <= 2) {   // only init and sh exist
      intr_on();
      asm volatile("wfi");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
// 这里仅仅做一些确认性的检测，然后通过swtch调到真正的调度线程
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))  // 保证该CPU正在运行的进程必须持有锁
    panic("sched p->lock");
  if(mycpu()->noff != 1)  // 保证除了进程本身的锁之外其他的锁在进程被调度之前已经被释放，否则另一进程可能争用锁产生死锁(一旦acquire锁会关中断 因此时间中断无法解开死锁)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);  // 将这个用户进程的 内核线程 的寄存器保存在进程的context中，寄存器中内容替换成cpu的context中寄存器内容(CPU调度线程的寄存器)
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();

  // >>> p->lock: 对旧进程处理 <<<
  // 我们需要将旧进程的状态从RUNNING改成RUNABLE，我们需要将内核线程的寄存器保存在context对象中，并且我们还需要停止使用当前内核线程的内核栈
  // 这三步需要原子性完成，防止中断干扰
  acquire(&p->lock);      // 会关闭中断  这里对进程的加锁会在scheduler中解锁

  p->state = RUNNABLE;    // 转为就绪态
  sched();
  release(&p->lock);      // 在scheduler中加的锁 这里释放
}


// There is one case when the scheduler’s call to swtch does not end up in sched. When a new
// process is first scheduled, it begins at forkret.
// 新进程在allocproc时，设置了新进程的context的ra为forkret，那么当某个CPU上发生调度时，scheduler会选择到这个新进程，这个新进程就从这个
// 地方(forkret)开始执行
// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.(在开始挑选任务时)
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();  // 是一个假的函数，它会使得程序表现的看起来像是从trap中返回，但是对应的trapframe其实也是假的，这样才能跳到用户的第一个指令中
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.
  if(lk != &p->lock){  //DOC: sleeplock0 预防对p->lock加锁导致死锁
    acquire(&p->lock);  //DOC: sleeplock1
    release(lk);        // 这里如果不解锁可能造成死锁 详见xv6 book中信号量的实现
  }

  // Go to sleep.     通过记录它的sleep channel和标记SLEEPING状态 将process作为睡眠
  p->chan = chan;
  p->state = SLEEPING;

  sched();      // 调用swtch切换到其他线程上去执行 scheduler会释放最近运行进程锁 释放后wakeup则可以获取到进程锁并对其进行唤醒

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &p->lock){
    release(&p->lock);      // wakeup设置同chan的sleep proc为RUNNABLE待调度器恢复运行到sleep中sched后 scheduler挑选进程时加的锁在这里解锁
    acquire(lk);            // 在开始sleep时放开的非进程锁在这里恢复加锁
  }
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
    }
    release(&p->lock);
  }
}

// Wake up p if it is sleeping in wait(); used by exit().
// Caller 'must hold' p->lock.
static void
wakeup1(struct proc *p)
{
  if(!holding(&p->lock))
    panic("wakeup1");
  if(p->chan == p && p->state == SLEEPING) {
    p->state = RUNNABLE;
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
// kill几乎不会做什么事情，不会直接杀死某个进程，只是对进程的killed标志进行标识
// 在后续合适的位置(例如在trap.c中)检测这个标志return结束这个被标记的进程
// 1. 系统调用、时钟中断等trap后
// 2. 一些需要长时间等待的操作可能睡眠我们将其唤醒后检测killed标志是否将其退出
//    例如：piperead()，buffer一直为空，我们要kill这个process，必须尽快使其结束，而不能一致等待sleep直到能读到数据，这里return到trap中，因为piperead本身也是系统调用
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){   // 减少等待，不会让等待输入的进程等到很久之后输入了再被kill
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
