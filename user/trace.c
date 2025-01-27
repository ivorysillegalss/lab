#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 以追踪的方式启动另外一个程序
// 在用户空间中的方法是 先注册追踪 再执行子程序
// 对于输入的mask 可以一个条件分支
// if注册过 输出锚点信息
// fork处修改
int main(int argc, char* argv[]) {
    int i;
    char* nargv[MAXARG];

    if (argc < 3 || (argv[1][0] < '0' || argv[1][0] > '9')) {
        fprintf(2, "Usage: %s mask command\n", argv[0]);
        exit(1);
    }

    if (trace(atoi(argv[1])) < 0) {
        fprintf(2, "%s: trace failed\n", argv[0]);
        exit(1);
    }

    // 新建参数数组 并且exec执行
    for (i = 2; i < argc && i < MAXARG; i++) {
        nargv[i - 2] = argv[i];
    }
    exec(nargv[0], nargv);
    exit(0);
}
