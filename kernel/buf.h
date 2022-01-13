struct buf {
  int valid;   // has data been read from disk? (data will be reload from disk when vaild == 0)
  int disk;    // does disk "own" buf? (the buffer content has been handed to the disk)
  uint dev;    // which device own the block?
  uint blockno;   // which block does the buffer cache?
  struct sleeplock lock;  // protects reads and writes of the block's buffered content
  uint refcnt;      // 尚未释放buffer的processes
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};


/**
 * Why sleeplock? Why not spinlock?
 * 1. It takes a lot of time to process I/O operations.
 * 2. Spinlock has many restrictions.
 *    e.g. locking the spinlock requires turning off interrupt on the current locking CPU core.
 *         We can't receive data from the disk anymore if we have only one core.
 *         (Data transforms by interrupts.)
 * 3. Sleeplock won't require to turn off interrupt when acquiring the sleeplock.
 * 4. Sleeplock is sleeping and gives up the CPU when acquiring the sleeplock. Spinlock works with busy-loop.
 */
