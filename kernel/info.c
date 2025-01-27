
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "sysinfo.h"

int ssinfo(uint64 addr) {
    struct sysinfo s;
    struct proc* p = myproc();

    s.freemem = kgetmem();
    s.nproc = getunusedproc();
    if (s.freemem < 0 || s.nproc < 0) {
        return -1;
    }

    if (copyout(p->pagetable, addr, (char*)&s, sizeof(s)) < 0) {
        return -1;
    }
    return 0;
}