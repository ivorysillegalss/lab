#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void primes(int* fd) {
    // 子进程关闭掉对应的写口 因为子进程不需要写
    close(fd[1]);

    int v;
    int l = read(fd[0], (void*)&v, sizeof(v));
    if (l != sizeof(v)) {
        if (l == 0) {
            // EOF
            exit(0);
        }
        fprintf(2, "Read Error!\n");
        exit(1);
    }
    //   进来到这里就代表是新开了一个筛选器 可以理解成递归
    printf("prime %d\n", v);

    int nextFd[2];
    pipe(nextFd);

    if (fork() == 0) {
        primes(nextFd);

    } else {
        close(nextFd[0]);

        int vv;
        while (read(fd[0], (void*)&vv, sizeof(vv)) == sizeof(vv)) {
            // 判断是不是素数
            if (vv % v != 0) {
                if (write(nextFd[1], (void*)&vv, sizeof(vv)) != sizeof(vv)) {
                    fprintf(2, "Write Error!\n");
                    exit(1);
                }
            }
        }
        close(fd[0]);
        close(nextFd[1]);
        wait(0);
    }

    exit(1);
}

int main(int argc, char* argv[]) {
    int fd[2];
    pipe(fd);
    if (fork() == 0) {
        primes(fd);
    } else {
        // generate线程 将所有数都传进去
        close(fd[0]);
        for (int i = 2; i < 36; i++) {
            if (write(fd[1], (void*)&i, sizeof(i)) != sizeof(i)) {
                fprintf(2, "Write fail!\n");
                exit(1);
            }
        }
        close(fd[1]);
        wait(0);
        // 等待子进程任务结束
    }

    exit(0);
}