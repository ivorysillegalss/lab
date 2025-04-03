//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

#define BACKSPACE 0x100
// 指将对应字符修改为控制字符 如'a' 修改为'ctrl + a'
#define C(x) ((x) - '@')  // Control-x

//
// send one character to the uart.
// called by printf, and to echo input characters,
// but not from write().
//
void consputc(int c) {
    if (c == BACKSPACE) {
        // if the user typed backspace, overwrite with a space.
        uartputc_sync('\b');
        uartputc_sync(' ');
        uartputc_sync('\b');
    } else {
        uartputc_sync(c);
    }
}

struct {
    struct spinlock lock;

    // input
#define INPUT_BUF 128
    char buf[INPUT_BUF];
    uint r;  // Read index
    uint w;  // Write index
    uint e;  // Edit index
} cons;

//
// user write()s to the console go here.
//
// 往控制台输出字符
int consolewrite(int user_src, uint64 src, int n) {
    int i;

    for (i = 0; i < n; i++) {
        char c;
        if (either_copyin(&c, user_src, src + i, 1) == -1)
            break;
        // 调用该方法进行字符输出
        uartputc(c);
    }

    return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dist indicates whether dst is a user
// or kernel address.
//
int consoleread(int user_dst, uint64 dst, int n) {
    uint target;
    int c;
    char cbuf;

    target = n;
    // 上锁
    acquire(&cons.lock);
    while (n > 0) {
        // wait until interrupt handler has put some
        // input into cons.buffer.
        // 等待直至缓冲区中有值 （当read偏移量等于write偏移量的时候 代表缓冲区为空）
        while (cons.r == cons.w) {
            if (myproc()->killed) {
                // 进程被杀 放锁
                release(&cons.lock);
                return -1;
            }
            // 阻塞 直到在consoleintr()函数中获取到单行字符 将此wakeup
            sleep(&cons.r, &cons.lock);
        }

        // 更新读取的索引 因为上方的循环是轮询查偏移量
        c = cons.buf[cons.r++ % INPUT_BUF];

        // 判断是否ctrl + D
        if (c == C('D')) {  // end-of-file
            // 如果读取的字节数小于预期 消除此次读取行为偏移量
            if (n < target) {
                // Save ^D for next time, to make sure
                // caller gets a 0-byte result.
                cons.r--;
            }
            // 结束读取循环
            break;
        }

        // copy the input byte to the user-space buffer.
        // 复制到用户空间的缓冲区
        cbuf = c;
        if (either_copyout(user_dst, dst, &cbuf, 1) == -1)
            break;

        // dst指用户空间对应的地址
        dst++;
        --n;

        // 按一行行进行读取 如果读到换行符标识此次读取结束 跳转回用户空间调用处
        if (c == '\n') {
            // a whole line has arrived, return to
            // the user-level read().
            break;
        }
    }
    release(&cons.lock);

    return target - n;
}

//
// the console input interrupt handler.
// uartintr() calls this for input character.
// do erase/kill processing, append to cons.buf,
// wake up consoleread() if a whole line has arrived.
//
// 控制台中断处理函数 根据触发中断的字符判断
void consoleintr(int c) {
    acquire(&cons.lock);

    switch (c) {
        case C('P'):  // Print process list.
            procdump();
            break;
        case C('U'):  // Kill line.
            while (cons.e != cons.w &&
                   cons.buf[(cons.e - 1) % INPUT_BUF] != '\n') {
                cons.e--;
                consputc(BACKSPACE);
            }
            break;
        case C('H'):  // Backspace
        case '\x7f':
            if (cons.e != cons.w) {
                cons.e--;
                consputc(BACKSPACE);
            }
            break;
        default:
            if (c != 0 && cons.e - cons.r < INPUT_BUF) {
                c = (c == '\r') ? '\n' : c;

                // echo back to the user.
                consputc(c);

                // store for consumption by consoleread().
                cons.buf[cons.e++ % INPUT_BUF] = c;

                if (c == '\n' || c == C('D') || cons.e == cons.r + INPUT_BUF) {
                    // wake up consoleread() if a whole line (or end-of-file)
                    // has arrived.
                    // 读取到当前行末尾 或 文件末尾 唤醒r偏移量
                    cons.w = cons.e;
                    // 上方consoleread中会通过sleep阻塞住 此处松开这个锁
                    // TODO 本质上是单线程内的多程序执行时的 上下文切换调度实现两者间跳转
                    wakeup(&cons.r);
                }
            }
            break;
    }

    release(&cons.lock);
}

void consoleinit(void) {
    // 初始化锁
    initlock(&cons.lock, "cons");

    // 配置硬件参数
    uartinit();

    // connect read and write system calls
    // to consoleread and consolewrite.
    // 注册读写系统调用用到的方法
    devsw[CONSOLE].read = consoleread;
    devsw[CONSOLE].write = consolewrite;
}
