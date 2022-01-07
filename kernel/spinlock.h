// Mutual exclusion lock. -- 这里是互斥的但是是自旋的
// 与我们理解的mutex区别在争用锁失败后mutex是睡眠(mutex在xv6实现是sleeplock)，spinlock是busy loop
struct spinlock {
  uint locked;       // Is the lock held?

  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
};

