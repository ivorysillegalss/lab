#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

// 需要遍历该目录下的所有文件以及文件夹
// 对于文件夹需要递归遍历其下的所有文件
// 每一个文件有自己的fd 通过fd获取mtdata

void find(char* path, char* target) {
    char buf[512], *p;
    // dirent 提供目录项的基本信息
    // stat 提供文件的详细信息
    struct stat st;
    struct dirent de;
    int fd;

    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        exit(1);
    }

    // 获取目标目录的格式并校验
    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        exit(1);
    }

    // 格式化各种路径
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
        printf("find: path too long\n");
        return;
    }

    // 这里是将p指针固定在父路径的尾部 这样子每一次循环的时候 只需要修改p之后的内容 就可实现递归
    strcpy(buf, path);      // 将path复制到变量buf之中
    p = buf + strlen(buf);  // 将指针p指向buf字符串的结尾
    *p++ = '/';             //结尾加上“/” 为之后进行查找时备用

    // 回溯递归校验查找
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        // 校验是否合法 不合法跳过
        if (de.inum == 0) {
            continue;
        }

        // de.name 是子文件的名字 将子文件与父路径进行拼接
        memmove(p, de.name, DIRSIZ);

        // 判断它的类型是文件还是文件夹 假如是文件夹的话 递归查找 是文件的话 直接再次结束判断即可
        if (stat(buf, &st) < 0) {
            printf("find: cannot stat %s\n", buf);
            continue;
        }

        switch (st.type) {
            case T_FILE:
                p[DIRSIZ] = 0;
                // 确保文件路径的结尾是"/0" 保证他是一个合法的字符串
                if (strcontain(buf, target) == 1) {
                    // 合法的话 打印该文件的名字
                    printf("%s\n", buf);
                }

            case T_DIR:
                // 递归进行查找
                find(buf, target);
        }
    }
    close(fd);
}

//  总体的思路应该是先通过一个while循环 进行遍历当前目录
//   对每一个文件都进行一次read 然后fstat 判断它的类型 假如是文件夹 则修改路径 然后直接进行递归即可
int main(int argc, char* argv[]) {

    if (argc != 3) {
        fprintf(2, "Please enter a path and str\n");
        exit(1);
    }

    find(argv[1], argv[2]);

    exit(0);
}