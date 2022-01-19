// Semaphore
// Wait queues marked as the different p->chan
struct semaphore {
  uint capacity;       // Is the lock held?
  struct spinlock lk;  // spinlock protecting this sleep lock
  
  // For debugging:
  char *name;        // Name of lock.
  int pid;           // Process holding lock
}; 

