// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct free_memory {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct free_memory cpu_freelist[NCPU];

void
kinit()
{
  // initlock(&kmem.lock, "kmem");

  struct free_memory* cur_freelist;
  for (int i = 0; i < NCPU; i++) {
    cur_freelist = &cpu_freelist[i];
    initlock(&cur_freelist->lock, "cpu-mem");
    cur_freelist->freelist = 0;
  }

  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int id = cpuid();
  pop_off();

  // acquire(&kmem.lock);
  // r->next = kmem.freelist;
  // kmem.freelist = r;
  // release(&kmem.lock);
  struct free_memory* cur_freelist = &cpu_freelist[id];
  acquire(&(cur_freelist->lock));
  r->next = cur_freelist->freelist;
  cur_freelist->freelist = r;
  release(&(cur_freelist->lock));
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int id = cpuid();
  pop_off();
  struct free_memory* cur_freelist = &cpu_freelist[id];
  acquire(&(cur_freelist->lock));
  r = cur_freelist->freelist;
  if (r) {
    cur_freelist->freelist = r->next;
    release(&(cur_freelist->lock));
  } else {
    // first release current lock to avoid deadlock
    release(&(cur_freelist->lock));

    // steal from other cpu
    int idx = (id + 1) % NCPU;
    struct free_memory *mem;
    while (idx != id) {
      mem = &cpu_freelist[idx];
      acquire(&(mem->lock));
      r = mem->freelist;
      if (r) {
        mem->freelist = r->next;
        release(&(mem->lock));
        break;
      }
      release(&(mem->lock));
      idx = (idx + 1) % NCPU;
    }
  }

  // acquire(&kmem.lock);
  // r = kmem.freelist;
  // if(r)
  //   kmem.freelist = r->next;
  // release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
