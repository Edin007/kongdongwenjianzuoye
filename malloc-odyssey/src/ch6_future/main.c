/*
 * 第 6 章 · 现在与未来 —— 各代汇总对比
 *
 * 把第 1 章（first-fit）、第 2 章（分箱）与现代 glibc 放进
 * 同一套 benchmark，量化看 50 年进化。
 *
 * 用法：
 *   ./build/ch6_future           -> 终端对比表
 *   ./build/ch6_future --csv     -> 输出 CSV（供主机 tools/plot.py 画图）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "allocator.h"
#include "bench.h"
#include "v7_allocator.h"
#include "dl_allocator.h"

/* 把现代 glibc malloc 包装成 alloc_vtable，让"现在"也跑同一套 bench */
static void  *glibc_init(size_t arena) { (void)arena; return (void *)1; }
static void  *glibc_alloc(void *ctx, size_t n) { (void)ctx; return malloc(n); }
static void   glibc_free_(void *ctx, void *p) { (void)ctx; free(p); }
static void  *glibc_realloc_(void *ctx, void *p, size_t n) { (void)ctx; return realloc(p, n); }
static size_t glibc_usable_(void *ctx, void *p) {
    (void)ctx;
#if defined(__GLIBC__)
    extern size_t malloc_usable_size(void *);
    return malloc_usable_size(p);
#else
    (void)p;
    return 0;
#endif
}
static void glibc_stats_(void *ctx) { (void)ctx; }
static void glibc_destroy(void *ctx) { (void)ctx; }

static const alloc_vtable glibc_vtable = {
    .name = "glibc malloc (现代)",
    .era  = "第 6 章 · 现在",
    .init = glibc_init,
    .alloc = glibc_alloc,
    .free = glibc_free_,
    .realloc = glibc_realloc_,
    .usable = glibc_usable_,
    .stats = glibc_stats_,
    .destroy = glibc_destroy,
};

static void banner(void) {
    printf(
"==============================================================\n"
" 第 6 章 · 现在与未来 —— 各代汇总\n"
"==============================================================\n"
" 把第 1 章（first-fit）、第 2 章（分箱）与现代 glibc\n"
" 放进同一套 benchmark，量化看 50 年进化。\n"
"\n"
" 进化主线回顾：\n"
"   V7(1979) 边界标记 + first-fit\n"
"     -> dlmalloc(1990s) 分箱 + 大块 mmap\n"
"     -> ptmalloc2(2003) 多 arena\n"
"     -> tcache(2017) + safe-linking(2020) + ASan/MTE\n"
"\n"
" 画图：./build/ch6_future --csv > results.csv\n"
"       python3 tools/plot.py results.csv\n"
"==============================================================\n");
}

int main(int argc, char **argv) {
    int csv_mode = (argc > 1 && strcmp(argv[1], "--csv") == 0);
    if (!csv_mode)
        banner();

    const alloc_vtable *vts[] = { &v7_vtable, &dl_vtable, &glibc_vtable };
    const int nvt = (int)(sizeof(vts) / sizeof(vts[0]));

    bench_config cfg = {
        .n_ops = 200000, .max_live = 512,
        .min_size = 8, .max_size = 1024, .seed = 42,
    };

    if (csv_mode)
        bench_csv_header(stdout);

    for (int i = 0; i < nvt; i++) {
        allocator *a = allocator_create(vts[i], 4u << 20);
        if (!a) {
            fprintf(stderr, "初始化失败：%s\n", vts[i]->name);
            continue;
        }
        bench_result r = bench_run(a, &cfg);
        if (csv_mode)
            bench_csv_row(stdout, a, &r);
        else
            bench_print(a, &r);
        allocator_destroy(a);
    }
    return 0;
}
