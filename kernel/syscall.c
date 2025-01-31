#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

#define LAB_PGTBL

// Fetch the uint64 at addr from the current process.
int fetchaddr(uint64 addr, uint64* ip) {
    struct proc* p = myproc();
    if (addr >= p->sz || addr + sizeof(uint64) > p->sz)
        return -1;
    if (copyin(p->pagetable, (char*)ip, addr, sizeof(*ip)) != 0)
        return -1;
    return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int fetchstr(uint64 addr, char* buf, int max) {
    struct proc* p = myproc();
    int err = copyinstr(p->pagetable, buf, addr, max);
    if (err < 0)
        return err;
    return strlen(buf);
}

// argraw方法 获取系统调用的参数 并通过下方的各种功能性方法
// 转换获取到的参数类型
static uint64 argraw(int n) {
    struct proc* p = myproc();
    switch (n) {
        case 0:
            return p->trapframe->a0;
        case 1:
            return p->trapframe->a1;
        case 2:
            return p->trapframe->a2;
        case 3:
            return p->trapframe->a3;
        case 4:
            return p->trapframe->a4;
        case 5:
            return p->trapframe->a5;
    }
    panic("argraw");
    return -1;
}

// Fetch the nth 32-bit system call argument.
int argint(int n, int* ip) {
    *ip = argraw(n);
    return 0;
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
int argaddr(int n, uint64* ip) {
    *ip = argraw(n);
    return 0;
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int argstr(int n, char* buf, int max) {
    uint64 addr;
    if (argaddr(n, &addr) < 0)
        return -1;
    return fetchstr(addr, buf, max);
}

// extern可以类比成外部链接 声明函数的实现可以在外部进行实现 此处仅作声明.
// 具体实现见sysproc.c.
extern uint64 sys_chdir(void);
extern uint64 sys_close(void);
extern uint64 sys_dup(void);
extern uint64 sys_exec(void);
extern uint64 sys_exit(void);
extern uint64 sys_fork(void);
extern uint64 sys_fstat(void);
extern uint64 sys_getpid(void);
extern uint64 sys_kill(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_mknod(void);
extern uint64 sys_open(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_unlink(void);
extern uint64 sys_wait(void);
extern uint64 sys_write(void);
extern uint64 sys_uptime(void);
extern uint64 sys_trace(void);
extern uint64 sys_info(void);
#ifdef LAB_NET
extern uint64 sys_connect(void);
#endif
extern uint64 sys_pgaccess(void);

static uint64 (*syscalls[])(void) = {
    [SYS_fork] sys_fork,         [SYS_exit] sys_exit,
    [SYS_wait] sys_wait,         [SYS_pipe] sys_pipe,
    [SYS_read] sys_read,         [SYS_kill] sys_kill,
    [SYS_exec] sys_exec,         [SYS_fstat] sys_fstat,
    [SYS_chdir] sys_chdir,       [SYS_dup] sys_dup,
    [SYS_getpid] sys_getpid,     [SYS_sbrk] sys_sbrk,
    [SYS_sleep] sys_sleep,       [SYS_uptime] sys_uptime,
    [SYS_open] sys_open,         [SYS_write] sys_write,
    [SYS_mknod] sys_mknod,       [SYS_unlink] sys_unlink,
    [SYS_link] sys_link,         [SYS_mkdir] sys_mkdir,
    [SYS_close] sys_close,       [SYS_trace] sys_trace,
    [SYS_sysinfo] sys_info,
#ifdef LAB_NET
    [SYS_connect] sys_connect,
#endif

    [SYS_pgaccess] sys_pgaccess,
};

static char* syscall_names[] = {
    // syscall调用编号从1开始 这里留空是为了方便打印输出
    "",       "fork",  "exit",   "wait",  "pipe",  "read",
    "kill",   "exec",  "fstat",  "chdir", "dup",   "getpid",
    "sbrk",   "sleep", "uptime", "open",  "write", "mknod",
    "unlink", "link",  "mkdir",  "close", "trace", "sysinfo"};

void syscall(void) {
    int num;
    struct proc* p = myproc();

    // a7寄存器保存系统调用的编号 a0保存返回值与调用的函数
    num = p->trapframe->a7;
    if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
        uint64 retV = syscalls[num]();
        p->trapframe->a0 = retV;

        if (p->trace_mask & (1 << num)) {
            printf("%d: syscall %s -> %d\n", p->pid, syscall_names[num], retV);
        }

    } else {
        printf("%d %s: unknown sys call %d\n", p->pid, p->name, num);
        p->trapframe->a0 = -1;
    }
}
