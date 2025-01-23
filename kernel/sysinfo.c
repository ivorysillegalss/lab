#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64 sys_info(void) {
    uint64 info;
    if (argaddr(0, &info) < 0) {
        return -1;
    }
    int iaddr = ssinfo(info);
    return iaddr;
}
