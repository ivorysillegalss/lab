#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S needs one stack per CPU.
// 对于启动的挨个CPU 分别设定值
__attribute__((aligned(16))) char stack0[4096 * NCPU];

// a scratch area per CPU for machine-mode timer interrupts.
uint64 timer_scratch[NCPU][5];

// assembly code in kernelvec.S for machine-mode timer interrupt.
// 时间片中断实现机制 实现一个调度器 
// 可以在文件中看到 机器模式相关的定时器中断时主动让出线程的
extern void timervec();

// entry.S jumps here in machine mode on stack0.
void start() {
    // 设置MPP值 MPP是其中记录上一个权限模式的字段 决定mret时ret的模式
    // 这里手动对他进行设置 设置当前的模式是machine mode 返回的模式则是kernel mode
    // set M Previous Privilege mode to Supervisor, for mret.
    unsigned long x = r_mstatus(); // 取值
    x &= ~MSTATUS_MPP_MASK; // 清除对应字段中的值
    x |= MSTATUS_MPP_S; // 设置为Supervisor即kernel模式为MPP中的值
    w_mstatus(x);  // 写回

    // set M Exception Program Counter to main, for mret.
    // requires gcc -mcmodel=medany

    // mepc寄存器是机器模式返回时的目标地址 
    // 这里可以理解为在机器模式中 发横中断之后 会以kernel(supervisor) mode返回到 哪一个位置
    w_mepc((uint64)main);

    // disable paging for now.
    // satp寄存器为定制是否启用虚拟地址转换
    // 禁用页表映射 暂时逻辑地址对应的就是真实的物理地址
    w_satp(0);

    // 下方有一些是0xffff类似的 这本质上是1 代表将权限设置为内核模式kernel / supervisor进行管理

    // delegate all interrupts and exceptions to supervisor mode.
    // 将中断和异常的寄存器内容委托给内核
    w_medeleg(0xffff);  // 异常寄存器
    w_mideleg(0xffff);  // 中断寄存器
    w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE); // 启用supervisor模式下的各种中断

    // configure Physical Memory Protection to give supervisor mode
    // access to all of physical memory.
    // 启用；并配置物理内存保护 配置为supervisor模式下可以访问整个物理内存
    w_pmpaddr0(0x3fffffffffffffull);
    w_pmpcfg0(0xf);

    // ask for clock interrupts.
    timerinit();

    // keep each CPU's hartid in its tp register, for cpuid().
    // 分配CPU自己本身的标识id
    int id = r_mhartid();
    w_tp(id);

    // switch to supervisor mode and jump to main().
    asm volatile("mret");
}

// set up to receive timer interrupts in machine mode,
// which arrive at timervec in kernelvec.S,
// which turns them into software interrupts for
// devintr() in trap.c.
void timerinit() {
    // each CPU has a separate source of timer interrupts.
    // CPU为颗粒度 获取ID
    int id = r_mhartid();

    // ask the CLINT for a timer interrupt.
    int interval = 1000000;  // cycles; about 1/10th second in qemu.
    // 将MTIMECMP寄存器 设定为当前时间加上时间间隔 当到达这个时间的时候 就会自动触发
    *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + interval;


    // 配置当中断发生的时候 到底需要保存哪一些信息
    // prepare information in scratch[] for timervec.
    // scratch[0..2] : space for timervec to save registers.
    // scratch[3] : address of CLINT MTIMECMP register.
    // scratch[4] : desired interval (in cycles) between timer interrupts.
    uint64* scratch = &timer_scratch[id][0];
    scratch[3] = CLINT_MTIMECMP(id);
    scratch[4] = interval;
    w_mscratch((uint64)scratch);

    // set the machine-mode trap handler.
    // 注册中断时 执行的函数
    // mtvec寄存器存储中断向量的地址 这里将主动让出线程的函数注册了上去
    // 就代表发生中断的时候 自己让出线程
    w_mtvec((uint64)timervec);

    // enable machine-mode interrupts.
    w_mstatus(r_mstatus() | MSTATUS_MIE);

    // enable machine-mode timer interrupts.
    w_mie(r_mie() | MIE_MTIE);
}
