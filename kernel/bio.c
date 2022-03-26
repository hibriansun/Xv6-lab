// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a ```synchronization``` point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// LRU policy:
// the first buffer in the list (head.next) is the most recently used(written to the disk and released)
// the last is the least recently used

// buffer cache中的某个buffer可能被多个程序占有，他们在内核对buffer的同步机制下，并发访问，
// 但内核保证每次操作只能有一个线程读/写buffer，还要求访问如果是写操作，写完后需要release对锁的占用，
// 这样数据在被同步到disk的前提下，下一个线程就可以暂时独占该buffer

// 我们从disk读入数据到buffer cache，这个数据如果经常被访问(读/写)，那么我们尽量就把它留在内存中，减少I/O访问成本，
// 如果一个buffer我们访问的次数较低，且buffer cache往往空间有限，我们在buffer不足时将新的访问block缓存到哪里呢？
// 这时就选出访问次数较少的buffer将其位置占用替换成新内容。

// xv6实现的buffer cache的双向循环链表中，每个节点作为可被分配的buffer资源，位置不同，表示不同的访问热度，
// 按照一定的规则(LRU)在选出新的空闲的buffer节点场景下，最近被线程操作结束很可能再次有线程来访问的放在链表靠前位置，
// 已经被线程操作并释放完很久的buffer节点位于链表的末端，这就是我们选择新buffer供予新disk数据分配的目标。

// 非常好的关于LRU的思考
// https://www.cnblogs.com/KatyuMarisaBlog/p/14366115.html#%E5%85%B3%E4%BA%8E%E7%BC%93%E5%86%B2%E5%8C%BA%E7%BD%AE%E6%8D%A2%E7%AE%97%E6%B3%95%E7%9A%84%E4%B8%80%E4%BA%9B%E6%80%9D%E8%80%83

struct {
  struct spinlock lock;   // protects `information`(not buffer content) which blocks are cached
                          // 例如我们要从buffer cache中选一个struct buf出来放data block (bget())，那么需要要在此期间加锁保证操作原子性
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;      // dummy head node
} bcache;         // 在遍历buf的链表、访问buf除了data段的成员时(全部buf)，都需要持有着bcache.lock

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);  //  ensure that there is at most one cached buffer per disk block

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;      // ensure that no more one data block occupies the struct buf 
                        // after one data block chose and releasing bcache.lock
      release(&bcache.lock);
      acquiresleep(&b->lock);   // 以防止其他进程对这个buf进行读写操作，这样就达到了同步多进程对盘块的读写操作的目的(brelse解锁)
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  // 逆序遍历
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {     // ensure that no more one data block occupies the struct buf 
                             // after one data block chose and releasing bcache.lock
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;          // data will be reload from disk when vaild == 0
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  
  // the file system is too busy
  // TODO More gentle way is to sleep but it would face to deadlock issues.
  panic("bget: no buffers");
}

// bread使用睡眠锁
// Return a `locked` buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);   // Load the block from the disk. 
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  `Buffer must be locked.`
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);   // Store the block to the disk.
}

// 1. Release a locked buffer.
// 2. Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


