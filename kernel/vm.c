#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[];  // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t kvmmake(void) {
    pagetable_t kpgtbl;

    kpgtbl = (pagetable_t)kalloc();
    memset(kpgtbl, 0, PGSIZE);

    // uart registers
    kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

    // virtio mmio disk interface
    kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

    // PLIC
    kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

    // map kernel text executable and read-only.
    kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

    // map kernel data and the physical RAM we'll make use of.
    kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext,
           PTE_R | PTE_W);

    // map the trampoline for trap entry/exit to
    // the highest virtual address in the kernel.
    kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

    // map kernel stacks
    proc_mapstacks(kpgtbl);

    return kpgtbl;
}

// Initialize the one kernel_pagetable
void kvminit(void) {
    kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void kvminithart() {
    w_satp(MAKE_SATP(kernel_pagetable));
    sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t* walk(pagetable_t pagetable, uint64 va, int alloc) {
    if (va >= MAXVA)
        panic("walk");

    for (int level = 2; level > 0; level--) {
        pte_t* pte = &pagetable[PX(level, va)];
        if (*pte & PTE_V) {
            pagetable = (pagetable_t)PTE2PA(*pte);
        } else {
            if (!alloc || (pagetable = (pde_t*)kalloc()) == 0)
                return 0;
            memset(pagetable, 0, PGSIZE);
            *pte = PA2PTE(pagetable) | PTE_V;
        }
    }
    return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
// 映射虚拟地址和实际的物理地址.
uint64 walkaddr(pagetable_t pagetable, uint64 va) {
    pte_t* pte;
    uint64 pa;

    if (va >= MAXVA)
        return 0;

    pte = walk(pagetable, va, 0);
    if (pte == 0)
        return 0;
    if ((*pte & PTE_V) == 0)
        return 0;
    if ((*pte & PTE_U) == 0)
        return 0;
    pa = PTE2PA(*pte);
    return pa;
}

pte_t* walkpte(pagetable_t pagetable, uint64 va) {
    return walk(pagetable, va, 0);
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm) {
    if (mappages(kpgtbl, va, sz, pa, perm) != 0)
        panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa,
             int perm) {
    uint64 a, last;
    pte_t* pte;

    if (size == 0)
        panic("mappages: size");

    a = PGROUNDDOWN(va);
    last = PGROUNDDOWN(va + size - 1);
    for (;;) {
        if ((pte = walk(pagetable, a, 1)) == 0)
            return -1;
        if (*pte & PTE_V)
            panic("mappages: remap");
        *pte = PA2PTE(pa) | perm | PTE_V;
        if (a == last)
            break;
        a += PGSIZE;
        pa += PGSIZE;
    }
    return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
    uint64 a;
    pte_t* pte;

    if ((va % PGSIZE) != 0)
        panic("uvmunmap: not aligned");

    for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
        if ((pte = walk(pagetable, a, 0)) == 0)
            panic("uvmunmap: walk");
        if ((*pte & PTE_V) == 0)
            panic("uvmunmap: not mapped");
        if (PTE_FLAGS(*pte) == PTE_V)
            panic("uvmunmap: not a leaf");
        if (do_free) {
            uint64 pa = PTE2PA(*pte);
            kfree((void*)pa);
        }
        *pte = 0;
    }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t uvmcreate() {
    pagetable_t pagetable;
    pagetable = (pagetable_t)kalloc();
    if (pagetable == 0)
        return 0;
    memset(pagetable, 0, PGSIZE);
    return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
// ------------------ .
// 某个进程 分配对应的初始化代码
// 形参分别为 须分配的页表 需分配的代码 分配的代码段大小
void uvminit(pagetable_t pagetable, uchar* src, uint sz) {
    char* mem;

    // 代码段超过页大小 返回失败
    if (sz >= PGSIZE)
        panic("inituvm: more than a page");
    // 获取物理页 分配空间
    mem = kalloc();
    memset(mem, 0, PGSIZE);
    // 为页设置权限
    mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
    // 初始化代码复制到 分配好的页当中
    memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
    char* mem;
    uint64 a;

    if (newsz < oldsz)
        return oldsz;

    oldsz = PGROUNDUP(oldsz);
    for (a = oldsz; a < newsz; a += PGSIZE) {
        mem = kalloc();
        if (mem == 0) {
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
        memset(mem, 0, PGSIZE);
        if (mappages(pagetable, a, PGSIZE, (uint64)mem,
                     PTE_W | PTE_X | PTE_R | PTE_U) != 0) {
            kfree(mem);
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
    }
    return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
    if (newsz >= oldsz)
        return oldsz;

    if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
        int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
        uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
    }

    return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
// 递归到最深层 解索引.
void freewalk(pagetable_t pagetable) {
    // there are 2^9 = 512 PTEs in a page table.
    for (int i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];
        // pte不存在 或PTE_V没有设置

        // 标识非叶子节点（页目录项） 中间节点是没有R W X这些权限的
        if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            // this PTE points to a lower-level page table.
            uint64 child = PTE2PA(pte);
            freewalk((pagetable_t)child);
            // 清除其中内容
            pagetable[i] = 0;

            // 标识为叶子节点 只有物理的节点可以有R W X这些权限
        } else if (pte & PTE_V) {
            panic("freewalk: leaf");
        }
    }
    kfree((void*)pagetable);
}

void vmprint(pagetable_t pagetable, uint depth) {
    for (int i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];

        // 同理 非叶子节点 无权限
        if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {

            // 获取目录项的内存
            uint64 child = PTE2PA(pte);

            // 遍历表示深度
            for (int j = 1; j < depth; j++) {
                printf(".. ");
            }
            printf("..");
            printf("%d: pte %p pa %p\n", i, (void*)pte, (void*)child);

            // 递归查找
            vmprint((pagetable_t)child, depth + 1);

            // 叶子节点
        } else if (pte & PTE_V) {
            printf(".. .. ..");
            printf("%d: pte %p pa %p\n", i, (void*)pte, (void*)PTE2PA(pte));
        }
    }
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz) {
    if (sz > 0)
        uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
    freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
// sz代表当前进程（程序）已分配的最后一个地址 表示当前进程所能申请到的最大范围（当前进程占用的总空间大小）
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
    pte_t* pte;
    uint64 i;

    // uint64 pa, i;
    // uint flags;
    // char* mem;

    // 这里通过虚拟内存的最高地址(sz) 并且由于虚拟地址是顺序写的
    // 所以通过这个循环遍历就复制父进程原所有数据
    // for (i = 0; i < sz; i += PGSIZE) {
    //     if ((pte = walk(old, i, 0)) == 0)
    //         panic("uvmcopy: pte should exist");
    //     if ((*pte & PTE_V) == 0)
    //         panic("uvmcopy: page not present");
    //     pa = PTE2PA(*pte);
    //     flags = PTE_FLAGS(*pte);
    //     if ((mem = kalloc()) == 0)
    //         goto err;
    //     memmove(mem, (char*)pa, PGSIZE);
    //     if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {
    //         kfree(mem);
    //         goto err;
    //     }
    // }

    // 通过CopyOnWrite修改后的思路 遍历的时候 如果有PTE_W位 清除 并且打上PTE_C位
    for (i = 0; i < sz; i += PGSIZE) {
        if ((pte = walk(old, i, 0)) == 0)
            panic("uvmcopy: pte should exist");
        if ((*pte & PTE_V) == 0)
            panic("uvmcopy: page not present");
        // 如果有W位的话 清除 标记上COW位
        if ((*pte & PTE_W) == 1) {

            // 获取子进程的页表项（需确保子进程页表已分配）
            pte_t* pte_child =
                walk(new, i, 1);  // 第三个参数为1表示必要时创建页表项
            if (!pte_child)
                panic("uvmcopy: failed to map child PTE");

            *pte |= PTE_C;
            *pte &= ~PTE_W;
            uint64 pa = PTE2PA(*pte);
            uint flags = PTE_FLAGS(*pte);

            *pte = PA2PTE(pa) | flags;
            *pte_child = PA2PTE(pa) | flags;

            PTE_RCINC(*pte);
        }
        // 如果无W位 但是有C位 代表目前已经是在COW当中 在现有的引用次数上++
        else if ((*pte & PTE_C) == 1) {
            PTE_RCINC(*pte);
        }
    }

    return 0;

    // err:
    //     uvmunmap(new, 0, i / PGSIZE, 1);
    //     return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va) {
    pte_t* pte;

    pte = walk(pagetable, va, 0);
    if (pte == 0)
        panic("uvmclear");
    *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
// pagetable 用户为颗粒度的虚拟存储页表.
// dstva 目标虚拟地址.
// src 内核复制的起始地址.
// len 还需要操作的字节长度.
int copyout(pagetable_t pagetable, uint64 dstva, char* src, uint64 len) {
    uint64 n, va0, pa0;
    // va0是目标虚拟地址dstva的页对齐地址
    // pa0是va0对应的物理地址
    // n是当前操作的字节数（每轮遍历）
    // len是还需要操作的字节数

    while (len > 0) {
        // 获取页对齐地址
        va0 = PGROUNDDOWN(dstva);
        // 虚拟地址映射到物理地址
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        // 计算复制的字节数
        n = PGSIZE - (dstva - va0);
        if (n > len)
            n = len;
        // 复制
        memmove((void*)(pa0 + (dstva - va0)), src, n);

        // 更新遍历条件
        len -= n;
        src += n;
        // 控制循环条件 确保循环的时候 是页对齐的基础上开始遍历的（只有第一次进来的时候不是对齐的）
        dstva = va0 + PGSIZE;
    }
    return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char* dst, uint64 srcva, uint64 len) {
    uint64 n, va0, pa0;

    while (len > 0) {
        va0 = PGROUNDDOWN(srcva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        n = PGSIZE - (srcva - va0);
        if (n > len)
            n = len;
        memmove(dst, (void*)(pa0 + (srcva - va0)), n);

        len -= n;
        dst += n;
        srcva = va0 + PGSIZE;
    }
    return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char* dst, uint64 srcva, uint64 max) {
    uint64 n, va0, pa0;
    int got_null = 0;

    while (got_null == 0 && max > 0) {
        va0 = PGROUNDDOWN(srcva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        n = PGSIZE - (srcva - va0);
        if (n > max)
            n = max;

        char* p = (char*)(pa0 + (srcva - va0));
        while (n > 0) {
            if (*p == '\0') {
                *dst = '\0';
                got_null = 1;
                break;
            } else {
                *dst = *p;
            }
            --n;
            --max;
            p++;
            dst++;
        }

        srcva = va0 + PGSIZE;
    }
    if (got_null) {
        return 0;
    } else {
        return -1;
    }
}
