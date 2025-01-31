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

unsigned char page_ref_cnt[PHYSTOP/PGSIZE];
struct spinlock page_ref_cnt_lock;

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void add_ref_cnt(uint64 pa) { 
  acquire(&page_ref_cnt_lock);
  int idx = pa >> 12; 
  page_ref_cnt[idx]++;
  release(&page_ref_cnt_lock);
}

void minus_ref_cnt(uint64 pa){
  acquire(&page_ref_cnt_lock);
  int idx = pa >> 12;
  page_ref_cnt[idx]--;
  release(&page_ref_cnt_lock);
}

void init_ref_cnt(uint64 pa){
  acquire(&page_ref_cnt_lock);
  int idx = pa >> 12;
  page_ref_cnt[idx] = 1;
  release(&page_ref_cnt_lock);
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&page_ref_cnt_lock,"page_ref_cnt");
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

  // some pages are not mapped in any pagetable,such as the pagetable page,
  // but we still need to free them,so minus their ref_cnt first
  // if a page is not cow page, its ref_cnt will be 1,also has to minus 1 to be 0
  acquire(&page_ref_cnt_lock);
  int idx = (uint64)pa >> 12;
  if(page_ref_cnt[idx]>=1){
    page_ref_cnt[idx]--;
  }
  if(page_ref_cnt[idx]>0){
    release(&page_ref_cnt_lock);
    return;
  }
  release(&page_ref_cnt_lock);

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
  {
    memset((char*)r, 5, PGSIZE); // fill with junk
    init_ref_cnt((uint64)r);
  }
  return (void *)r;
}
