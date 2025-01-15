#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

int strcontain(const char* p, const char* q) {
    // const char *p_start = p;    // 保留 p 的起始位置
    while (*p) {  // 遍历字符串 p 的每个字符
        const char* p_temp = p;
        const char* q_temp = q;

        while (*q_temp && *p_temp && *q_temp == *p_temp) {
            q_temp++;
            p_temp++;
        }

        if (*q_temp == '\0') {  // 如果 q 全部匹配完，说明找到子串
            return 1;
        }
        p++;  // 移动到 p 的下一个位置
    }
    return 0;  // 如果遍历完未找到，返回 0
}

// 需要遍历该目录下的所有文件以及文件夹
// 对于文件夹需要递归遍历其下的所有文件
// 每一个文件有自己的fd 通过fd获取mtdata

char* target;

void find(char* path) {
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

    // 判断是否文件夹
    if (st.type != T_DIR) {
        fprintf(2, "find: %s is not a directory\n", path);
        close(fd);
        return;
    }

    // 格式化各种路径
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
        printf("find: path too long\n");
        close(fd);
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

        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
            continue;

        // de.name 是子文件的名字 将子文件与父路径进行拼接
        memmove(p, de.name, DIRSIZ);
        // 确保路径的结尾是"/0" 保证他是一个合法的字符串
        p[DIRSIZ] = 0;

        // 判断它的类型是文件还是文件夹 假如是文件夹的话 递归查找 是文件的话 直接再次结束判断即可
        if (stat(buf, &st) < 0) {
            printf("find: cannot stat %s\n", buf);
            continue;
        }

        switch (st.type) {
            case T_FILE:
                if (strcontain(de.name, target) == 1) {
                    // 合法的话 打印该文件的名字
                    printf("%s\n", buf);
                }
                break;
            case T_DIR:
                // 递归进行查找
                find(buf);
                break;
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

    target = argv[2];
    find(argv[1]);

    exit(0);
}