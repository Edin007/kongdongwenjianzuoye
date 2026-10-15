/*
 * 空洞文件「断点续传」下载器
 *
 * 基于文件 I/O 的空洞文件（sparse file）演示程序：
 *   - 用 lseek + write 在固定偏移定点写入，制造/填充文件空洞
 *   - 通过命令行传参控制：create / download / write / status / help
 *   - download 支持断点续传：进度（已完成偏移）记录在 <file>.progress，
 *     Ctrl+C（SIGINT）后保存断点，下次运行从断点继续
 *
 * 运行平台：鲁班猫 RK3566（ARM64 Linux）
 * 编译：
 *   gcc -Wall -Wextra -O2 -o hole_download main.c      # 板载原生编译
 *   aarch64-linux-gnu-gcc ... 同上                     # PC 交叉编译
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>

/* 文件最小逻辑大小：128KB */
#define MIN_SIZE        (128LL * 1024LL)
/* 下载默认块大小：4KB */
#define DEFAULT_CHUNK   4096LL
/* 路径缓冲区大小 */
#define PATHBUF         1024

/* Ctrl+C 标志：由信号处理器置位，下载循环检查后保存断点并退出 */
static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* 拼接进度文件路径：<file>.progress */
static void make_progress_path(const char *path, char *out, size_t outsz)
{
    snprintf(out, outsz, "%s.progress", path);
}

/* 读取「已完成偏移」；进度文件不存在时视为 0（从头开始） */
static long long read_progress(const char *prog)
{
    FILE *fp = fopen(prog, "r");
    long long done = 0;
    if (fp) {
        if (fscanf(fp, "%lld", &done) != 1)
            done = 0;
        fclose(fp);
    }
    return done;
}

/* 写入「已完成偏移」 */
static int write_progress(const char *prog, long long done)
{
    FILE *fp = fopen(prog, "w");
    if (!fp) {
        perror("写进度文件失败");
        return -1;
    }
    fprintf(fp, "%lld\n", done);
    fclose(fp);
    return 0;
}

/* 打印用法 */
static void usage(const char *prog)
{
    printf(
        "空洞文件「断点续传」下载器 用法:\n"
        "  %s create  <file> <size>            建 >=128KB 稀疏文件(全洞), 初始化断点=0\n"
        "  %s download <file> [chunk_size]      从断点续传, 分块定点写入填洞; Ctrl+C 保存断点\n"
        "  %s write    <file> <offset> <data>   lseek+write 定点写入(不破坏其他空间)\n"
        "  %s status   <file>                   打印 逻辑大小/实际占用块数/空洞占比\n"
        "  %s help                              显示本帮助\n",
        prog, prog, prog, prog, prog);
}

/* create: 创建 >=128KB 的稀疏文件(全洞), 并把断点初始化为 0 */
static int create_file(const char *path, long long size)
{
    if (size < MIN_SIZE) {
        fprintf(stderr, "错误: 文件大小必须 >= 128KB (%lld 字节)\n", MIN_SIZE);
        return 1;
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open 失败");
        return 1;
    }

    /* 关键: 把偏移跳到 size-1 处再写 1 字节, 中间区域即为空洞 */
    if (lseek(fd, (off_t)(size - 1), SEEK_SET) == (off_t)-1) {
        perror("lseek 失败");
        close(fd);
        return 1;
    }
    char zero = 0;
    if (write(fd, &zero, 1) != 1) {
        perror("write 失败");
        close(fd);
        return 1;
    }
    close(fd);

    /* 初始化断点 = 0 */
    char prog[PATHBUF];
    make_progress_path(path, prog, sizeof(prog));
    if (write_progress(prog, 0) != 0)
        return 1;

    printf("已创建稀疏文件 %s, 逻辑大小 %lld 字节 (%.2f KB), 断点初始化为 0\n",
           path, size, (double)size / 1024.0);
    return 0;
}

