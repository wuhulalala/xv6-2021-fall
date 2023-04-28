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

#define PAINDEX(a) (((a) - KERNBASE) / PGSIZE)
#define MAX_ENTRY PAINDEX(PHYSTOP)
int refer_count[MAX_ENTRY];
struct spinlock pgreflock;


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
  //initlock(&pgreflock, "pgref");
  freerange(end, (void*)PHYSTOP);
  //for(uint64 pa = PGROUNDUP((uint64)end); pa <= PHYSTOP; pa += PGSIZE){
    //refer_count[PAINDEX((uint64)pa)] = 0;
  //}
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

  acquire(&pgreflock);
  refer_decrease((uint64)pa);
  // Fill with junk to catch dangling refs.
  if(refer_count[PAINDEX((uint64)pa)] <= 0){
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  release(&pgreflock);
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

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    refer_count[PAINDEX((uint64)r)] = 1;
  }
  return (void*)r;
}

void*
iscowpagekalloc(pagetable_t pagetable, uint64 pa)
{
  acquire(&pgreflock);
  if(refer_count[PAINDEX((uint64)pa)] <= 1){
    release(&pgreflock);
    return (void*)pa;
  }

  char* mem = kalloc();
  if(!mem){
    release(&pgreflock);
    return 0;
  }
  memmove(mem, (void*)pa, PGSIZE);
  refer_decrease(pa);
  release(&pgreflock);
  return (void*)mem;
}

void 
refer_decrease(uint64 pa)
{
  refer_count[PAINDEX((uint64)pa)] -= 1;
}

void 
refer_increase(uint64 pa)
{
  acquire(&pgreflock);
  refer_count[PAINDEX((uint64)pa)] += 1;
  release(&pgreflock);
}
