#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "memlayout.h"
#include "param.h"
#include "fcntl.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "fs.h"
#include "file.h"

uint64 
sys_mmap(void)
{
  uint64 addr, length, offset;
  int prot, flags, fd;
  if (argaddr(0, &addr) < 0 || argaddr(1, &length) < 0 || argint(2, &prot) < 0 
      || argint(3, &flags) || argint(4, &fd) < 0 || argaddr(5, &offset) < 0) 
    return -1;

  if (addr != 0) {
    panic("mmap(): let the kernel choose a starting address");
  }

  struct proc *p = myproc();
  struct file *f = p->ofile[fd];
  if (f->readable == 0 || ((prot & PROT_WRITE) != 0 && flags == MAP_SHARED && f->writable == 0)) {
    return -1;
  }

  addr = p->mmap_top - length;
  addr = PGROUNDDOWN(addr);
  if (addr < p->sz) {
    panic("mmap(): mmap area overlap with the heap");
  }
  p->mmap_top = addr;

  struct mmap_info *info = 0;
  for (info = p->mmap_infos; info < p->mmap_infos + NMMAP; info++) {
    if (info->addr == 0) {
      break;
    }
  }
  if (info == p->mmap_infos + NMMAP) 
    return -1;

  info->addr = addr;
  info->length = length;
  info->prot = prot;
  info->flags = flags;
  info->fd = fd;
  info->offset = offset;
  info->file = p->ofile[fd];
  filedup(info->file); // increment ref count

  return addr;
}

uint64 
sys_munmap(void)
{
  uint64 addr, length;
  if (argaddr(0, &addr) < 0 || argaddr(1, &length) < 0) 
    return -1;

  return munmap_impl(addr, length);
}

int
alloc_mmap_page(uint64 faulting_addr)
{
  struct proc *p = myproc();
  struct mmap_info *info = 0;
  for (info = p->mmap_infos; info < p->mmap_infos + NMMAP; info++) {
    if (faulting_addr >= info->addr && faulting_addr < info->addr + info->length) {
      break;
    }
  }
  if (info == p->mmap_infos + NMMAP) {
    printf("alloc_mmap_page(): invalid faulting address: %lu\n", faulting_addr);
    return -1;
  }

  faulting_addr = PGROUNDDOWN(faulting_addr);

  char *mem;
  mem = kalloc();
  if (mem == 0) {
    return -2;
  }
  memset(mem, 0, PGSIZE);

  // read from file
  uint64 offset = info->offset + (faulting_addr - info->addr);
  ilock(info->file->ip);
  readi(info->file->ip, 0, (uint64)mem, offset, PGSIZE);
  iunlock(info->file->ip);

  int perm = PTE_U;
  if (info->prot & PROT_READ) {
    perm |= PTE_R;
  }
  if (info->prot & PROT_WRITE) {
    perm |= PTE_W;
  }
  if (mappages(p->pagetable, faulting_addr, PGSIZE, (uint64)mem, perm) != 0) {
    kfree(mem);
    return -3;
  }

  return 0;
}

int 
munmap_impl(uint64 addr, uint64 length) 
{
  struct proc *p = myproc();
  struct mmap_info *info;
  for (info = p->mmap_infos; info < p->mmap_infos + NMMAP; info++) {
    if (addr >= info->addr && addr < info->addr + info->length) {
      break;
    }
  }
  if (info == p->mmap_infos + NMMAP) {
    printf("alloc_mmap_page(): invalid address: %lu\n", addr);
    return -1;
  }

  // unmap either the begin part or the end part
  uint64 beg_addr, end_addr;
  if (addr == info->addr && (addr + length) == (info->addr + info->length)) {
    // delete the whole mapping
    beg_addr = info->addr;
    end_addr = PGROUNDUP(addr + length);
    info->addr = 0;
    if (addr == p->mmap_top) {
      p->mmap_top = end_addr;
    }
  } else if (addr == info->addr) {
    // delete the begin part
    if (length > info->length) {
      return -1;
    }
    beg_addr = PGROUNDDOWN(addr);
    end_addr = PGROUNDDOWN(addr + length);
    uint64 gap = end_addr - beg_addr;
    info->addr = end_addr;
    info->length -= gap;
    info->offset += gap;
    if (addr == p->mmap_top) {
      p->mmap_top = end_addr;
    }
  } else if ((addr + length) == (info->addr + info->length)) {
    // delete the end part
    beg_addr = PGROUNDUP(addr);
    end_addr = addr + length;
    info->length -= end_addr - beg_addr;
  } else {
    return -1;
  }

  uint64 a;
  pte_t *pte;
  uint64 offset = beg_addr - info->addr + info->offset;
  for (a = beg_addr; a < end_addr; a += PGSIZE, offset += PGSIZE) {
    if ((pte = walk(p->pagetable, a, 0)) == 0) 
      continue;
    if((*pte & PTE_V) == 0)
      continue;
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("sys_munmap: not a leaf");
    
    uint64 pa = PTE2PA(*pte);
    if((*pte & PTE_D) != 0 && (info->flags & MAP_SHARED) != 0) {
      // write back the modified data
      begin_op();
      ilock(info->file->ip);
      writei(info->file->ip, 0, pa, offset, PGSIZE);
      iunlock(info->file->ip);
      end_op();
    }
    kfree((void*)pa);
    *pte = 0;
  }

  if (info->addr == 0) {
    fileclose(info->file);
  }

  return 0;
}