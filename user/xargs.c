
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"
#define NULL ((void*)0)

// 本质上是将标准输入流的数据 通过换行符对每一行进行标识
// 对于每一行都分别执行xarg后的命令

void xargs(char* func, char* cmd[]) {

    // fork子进程exec执行命令 父进程wait获取执行结果并返回
    if (fork() == 0) {
        if (exec(func, cmd) < 0) {
            fprintf(2, "xargs: exec %s failed!\n", func);
            exit(1);
        }
    } else {
        wait(0);
    }
}

int main(int argc, char* argv[]) {

    if (argc < 2) {
        fprintf(2, "xargs: enter args\n");
        exit(1);
    }

    char buf[512], *p;
    p = buf;
    // 装配执行参数
    char* cmd[MAXARG];
    char* func = argv[1];

    int i = 1;
    int cmd_args = 0;
    while (i < argc) {
        cmd[cmd_args++] = argv[i++];
    }

    // 从输入流中获取数据 read
    char c;
    while (read(0, &c, 1)) {
        if (c != '\n') {
            // 动态添加
            *p++ = c;

            // p为当前操作的字符串的末尾 buf则为开头 两者相减为目前操作的字符串的真实长度
            if (p - buf >= sizeof(buf)) {
                fprintf(2, "xargs: input too lang\n");
                exit(1);
            }

        } else if (c == '\n') {
            *p = '\0';
            // 组装运行参数
            cmd[cmd_args] = buf;
            cmd[cmd_args + 1] = 0;

            // 执行
            xargs(func, cmd);

            // 恢复现场
            p = buf;
        }
    }

    // 处理最后一行
    if (p != buf) {
        *p = '\0';
        cmd[cmd_args] = buf;
        cmd[cmd_args + 1] = 0;
        xargs(func, cmd);
    }

    exit(0);
}