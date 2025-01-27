# Initial process that execs /init.
# This code runs in user space.

#include "syscall.h"


# 该文件调用的变量都是在内部定义的（写死）
# 认为规定了执行程序init的地址空间 并通过ecall进行系统调用 到对应的程序中去

# exec(init, argv)
.globl start
start:
        la a0, init
        la a1, argv
        li a7, SYS_exec
        ecall

# for(;;) exit();
# 执行失败的话不断循环执行退出的系统调用
exit:
        li a7, SYS_exit
        ecall
        jal exit

# char init[] = "/init\0";
init:
  .string "/init\0"

# char *argv[] = { init, 0 };
.p2align 2
argv:
  .long init
  .long 0
