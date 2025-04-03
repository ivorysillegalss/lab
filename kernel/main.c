#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// 在start.c中 machine mode 转交权限后 就直接jump到了这里
// start() jumps here in supervisor mode on all CPUs.
void main() {
    if (cpuid() == 0) {
        // 初始化uart硬件
        consoleinit();
        printfinit();
        printf("\n");
        printf("xv6 kernel is booting\n");
        printf("\n");
        kinit();             // physical page allocator
        kvminit();           // create kernel page table
        kvminithart();       // turn on paging
        procinit();          // process table
        trapinit();          // trap vectors
        trapinithart();      // install kernel trap vector
        plicinit();          // set up interrupt controller
        plicinithart();      // ask PLIC for device interrupts
        binit();             // buffer cache
        iinit();             // inode table
        fileinit();          // file table
        virtio_disk_init();  // emulated hard disk
        // 第一个用户空间的进程
        userinit();          // first user process
        __sync_synchronize();
        started = 1;
    } else {
        while (started == 0)
            ;
        __sync_synchronize();
        printf("hart %d starting\n", cpuid());
        kvminithart();   // turn on paging
        trapinithart();  // install kernel trap vector
        plicinithart();  // ask PLIC for device interrupts
    }

    scheduler();
}
