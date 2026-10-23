/*
 * 第 4 章 · 速度时代 —— tcache（glibc 2.26, 2017）
 *
 * glibc 2.26 引入 tcache（thread cache）：每个线程维护 64 个
 * 小对象 bin（16~1032 字节，每 bin 默认缓存 7 块），LIFO 直取、
 * 几乎免锁，让高频小对象分配大幅加速。
 *
 * 同一思路也催生了竞品：jemalloc(2005, Firefox/Rust)、
 * tcmalloc(2005, Google)、mimalloc(2019, 微软)，都是"每线程缓存"。
 *
 * 本演示自动 fork 出两个子进程，对比"tcache 开 vs 关"的吞吐。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static double bench_single(size_t size, int iters) {
    double t0 = now_sec();
    for (int i = 0; i < iters; i++) {
        void *p = malloc(size);
        free(p);
    }
    return now_sec() - t0;
}

static void run_benchmarks(void) {
    int sizes[] = {16, 32, 64};
    for (int s = 0; s < 3; s++) {
        int iters = 2000000;
        double t = bench_single((size_t)sizes[s], iters);
        printf("  malloc/free %3d B x %d 次 : %6.3f s  (%8.0f ops/s)\n",
               sizes[s], iters, t, (double)iters / t);
    }
}

static void run_child(int disable_tcache) {
    pid_t pid = fork();
    if (pid < 0)
        return;
    if (pid == 0) {
        if (disable_tcache)
            setenv("GLIBC_TUNABLES", "glibc.malloc.tcache_count=0", 1);
        char exe[4096];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n > 0)
            exe[n] = 0;
        char *argv[] = { exe, (char *)"--run", NULL };
        execv(exe, argv);
        _exit(1);
    }
    waitpid(pid, NULL, 0);
}

static void banner(void) {
    printf(
"==============================================================\n"
" 第 4 章 · 速度时代 —— tcache（glibc 2.26, 2017）\n"
"==============================================================\n"
" 2017 年 glibc 2.26 引入 tcache（thread cache）：\n"
" 每个线程维护 64 个小对象 bin（16~1032 字节），\n"
" 每 bin 默认缓存 7 块，LIFO 直取、几乎免锁。\n"
" 这让高频小对象分配大幅加速。\n"
"\n"
" 同一思路也催生了竞品：\n"
"   jemalloc(2005, Firefox/Rust)、tcmalloc(2005, Google)、\n"
"   mimalloc(2019, 微软) —— 都是\"每线程缓存\"这条路线。\n"
"\n"
" 本演示自动 fork 两个子进程，对比 tcache 开 vs 关。\n"
"==============================================================\n");
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--run") == 0) {
        run_benchmarks();
        return 0;
    }

    banner();
    printf("\n[对比 1] tcache 开启（glibc 默认）：\n");
    run_child(0);
    printf("\n[对比 2] tcache 禁用（glibc.malloc.tcache_count=0）：\n");
    run_child(1);
    printf("\n（若两组吞吐接近，说明你的 glibc 未识别该 tunable，属正常）\n");
    return 0;
}
