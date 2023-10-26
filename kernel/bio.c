// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
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

#define NBUFMAP_BUCKET 13 //几个哈希表
#define BUFMAP_HASH(dev, blockno) ((((dev)<<27)|(blockno))%NBUFMAP_BUCKET) // 计算哈希函数

struct {
  struct buf buf[NBUF];
  struct spinlock eviction_locks[NBUFMAP_BUCKET];//驱逐锁

  // Hash map: dev and blockno to buf
  struct buf bufmap[NBUFMAP_BUCKET];
  struct spinlock bufmap_locks[NBUFMAP_BUCKET];
} bcache;

void
binit(void)
{
  // Initialize bufmap
  for(int i=0;i<NBUFMAP_BUCKET;i++) {
    initlock(&bcache.eviction_locks[i], "bcache_eviction");
    initlock(&bcache.bufmap_locks[i], "bcache_bufmap");
    bcache.bufmap[i].next = 0;
  }

  // Initialize buffers
  for(int i=0;i<NBUF;i++){
    struct buf *b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    b->lastuse = 0;
    b->refcnt = 0;
    // put all the buffers into bufmap[0]
    b->next = bcache.bufmap[0].next;
    bcache.bufmap[0].next = b;
  }
}

// 在设备 dev 上查找缓存块。
// 如果未找到，则分配一个缓存块。
// 无论哪种情况，都会返回一个已锁定的缓存块。
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint key = BUFMAP_HASH(dev, blockno);

  // printf("dev: %d, blockno: %d, locked: %d\n", dev, blockno, bcache.bufmap_locks[key].locked);
  
  acquire(&bcache.bufmap_locks[key]);

  // 这个块是否已经在缓存中？
  for(b = bcache.bufmap[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bufmap_locks[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 未在缓存中找到。

  // 为了获得一个适合重用的块，我们需要在所有的桶中搜索一个块，
  // 这意味着需要获取它们的桶锁。
  // 但是在持有一个锁的同时尝试获取每个单独的桶锁是不安全的。
  // 这可能很容易导致循环等待，从而产生死锁。

  release(&bcache.bufmap_locks[key]);
  // 我们需要释放桶锁，以便在迭代所有桶时不会产生循环等待和死锁。
  // 但是，释放桶锁的副作用是，其他 CPU 可能会同时请求相同的块号，
  // 在最坏的情况下，块号的缓存块可能被多次创建。
  // 因此，在获取 eviction_locks[key] 之后，我们再次检查“块是否已缓存”，
  // 以确保我们不会创建重复的缓存块。


  // 阻止其他线程启动并发的逐出操作（防止相同块号的重复缓冲区）
  acquire(&bcache.eviction_locks[key]);

  // 再次检查，块是否已缓存？
  // 我们在持有 eviction_locks[key] 期间不会发生针对该桶的其他分配，
  // 这意味着此桶的链接列表结构不会改变。
  // 因此，在不持有相应桶锁的情况下，通过 `bcache.bufmap[key]` 进行迭代是可以的，
  // 因为我们持有更强的 eviction_locks[key]。
  for(b = bcache.bufmap[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      acquire(&bcache.bufmap_locks[key]); // 必须，用于 `refcnt++`
      b->refcnt++;
      release(&bcache.bufmap_locks[key]);
      release(&bcache.eviction_locks[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 仍未缓存。
  // 现在我们只持有逐出锁，没有任何桶锁由我们持有。
  // 因此，现在可以安全地获取任何桶的锁，而不会出现循环等待和死锁。

  // 查找所有桶中最近最不常使用的缓冲区。
  // 完成后，它将持有相应桶的锁。
  struct buf *before_least = 0; 
  uint holding_bucket = -1;
  for(int i = 0; i < NBUFMAP_BUCKET; i++){
    // 在获取之前，要么没有持有锁，要么只有左侧的桶的锁。
    // 所以这里永远不会发生循环等待。（免于死锁）
    acquire(&bcache.bufmap_locks[i]);
    int newfound = 0; // 在此桶中找到的新的最不常使用的缓冲区
    for(b = &bcache.bufmap[i]; b->next; b = b->next) {
      if(b->next->refcnt == 0 && (!before_least || b->next->lastuse < before_least->next->lastuse)) {
        before_least = b;
        newfound = 1;
      }
    }
    if(!newfound) {
      release(&bcache.bufmap_locks[i]);
    } else {
      if(holding_bucket != -1) release(&bcache.bufmap_locks[holding_bucket]);
      holding_bucket = i;
      // 保持此桶的锁...
    }
  }
  if(!before_least) {
    panic("bget:no buffers");
  }
  b = before_least->next;
  
  if(holding_bucket != key) {
    // 从原始桶中删除缓冲区
    before_least->next = b->next;
    release(&bcache.bufmap_locks[holding_bucket]);
    // 重新哈希并将其添加到正确的桶中
    acquire(&bcache.bufmap_locks[key]);
    b->next = bcache.bufmap[key].next;
    bcache.bufmap[key].next = b;
  }
  
  b->dev = dev;
  b->blockno = blockno;
  b->refcnt = 1;
  b->valid = 0;
  release(&bcache.bufmap_locks[key]);
  release(&bcache.eviction_locks[key]);
  acquiresleep(&b->lock);
  return b;
}


// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void//将一个缓冲区的内容写入磁盘。
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
	virtio_disk_rw(b, 1);
}

// Release a locked buffer.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->lastuse = ticks;//当前系统的时间戳
  }//当引用计数 b->refcnt 仍为正数时，没有必要更新 lastuse 字段，因为最近最少使用只看空页
  release(&bcache.bufmap_locks[key]);
}

void
bpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.bufmap_locks[key]);
  b->refcnt++;//非原子操作，需要上锁
  release(&bcache.bufmap_locks[key]);
}

void
bunpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  release(&bcache.bufmap_locks[key]);
}
