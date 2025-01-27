// init: The initial user-level program

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"
#include "kernel/fcntl.h"

char* argv[] = {"sh", 0};

// 通过initCode.S文件exec调用
// 将第一个用户空间的进程 exec成为sh
int main(void) {
    int pid, wpid;

    // 这里的console是提前已经通过consoleinit()方法提前进行定义了的
    if (open("console", O_RDWR) < 0) {
        mknod("console", CONSOLE, 0);
        open("console", O_RDWR);
    }
    dup(0);  // stdout
    dup(0);  // stderr

    for (;;) {
        printf("init: starting sh\n");
        pid = fork();
        if (pid < 0) {
            // 父进程fork失败
            printf("init: fork failed\n");
            exit(1);
        }
        if (pid == 0) {
            // 新进程exec为sh
            exec("sh", argv);
            printf("init: exec sh failed\n");
            exit(1);
        }

        for (;;) {
            // this call to wait() returns if the shell exits,
            // or if a parentless process exits.
            wpid = wait((int*)0);
            if (wpid == pid) {
                // the shell exited; restart it.
                break;
            } else if (wpid < 0) {
                printf("init: wait returned an error\n");
                exit(1);
            } else {
                // it was a parentless process; do nothing.
            }
        }
    }
}
