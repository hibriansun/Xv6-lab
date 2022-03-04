#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// Log详述：
// https://www.notion.so/briansun/Xv6-cd6c4e1350154a0f8451978e3f7b7ca4


// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active.(防止一个涉及文件系统的syscall分裂到几个事务中)
// Thus there is never any reasoning required about whether 
// a commit might write an uncommitted system call's updates to disk.
// 因此，对于a commit是否会将未提交的系统调用的更新写入磁盘，永远不需要进行任何推理。
//
// A system call should call ``begin_op()/end_op()`` to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s(block (index) number) for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader {
  int n;   // counter: 修改的struct buf块数(未写入disk上data block的block数 在commit后置0)
  int block[LOGSIZE];  // 修改的struct buf块对应的扇区号数组，从bcache中拿出来使用
};

struct log {
  struct spinlock lock;
  int start;       // logstart
  int size;        // Number of log blocks (struct superlog.nlog)
  int outstanding; // how many FS sys calls are executing.
  int committing;  // in commit(), please wait.
  int dev;
  struct logheader lh;
};
struct log log;

static void recover_from_log(void);
static void commit();


// 文件系统需要被初始化，具体来说，需要从磁盘读取一些数据来确保文件系统的运行，比如说文件系统究竟有多大，
// 各种各样的东西在文件系统的哪个位置，同时还需要有crash recovery log。完成任何文件系统的操作都需要等待磁盘操作结束
// 但是XV6只能在用户进程的context下执行文件系统操作，那我们就在第一个用户进程开始执行前进行fsinit(-->initlog)
// main() --> 第一个用户进程开始执行userinit() --> forkret() --> fsinit() --> initlog()
void
initlog(int dev, struct superblock *sb)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: too big logheader");

  initlock(&log.lock, "log");
  log.start = sb->logstart;
  log.size = sb->nlog;
  log.dev = dev;
  recover_from_log();   // 文件系统初始化时
}

// 将在log域在一次事务中缓存的要写入data block的数据写入到disk上真正想写入的data block中的响应位置(home locations)
// Copy committed blocks from log to their home location
static void
install_trans(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *lbuf = bread(log.dev, log.start+tail+1); // read log block
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // read dst 读取修改过data block的块号对应的块到内存(用buf表示
    memmove(dbuf->data, lbuf->data, BSIZE);  // copy block to dst (log域缓存的写块复制到 原本要写的data block在内存中内对应的struct buf)
    bwrite(dbuf);  // write dst to disk 将data block在内存中的映射写入disk上的data block
    bunpin(dbuf);
    brelse(lbuf);
    brelse(dbuf);
  }
}

// Read the log header from disk into the in-memory log header
static void
read_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *lh = (struct logheader *) (buf->data);
  int i;
  log.lh.n = lh->n;
  for (i = 0; i < log.lh.n; i++) {
    log.lh.block[i] = lh->block[i];
  }
  brelse(buf);
}

// 将header块写到磁盘上，就表明已提交，为提交点，写完日志后的崩溃，会导致在重启后重新执行日志
// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
static void
write_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *) (buf->data);
  int i;
  hb->n = log.lh.n;   // 在commit()中已完成实际data block写入后将该值置为0表示事务完成 这里写入disk也写入0
                      // 这必须在下一个事务开始之前修改，这样崩溃就不会导致重启后的恢复使用这次的header和下次的日志块
  for (i = 0; i < log.lh.n; i++) {
    hb->block[i] = log.lh.block[i];
  }
  bwrite(buf);
  brelse(buf);
}

static void
recover_from_log(void)
{
  read_head();
  install_trans(); // if committed, copy from log to disk
  log.lh.n = 0; // 未写入data block的block数
  write_head(); // clear the old log and create the new log
}

// called at the start of each FS system call.
void
begin_op(void)
{
  acquire(&log.lock);
  while(1){
    if(log.committing){     // 等待正在被commit中进行睡眠
      sleep(&log, &log.lock);
    } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){  // 没有足够的日志空间用来容纳日志时会等待有充足空间再进行
      // 假设每次系统调用最多写入MAXOPBLOCKS个块
      // this op might exhaust log space; wait for commit.
      sleep(&log, &log.lock);
    } else {  // 可以将多个系统调用的写操作封装在一个事务中
      log.outstanding += 1; // 在本次commit中，多一个事务(内核线程)，并且该事务占有该commit中，别开始commit提交
      release(&log.lock);
      break;
    }
  }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void
end_op(void)
{
  int do_commit = 0;

  acquire(&log.lock);
  log.outstanding -= 1;
  if(log.committing)
    panic("log.committing");
  if(log.outstanding == 0){
    do_commit = 1;
    log.committing = 1;
  } else {
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    wakeup(&log);
  }
  release(&log.lock);

  if(do_commit){
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();     // 在结束fs写系统调用时进行一次提交，具体是否会写入disk要看同一事务下其他的系统调用是否结束，防止造成将一个系统调用的多个写分散到不同的事务中
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }
}

// Copy modified blocks from cache to log. 将事务中修改的每个块从buffer缓存中复制到磁盘上的日志槽中(log域中缓存一次事务写的data block的数据)
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *to = bread(log.dev, log.start+tail+1); // log block
    struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block (log.lh.block[tail]是块号，bread通过块号找到块的封装buf)
    memmove(to->data, from->data, BSIZE);
    bwrite(to);  // write the log to the **DISK**
    brelse(from);
    brelse(to);
  }
}

static void
commit()
{
  if (log.lh.n > 0) {
    write_log();     // Write modified blocks from cache to log
    write_head();    // Write header to disk -- the real commit
    install_trans(); // Now install writes to home locations
    log.lh.n = 0;
    write_head();    // Erase the transaction from the log
  }
}

// Caller(Program) has modified b->data and is done with the buffer.
// Record the block number and pin in the cache by increasing refcnt.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void
log_write(struct buf *b)
{
  int i;

  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  acquire(&log.lock);
  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // log absorbtion 一次事务的多次写都是对同一块进行写，那就将这多次合并成一次节省日志空间
      break;
  }
  log.lh.block[i] = b->blockno;     // 一次事务中对哪些data block写了需要将其块号写入到日志的header的扇区号数组
  if (i == log.lh.n) {  // Add new block to log?
    bpin(b);
    log.lh.n++;
  }
  release(&log.lock);
}

