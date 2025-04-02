#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void trapinit(void) {
    initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void trapinithart(void) {
    w_stvec((uint64)kernelvec);
}

void saving_userregister(struct proc* p) {
    struct sigregister* s = &p->sigregister;
    struct trapframe* t = p->trapframe;
    s->ra = t->ra;
    s->sp = t->sp;
    s->gp = t->gp;
    s->tp = t->tp;
    s->t0 = t->t0;
    s->t1 = t->t1;
    s->t2 = t->t2;
    s->s0 = t->s0;
    s->s1 = t->s1;
    s->a0 = t->a0;
    s->a1 = t->a1;
    s->a2 = t->a2;
    s->a3 = t->a3;
    s->a4 = t->a4;
    s->a5 = t->a5;
    s->a6 = t->a6;
    s->a7 = t->a7;
    s->s2 = t->s2;
    s->s3 = t->s3;
    s->s4 = t->s4;
    s->s5 = t->s5;
    s->s6 = t->s6;
    s->s7 = t->s7;
    s->s8 = t->s8;
    s->s9 = t->s9;
    s->s10 = t->s10;
    s->s11 = t->s11;
    s->t3 = t->t3;
    s->t4 = t->t4;
    s->t5 = t->t5;
    s->t6 = t->t6;
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S.
//
// 每个中断发生 都会调用该函数. 通过which_dev变量来判断中断的类型.
// 将程序调用由用户空间过渡到内核空间.
// 陷入处理器（中断 异常 系统调用）.
// 本身是写在trapframe当中 在内核空间中调用时 读取trapframe中的地址 jump到此处执行方法.
void usertrap(void) {
    int which_dev = 0;

    // 判断状态是否合法 （当前权限模式）
    if ((r_sstatus() & SSTATUS_SPP) != 0)
        panic("usertrap: not from user mode");

    // send interrupts and exceptions to kerneltrap(),
    // since we're now in the kernel.
    // 此处为内核态 发送对应的中断异常
    w_stvec((uint64)kernelvec);

    struct proc* p = myproc();

    // save user program counter.
    // 保存用户空间当前执行的进度（栈顶）
    p->trapframe->epc = r_sepc();

    if (r_scause() == 8) {
        // system call

        if (p->killed)
            exit(-1);

        // sepc points to the ecall instruction,
        // but we want to return to the next instruction.
        // 此处标识成功执行 向前移动一个语句 加4
        // （4是一个指令的长度）
        p->trapframe->epc += 4;

        // an interrupt will change sstatus &c registers,
        // so don't enable until done with those registers.
        // 进入内核态的时候 硬件会自动全局禁用中断 防止内核中中断的嵌套 直至此处人为启动中断 才可以重新开始中断
        intr_on();

        // 真正执行系统调用
        syscall();
    } else if ((which_dev = devintr()) != 0) {
        // ok
    } else if (r_scause() == 13) {
        // COW页面错误 在此分配新页面

        uint64 err_vaddr = PGROUNDDOWN(r_stval());
        // pagetable_t up_pte = p->pagetable;
        pte_t* pte = walkpte(p->pagetable, err_vaddr);

        if (*p->pagetable & PTE_C) {
            uint64 err_paddr = PTE2PA((uint64)*pte);
            if (err_paddr == 0) {
                printf("usertrap : err_vaddr: %p\n", err_vaddr);
                panic("usertrap: fail to walkaddr! \n");
            }

            // 分配新页面 并且分配PTE_W位
            char* page = kalloc();
            if (page == 0) {
                // 没内存kill掉当前线程
                printf("usertrap(): Fail to allocate physical page.\n");
                p->killed = 1;
            } else {
                uvmunmap(p->pagetable, err_vaddr, PGSIZE, 1);
                memmove((char*)page, (char*)err_paddr, PGSIZE);
                uint64 flags = ((PTE_FLAGS((uint64)(*pte)) | PTE_W) & (~PTE_C));
                *pte = PA2PTE((uint64)page) | flags;
            }
        }

    } else {
        printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
        printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
        p->killed = 1;
    }

    if (p->killed)
        exit(-1);

    // give up the CPU if this is a timer interrupt.
    // 定时片花完了 主动让出CPU
    if ((which_dev == 2) && (p->inalarm == 0)) {
        struct sigcontext* sigctx = p->sigcontext;
        if (p->sigcontext != 0) {
            sigctx->ticks += 1;

            // 判断是否满足回调条件
            if (sigctx->alramtick != 0 && sigctx->ticks &&
                sigctx->ticks == sigctx->alramtick) {
                // 进入alarm函数内 清空计数器
                p->inalarm = 1;
                sigctx->ticks = 0;
                // 保存寄存器中函数状态
                saving_userregister(p);
                // 存储epc的值
                p->epc = p->trapframe->epc;
                // 修改 执行alarm函数
                p->trapframe->epc = sigctx->handler;
            }
        }
        yield();
    }
    usertrapret();
}

//
// return to user space
//
// 返回用户空间时的各种配置 恢复现场 数据修改 等等
void usertrapret(void) {
    struct proc* p = myproc();

    // we're about to switch the destination of traps from
    // kerneltrap() to usertrap(), so turn off interrupts until
    // we're back in user space, where usertrap() is correct.
    intr_off();

    // send syscalls, interrupts, and exceptions to trampoline.S
    w_stvec(TRAMPOLINE + (uservec - trampoline));

    // set up trapframe values that uservec will need when
    // the process next re-enters the kernel.
    p->trapframe->kernel_satp = r_satp();          // kernel page table
    p->trapframe->kernel_sp = p->kstack + PGSIZE;  // process's kernel stack
    p->trapframe->kernel_trap = (uint64)usertrap;
    p->trapframe->kernel_hartid = r_tp();  // hartid for cpuid()

    // set up the registers that trampoline.S's sret will use
    // to get to user space.

    // set S Previous Privilege mode to User.
    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP;  // clear SPP to 0 for user mode
    x |= SSTATUS_SPIE;  // enable interrupts in user mode
    w_sstatus(x);

    // set S Exception Program Counter to the saved user pc.
    w_sepc(p->trapframe->epc);

    // tell trampoline.S the user page table to switch to.
    uint64 satp = MAKE_SATP(p->pagetable);

    // jump to trampoline.S at the top of memory, which
    // switches to the user page table, restores user registers,
    // and switches to user mode with sret.
    // 这里指的是 以TRAMPOLINE为基地址的 对应函数起始地址的便宜量（具体对应trampoline.S中的代码位置）
    uint64 fn = TRAMPOLINE + (userret - trampoline);
    // 将对应的指针转为函数 传入TRAPFRAME的位置和satp寄存器值进去
    ((void (*)(uint64, uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
// 陷入内核空间
void kerneltrap() {
    int which_dev = 0;
    uint64 sepc = r_sepc();
    uint64 sstatus = r_sstatus();
    uint64 scause = r_scause();

    if ((sstatus & SSTATUS_SPP) == 0)
        panic("kerneltrap: not from supervisor mode");
    if (intr_get() != 0)
        panic("kerneltrap: interrupts enabled");

    if ((which_dev = devintr()) == 0) {
        printf("scause %p\n", scause);
        printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
        panic("kerneltrap");
    }

    // give up the CPU if this is a timer interrupt.
    if (which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
        yield();

    // the yield() may have caused some traps to occur,
    // so restore trap registers for use by kernelvec.S's sepc instruction.
    w_sepc(sepc);
    w_sstatus(sstatus);
}

void clockintr() {
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
// 内核中断
int devintr() {
    uint64 scause = r_scause();

    if ((scause & 0x8000000000000000L) && (scause & 0xff) == 9) {
        // this is a supervisor external interrupt, via PLIC.

        // irq indicates which device interrupted.
        int irq = plic_claim();

        if (irq == UART0_IRQ) {
            // 如果是输入字符流 uart进行读取
            uartintr();
        } else if (irq == VIRTIO0_IRQ) {
            virtio_disk_intr();
        } else if (irq) {
            printf("unexpected interrupt irq=%d\n", irq);
        }

        // the PLIC allows each device to raise at most one
        // interrupt at a time; tell the PLIC the device is
        // now allowed to interrupt again.
        if (irq)
            plic_complete(irq);

        return 1;
    } else if (scause == 0x8000000000000001L) {
        // software interrupt from a machine-mode timer interrupt,
        // forwarded by timervec in kernelvec.S.

        if (cpuid() == 0) {
            clockintr();
        }

        // acknowledge the software interrupt by clearing
        // the SSIP bit in sip.
        w_sip(r_sip() & ~2);

        return 2;
    } else {
        return 0;
    }
}
