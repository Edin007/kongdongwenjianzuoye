/*
 * 第 2 章 · 规范时代 —— dlmalloc 分箱 + 大块 mmap（1990s）
 *
 * 分配器实现见 common/dl_allocator.c；本文件只做演示。
 */
#include <stdio.h>
#include <stdlib.h>

#include "allocator.h"
#include "bench.h"
#include "dl_allocator.h"

static void banner(void) {
    printf(
"==============================================================\n"
" 第 2 章 · 规范时代 —— dlmalloc 分箱 + 大块 mmap（1990s）\n"
"==============================================================\n"
" 1990 年代 Doug Lea 的 dlmalloc 修掉了 ch1 的两个痛点：\n"
"   1) 把空闲块按大小塞进不同 bin（segregated free lists），\n"
"      分配时直取对应箱，把 O(n) 线性扫描降到近似 O(1)；\n"
"   2) 大块(>=128KB)直接 mmap，释放即归还内核，不占 arena。\n"
"\n"
" dlmalloc 是后来 glibc malloc（ptmalloc2）的直接前身。\n"
"==============================================================\n");
}

int main(void) {
    banner();

    allocator *a = allocator_create(&dl_vtable, 4u << 20);
    if (!a) {
        fprintf(stderr, "分配器初始化失败\n");
        return 1;
    }

    printf("\n[演示 1] 分配 100/200/300 字节，释放中间块\n");
    void *p1 = allocator_alloc(a, 100);
    void *p2 = allocator_alloc(a, 200);
    void *p3 = allocator_alloc(a, 300);
    printf("  p1=%p  p2=%p  p3=%p\n", p1, p2, p3);
    allocator_free(a, p2);
    allocator_stats(a);

    printf("\n[演示 2] 分配 256KB 大块 —— 观察它走 mmap、不占 arena\n");
    void *big = allocator_alloc(a, 256 * 1024);
    printf("  big=%p (usable=%zu B)\n", big, allocator_usable(a, big));
    allocator_stats(a);
    allocator_free(a, big);

    printf("\n[演示 3] 统一随机负载\n");
    bench_config cfg = {
        .n_ops = 100000, .max_live = 512,
        .min_size = 8, .max_size = 1024, .seed = 42,
    };
    bench_result r = bench_run(a, &cfg);
    bench_print(a, &r);
    printf("\n[诊断] 负载结束后分配器内部状态：\n");
    allocator_stats(a);

    printf("\n[演示 · 碎片化对比] 交错释放制造 32 个不可合并的小空闲块\n");
    void *frag[64];
    for (int i = 0; i < 64; i++)
        frag[i] = allocator_alloc(a, 128);
    for (int i = 0; i < 64; i += 2)
        allocator_free(a, frag[i]);
    printf("  碎片化后（分配 160B 之前）：\n");
    allocator_stats(a);
    void *probe = allocator_alloc(a, 160);
    printf("  分配 160B 之后（看累计扫描步数增量）：\n");
    allocator_stats(a);
    allocator_free(a, probe);
    for (int i = 1; i < 64; i += 2)
        allocator_free(a, frag[i]);

    allocator_destroy(a);
    return 0;
}
