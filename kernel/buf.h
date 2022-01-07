struct buf {
  int valid;   // has data been read from disk? (data will be reload from disk when vaild == 0)
  int disk;    // does disk "own" buf? (the buffer content has been handed to the disk)
  uint dev;    // which device own the block?
  uint blockno;   // which block does the buffer cache?
  struct sleeplock lock;  // protects reads and writes of the block's buffered content
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};

