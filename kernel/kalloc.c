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

#define PHY_PAGE_CNT ((PHYSTOP - KERNBASE) / PGSIZE)
#define PAGE_IDX(pa) ((pa - KERNBASE) / PGSIZE);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

int g_page_ref_count[PHY_PAGE_CNT];


struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);

  for (uint64 idx = 0; idx < PHY_PAGE_CNT; idx++) {
    g_page_ref_count[idx] = 0;
  }
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

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// increment reference count to a physical pages by one 
void 
inc_page_ref_count(uint64 pa) 
{
  uint64 idx = PAGE_IDX(pa);
  if (idx >= PHY_PAGE_CNT) {
    printf("idx: %p, pa: %p, PHY_PAGE_CNT: %p\n", idx, pa, PHY_PAGE_CNT);
    panic("invalid physical address in inc");
  }

  acquire(&kmem.lock);
  g_page_ref_count[idx]++;
  release(&kmem.lock);

  if (g_page_ref_count[idx] < 0) {
    panic("too many increments");
  }
}

int
dec_page_ref_count(uint64 pa) {
  uint64 idx = PAGE_IDX(pa);
  if (idx >= PHY_PAGE_CNT) {
    printf("idx: %p, pa: %p, PHY_PAGE_CNT: %p\n", idx, pa, PHY_PAGE_CNT);
    panic("invalid physical address in dec");
  }

  acquire(&kmem.lock);
  int ret = --g_page_ref_count[idx];
  release(&kmem.lock);

  if (g_page_ref_count[idx] < 0) {
    panic("too many decrements");
  }

  return ret;
}
