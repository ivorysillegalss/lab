// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void* pa_start, void* pa_end);

extern char end[];  // first address after kernel.
                    // defined by kernel.ld.
uint64 freemem = 0;

struct run {
    struct run* next;
};

struct {
    struct spinlock lock;
    struct run* freelist;
} kmem;

// 标识页被调用的次数
uint page_refs[PHYSTOP >> 12];
// 控制页调用的时候 所涉及到的并发锁
struct spinlock refs_lock;

void pin_page(uint32 index) {
    acquire(&refs_lock);
    page_refs[index]++;
    release(&refs_lock);
}

void unpin_page(uint32 index) {
    acquire(&refs_lock);
    page_refs[index]--;
    release(&refs_lock);
}

int get_page_ref(uint index) {
    return page_refs[index];
}

void kinit() {
    initlock(&kmem.lock, "kmem");
    freerange(end, (void*)PHYSTOP);
}

void freerange(void* pa_start, void* pa_end) {
    char* p;
    p = (char*)PGROUNDUP((uint64)pa_start);
    for (; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
        uint index = ((uint64)p - (uint64)pa_start) / PGSIZE;
        acquire(&refs_lock);
        page_refs[index] = 1;
        release(&refs_lock);
        kfree(p);
    }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void* pa) {
    struct run* r;

    if (((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    // 查看目标页的引用次数 大于1直接减 无需释放空间
    uint32 index = ((uint64)pa - PGROUNDUP((uint64)end)) / PGSIZE;

    if (get_page_ref(index) < 1) {
        printf("kfree: refs: %d\n", get_page_ref(index));
        panic("kfree: refs < 1.\n");
    }

    unpin_page(index);
    int refs = get_page_ref(index);
    if (refs > 0)
        return;

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;

    freemem += PGSIZE;
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void* kalloc(void) {
    struct run* r;

    acquire(&kmem.lock);
    r = kmem.freelist;
    if (r) {
        // 分配新页面的时候 同时也需要分配对应的引用次数
        kmem.freelist = r->next;
        uint32 index = ((uint64)r - PGROUNDUP((uint64)end)) / PGSIZE;
        acquire(&refs_lock);
        page_refs[index] = 1;
        release(&refs_lock);
    }

    freemem -= PGSIZE;
    release(&kmem.lock);

    if (r)
        memset((char*)r, 5, PGSIZE);  // fill with junk
    return (void*)r;
}

// 设置一个缓存变量 直接读取就可以了
// TODO CAS 现在有可能多个CPU下会有并发错误的
uint64 kgetmem(void) {
    // if (freemem != 0) {
    // return freemem;
    // } else {
    uint64 retV = 0;
    struct run* r;
    r = kmem.freelist;
    while (r != (void*)0) {
        retV += PGSIZE;
        r = r->next;
    }
    freemem = retV;
    return retV;
    // }
}