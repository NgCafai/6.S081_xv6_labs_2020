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
#include "limits.h"

#define NBUCKET 13

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;

  struct buf *head_list[NBUCKET]; 
  struct spinlock lock_list[NBUCKET];
  struct spinlock eviction_lock;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }

  initlock(&bcache.eviction_lock, "eviction_lock");

  char name_buf[100];
  for (int i = 0; i < NBUCKET; i++) {
    int n = snprintf(name_buf, sizeof(name_buf), "bcache-%d", i);
    name_buf[n] = 0;
    initlock(&(bcache.lock_list[i]), name_buf);
    // printf("name: %s\n", name_buf);
  }

  // give all buf to the first bucket
  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    struct buf *head = bcache.head_list[0];
    b->next = head;
    if (head != 0) {
      head->prev = b;
    }
    bcache.head_list[0] = b;
    initsleeplock(&b->lock, "buffer");
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // acquire(&bcache.lock);

  // Is the block already cached?
  // for(b = bcache.head.next; b != &bcache.head; b = b->next){
  //   if(b->dev == dev && b->blockno == blockno){
  //     b->refcnt++;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
  int bucket_idx = blockno % NBUCKET;
  acquire(&(bcache.lock_list[bucket_idx]));
  b = bcache.head_list[bucket_idx];
  for (; b != 0; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&(bcache.lock_list[bucket_idx]));
      // release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  // for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
  //   if(b->refcnt == 0) {
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
  int smallest_idx = -1;
  struct buf *smallest_buf;
  uint64 smallest_timestamp = ULONG_MAX;
  int idx = (bucket_idx + 1) % NBUCKET;
  
  release(&(bcache.lock_list[bucket_idx]));
  acquire(&bcache.eviction_lock);
  acquire(&(bcache.lock_list[bucket_idx]));

  // check again
  b = bcache.head_list[bucket_idx];
  for (; b != 0; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&(bcache.lock_list[bucket_idx]));
      release(&bcache.eviction_lock);
      // release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  while (idx != bucket_idx) {
    // acquire locks for buckets in ascending order of index
    // if (idx > bucket_idx) {
      acquire(&(bcache.lock_list[idx]));
    // } else {
    //   release(&(bcache.lock_list[bucket_idx]));
    //   acquire(&(bcache.lock_list[idx]));
    //   acquire(&(bcache.lock_list[bucket_idx]));
    // }
    b = bcache.head_list[idx];
    for (; b != 0; b = b->next) {
      if (b->refcnt == 0 && b->timestamp < smallest_timestamp) {
        smallest_idx = idx;
        smallest_timestamp = b->timestamp;
        smallest_buf = b;
      }
    }

    release(&(bcache.lock_list[idx]));
    idx = (idx + 1) % NBUCKET;
  }

  if (smallest_idx >= 0) {
    
    // if (smallest_idx > bucket_idx) {
      acquire(&(bcache.lock_list[smallest_idx]));
    // } else {
    //   release(&(bcache.lock_list[bucket_idx]));
    //   acquire(&(bcache.lock_list[smallest_idx]));
    //   acquire(&(bcache.lock_list[bucket_idx]));
    // }

    if (smallest_buf->prev == 0) {
      bcache.head_list[smallest_idx] = smallest_buf->next;
      if (bcache.head_list[smallest_idx] != 0) {
        bcache.head_list[smallest_idx]->prev = 0;
      }
    } else {
      smallest_buf->prev->next = smallest_buf->next;
      if (smallest_buf->next != 0)
        smallest_buf->next->prev = smallest_buf->prev;
    }
    release(&(bcache.lock_list[smallest_idx]));

    smallest_buf->dev = dev;
    smallest_buf->blockno = blockno;
    smallest_buf->valid = 0;
    smallest_buf->refcnt = 1;
    smallest_buf->next = bcache.head_list[bucket_idx];
    if (bcache.head_list[bucket_idx] != 0) {
      bcache.head_list[bucket_idx]->prev = smallest_buf;
    }
    smallest_buf->prev = 0;
    bcache.head_list[bucket_idx] = smallest_buf;
    release(&(bcache.lock_list[bucket_idx]));
    release(&bcache.eviction_lock);
    // release(&bcache.lock);

    acquiresleep(&smallest_buf->lock);
    return smallest_buf;
  }

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  
  // debug
  if (b < bcache.buf || b >= (bcache.buf + NBUF)) {
    printf("invalid b: %p\n", b);
  }

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

  // acquire(&bcache.lock);

  int bucket_idx = b->blockno % NBUCKET;
  acquire(&(bcache.lock_list[bucket_idx]));
  b->refcnt--;
  acquire(&tickslock);
  b->timestamp = ticks;
  release(&tickslock);
  release(&(bcache.lock_list[bucket_idx]));

  // b->refcnt--;
  // if (b->refcnt == 0) {
  //   // no one is waiting for it.
  //   b->next->prev = b->prev;
  //   b->prev->next = b->next;
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
  
  // release(&bcache.lock);
}

void
bpin(struct buf *b) {
  // acquire(&bcache.lock);
  int bucket_idx = b->blockno % NBUCKET;
  acquire(&(bcache.lock_list[bucket_idx]));

  b->refcnt++;
  
  // release(&bcache.lock);
  release(&(bcache.lock_list[bucket_idx]));
}

void
bunpin(struct buf *b) {
  // acquire(&bcache.lock);
  int bucket_idx = b->blockno % NBUCKET;
  acquire(&(bcache.lock_list[bucket_idx]));

  b->refcnt--;

  // release(&bcache.lock);
  release(&(bcache.lock_list[bucket_idx]));
}


