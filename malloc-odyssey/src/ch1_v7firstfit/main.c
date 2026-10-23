/*
 * 第 1 章 · 蛮荒时代 —— V7 first-fit 自由链表（1979）
 *
 * 分配器实现见 common/v7_allocator.c；本文件只做演示。
 */
#include <stdio.h>
#include <stdlib.h>

#include "allocator.h"
#include "bench.h"
#include "v7_allocator.h"

static void banner(void) {
    printf(
"==============================================================\n"
" 第 1 章 · 蛮荒时代 —— V7 first-fit（1979）\n"
"==============================================================\n"
" 1979 年第七版 Unix 带来了 K&R 书里那个经典 malloc：\n"
" 用边界标记(boundary tags)在每个块头尾记录大小，\n"
" 用一条自由链表串起所有空闲块，分配时从头找第一块\n"
" 够大的 —— 这就是 first-fit。\n"
"\n"
" 它有两个致命缺点，本演示会亲手看到：\n"
"   1) first-fit 要线性扫链表，块越多越慢（O(n)）；\n"
"   2) 小空闲块散落各处，大请求可能明明有空闲却装不下。\n"
"==============================================================\n");
}

int main(void) {
    banner();

    allocator *a = allocator_create(&v7_vtable, 4u << 20);
    if (!a) {
        fprintf(stderr, "分配器初始化失败\n");
        return 1;
    }

    printf("\n[演示 1] 分配 100/200/300 字节，释放中间块，观察切分与合并\n");
    void *p1 = allocator_alloc(a, 100);
    void *p2 = allocator_alloc(a, 200);
    void *p3 = allocator_alloc(a, 300);
    printf("  p1=%p  p2=%p  p3=%p\n", p1, p2, p3);
    printf("  释放 p2 ...\n");
    allocator_free(a, p2);
    allocator_stats(a);

    printf("\n[演示 2] 统一随机负载（对象池式分配/释放）\n");
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
