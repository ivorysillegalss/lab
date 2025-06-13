//
// simple PCI-Express initialization, only
// works for qemu and its e1000 card.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// 设备驱动的初始化函数
void pci_init() {
    // 指定网卡一会的内存映射区域
    // we'll place the e1000 registers at this address.
    // vm.c maps this range.
    uint64 e1000_regs = 0x40000000L;

    // qemu -machine virt puts PCIe config space here.
    // vm.c maps this range.
    uint32* ecam = (uint32*)0x30000000L;

    // look at each possible PCI device on bus 0.
    for (int dev = 0; dev < 32; dev++) {
        int bus = 0;
        int func = 0;
        int offset = 0;
        uint32 off = (bus << 16) | (dev << 11) | (func << 8) | (offset);
        volatile uint32* base = ecam + off;
        uint32 id = base[0];

        // 如果遍历的时候找到了 网卡的驱动 执行对应的初始化函数
        // 这里是硬编码了 对应的pcie id的位置 实际的操作系统将会是动态分配等
        // 100e:8086 is an e1000
        if (id == 0x100e8086) {
            // 表示e1000的状态寄存器 之类是启用内存映射
            // command and status register.
            // bit 0 : I/O access enable
            // bit 1 : memory access enable
            // bit 2 : enable mastering
            base[1] = 7;
            __sync_synchronize();

            for (int i = 0; i < 6; i++) {
                uint32 old = base[4 + i];

                // bar ---- Base Adderss Register
                // 寄存器分配空间的概念～
                // writing all 1's to the BAR causes it to be
                // replaced with its size.
                // 向这个硬件写入ffff 就能返回对应的bar大小 （硬件特性）
                base[4 + i] = 0xffffffff;
                __sync_synchronize();

                base[4 + i] = old;
            }

            // 将e1000点bar0指向 内存中映射好的区域
            // tell the e1000 to reveal its registers at
            // physical address 0x40000000.
            base[4 + 0] = e1000_regs;

            e1000_init((uint32*)e1000_regs);
        }
    }
}
