#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 两个进程 父进程先write到 子进程 ping
//         子进程后write到 父进程 pong

int main(int argc, char* argv[]) {
    int p[2];    //as chan
    int buf[2];  //msg buffer

    pipe(p);

    char* pingMsg = "i";
    char* pongMsg = "o";

    if (fork() == 0) {
        // 子进程
        int rr;
        rr = read(p[0], buf, 1);
        if (rr != 1) {
            fprintf(2, "child process read error\n");
            exit(1);
        }

        close(p[0]);
        printf("%d: received ping\n");

        int wr;
        wr = write(p[1], pongMsg, 1);
        if (wr != 1) {
            fprintf(2, "child process write error\n");
            exit(1);
        }

        close(p[1]);
        exit(0);

    } else {
        // 父进程
        int pwr;
        pwr = write(p[1], pingMsg, 1);
        if (pwr != 1) {
            fprintf(2, "parent process write error\n");
            exit(1);
        }

        close(p[1]);

        wait(0);
        // wait系统调用下 等待某个线程的任一子进程结束任务即返回
        // 其中填入的形参本应是一个地址 这个地址将会记录子进程退出时状态
        // 这里填入了0 相当于填入了NULL 不记录子进程的退出状态

        int prr;
        prr = read(p[0], buf, 1);
        if (prr != 1) {
            fprintf(2, "parent process read error\n");
        }

        printf("%d: received pong\n", getpid());
        close(p[0]);
        exit(0);
    }
}