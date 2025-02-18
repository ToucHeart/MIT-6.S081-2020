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

struct buf buf[NBUF];

typedef struct bucket{
  struct spinlock lock;
  struct buf elemhead;
}bucket;

#define BUCKETSIZE 13

bucket buckets[BUCKETSIZE];

void
binit(void)
{
  for(int i=0;i < BUCKETSIZE;++i){
    initlock(&buckets[i].lock,"buckets");
    buckets[i].elemhead.next = &buckets[i].elemhead;
    buckets[i].elemhead.prev = &buckets[i].elemhead;
  }
  for(int i = 0;i < NBUF;++i){
    initsleeplock(&(buf[i].lock), "buffer");
    buf[i].timestamp = ticks;
    int target = i%BUCKETSIZE;
    buf[i].next = buckets[target].elemhead.next;
    buf[i].prev = &buckets[target].elemhead;
    buckets[target].elemhead.next->prev = &buf[i];
    buckets[target].elemhead.next = &buf[i];
  }
}

static struct buf*
bget_helper(uint dev, uint blockno, int bucketidx, int steal)
{
  int target = steal ? bucketidx : blockno % BUCKETSIZE;
  acquire(&buckets[target].lock);
  
  struct buf *b;
  // Is the block already cached?
  if(!steal){
    for(b = buckets[target].elemhead.next; b != &buckets[target].elemhead; b = b->next){
      if(b->dev == dev && b->blockno == blockno){
        b->refcnt++;
        release(&buckets[target].lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  uint lru = (uint)-1;
  struct buf* ret = 0;
  for(b = buckets[target].elemhead.next; b != &buckets[target].elemhead; b = b->next){
    if(b->refcnt == 0 && b->timestamp < lru) {
      ret = b;
      lru = b->timestamp;
    }
  }
  if(ret){
    ret->dev = dev;
    ret->blockno = blockno;
    ret->valid = 0;
    ret->refcnt = 1;
    if(steal){          // remove from current list
      ret->next->prev = ret->prev; 
      ret->prev->next = ret->next;
      ret->next = ret->prev = 0;
    }
    release(&buckets[target].lock);
    acquiresleep(&ret->lock);
    return ret;
  }
  release(&buckets[target].lock);
  return 0;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf* ret = bget_helper(dev,blockno,0,0);
  if(ret){
    return ret;
  }
  int cur_bucket = blockno % BUCKETSIZE;
  for(int i = 0;i < BUCKETSIZE;++i){
    if(i != cur_bucket){
      ret = bget_helper(dev,blockno,i,1);
      //add the buf stolen from others to this list
      if(ret){
        acquire(&buckets[cur_bucket].lock);
        ret->next = buckets[cur_bucket].elemhead.next;
        ret->prev = &buckets[cur_bucket].elemhead;
        ret->next->prev = ret;
        buckets[cur_bucket].elemhead.next = ret;
        release(&buckets[cur_bucket].lock);
        return ret;
      }
    }
  }
  panic("no free buf");
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
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int target = b->blockno%BUCKETSIZE;
  acquire(&buckets[target].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = buckets[target].elemhead.next;
    b->prev = &buckets[target].elemhead;
    buckets[target].elemhead.next->prev = b;
    buckets[target].elemhead.next = b;
    b->timestamp = ticks;
  }
  release(&buckets[target].lock);
}

void
bpin(struct buf *b) {
  int target = b->blockno%BUCKETSIZE;
  acquire(&buckets[target].lock);
  b->refcnt++;
  release(&buckets[target].lock);
}

void
bunpin(struct buf *b) {
  int target = b->blockno%BUCKETSIZE;
  acquire(&buckets[target].lock);
  b->refcnt--;
  release(&buckets[target].lock);
}