/* download: 从断点续传, 分块定点写入填洞; Ctrl+C 保存断点后退出 */
static int download_file(const char *path, long long chunk)
{
    char prog[PATHBUF];
    make_progress_path(path, prog, sizeof(prog));

    long long done = read_progress(prog);   /* 已完成偏移 = 下一次写入起点 */

    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("open 失败(请先 create 创建文件)");
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("fstat 失败");
        close(fd);
        return 1;
    }
    long long total = (long long)st.st_size;

    if (done >= total) {
        printf("文件 %s 已下载完成(断点 %lld == 总大小 %lld)\n", path, done, total);
        close(fd);
        return 0;
    }

    /* 安装 Ctrl+C 处理器 */
    signal(SIGINT, on_sigint);

    unsigned char *buf = malloc((size_t)chunk);
    if (!buf) {
        perror("malloc 失败");
        close(fd);
        return 1;
    }

    printf("开始下载 %s: 总大小 %lld 字节, 从断点 %lld 继续, 块大小 %lld\n",
           path, total, done, chunk);

    while (done < total && !g_stop) {
        long long n = total - done;
        if (n > chunk)
            n = chunk;

        /* 填充确定性字节模式, 模拟下载数据(便于验证定点写入结果) */
        for (long long i = 0; i < n; i++)
            buf[i] = (unsigned char)((done + i) % 256);

        /* 定点写入: lseek 到固定偏移再 write, 不影响其他空间 */
        if (lseek(fd, (off_t)done, SEEK_SET) == (off_t)-1) {
            perror("lseek 失败");
            break;
        }
        ssize_t w = write(fd, buf, (size_t)n);
        if (w < 0) {
            perror("write 失败");
            break;
        }

        done += (long long)w;
        if (write_progress(prog, done) != 0)    /* 每块写完后保存断点 */
            break;

        printf("[下载] 已完成 %lld / %lld 字节 (%.2f%%)\n",
               done, total, (double)done * 100.0 / (double)total);
    }

    free(buf);
    close(fd);

    if (g_stop) {
        printf("收到 Ctrl+C, 断点已保存: %lld 字节, 下次运行 download 将从这里续传\n", done);
        return 0;
    }
    printf("下载完成: %s (%lld 字节)\n", path, total);
    return 0;
}

/* write: lseek+write 定点写入, 不破坏其余空间 */
static int write_at(const char *path, long long off, const char *data)
{
    if (off < 0) {
        fprintf(stderr, "错误: 偏移不能为负\n");
        return 1;
    }

    /* 注意: 不使用 O_TRUNC, 避免清空文件; 先 lseek 再 write 实现定点写入 */
    int fd = open(path, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        perror("open 失败");
        return 1;
    }

    if (lseek(fd, (off_t)off, SEEK_SET) == (off_t)-1) {
        perror("lseek 失败");
        close(fd);
        return 1;
    }
    size_t len = strlen(data);
    ssize_t w = write(fd, data, len);
    close(fd);

    if (w != (ssize_t)len) {
        perror("write 失败");
        return 1;
    }
    printf("已在 %s 偏移 %lld 处定点写入 %zu 字节: \"%s\"\n", path, off, len, data);
    return 0;
}

/* status: 打印 逻辑大小 / 实际占用块数 / 空洞占比 */
static int show_status(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        perror("stat 失败");
        return 1;
    }

    long long logical = (long long)st.st_size;
    long long blocks  = (long long)st.st_blocks;   /* 512 字节块数 */
    long long used    = blocks * 512;              /* 实际占用字节 */
    long long hole    = logical > used ? logical - used : 0;
    double ratio      = logical > 0 ? (double)hole * 100.0 / (double)logical : 0.0;

    printf("文件: %s\n", path);
    printf("逻辑大小:     %lld 字节 (%.2f KB)\n", logical, (double)logical / 1024.0);
    printf("实际占用块数: %lld 块 (实际占用 %lld 字节)\n", blocks, used);
    printf("空洞占比:     %.2f%%\n", ratio);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "create") == 0) {
        if (argc != 4) {
            usage(argv[0]);
            return 1;
        }
        long long size = strtoll(argv[3], NULL, 10);
        return create_file(argv[2], size);
    } else if (strcmp(cmd, "download") == 0) {
        if (argc < 3 || argc > 4) {
            usage(argv[0]);
            return 1;
        }
        long long chunk = DEFAULT_CHUNK;
        if (argc == 4) {
            chunk = strtoll(argv[3], NULL, 10);
            if (chunk <= 0)
                chunk = DEFAULT_CHUNK;
        }
        return download_file(argv[2], chunk);
    } else if (strcmp(cmd, "write") == 0) {
        if (argc != 5) {
            usage(argv[0]);
            return 1;
        }
        long long off = strtoll(argv[3], NULL, 10);
        return write_at(argv[2], off, argv[4]);
    } else if (strcmp(cmd, "status") == 0) {
        if (argc != 3) {
            usage(argv[0]);
            return 1;
        }
        return show_status(argv[2]);
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 ||
               strcmp(cmd, "-h") == 0) {
        usage(argv[0]);
        return 0;
    }

    usage(argv[0]);
    return 1;
}
