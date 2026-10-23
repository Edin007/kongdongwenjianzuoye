#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>

/*
 * 统一分配器接口 —— 《堆的进化论》核心抽象。
 *
 * 每一代 mini allocator（V7 first-fit / dlmalloc 分箱 / 多 arena / ...）
 * 都实现同一套 vtable。这样"进化"不再是纸面描述，而是：
 *   同一套 benchmark 跑过每一代，量化对比碎片率、耗时、并发扩展性。
 *
 * 每个实现只通过 mem_backend 从内核"借地"（mmap），
 * 绝不调用 libc 的 malloc —— 否则就成了自己观察自己。
 */

typedef struct {
    const char *name;   /* 展示名，如 "V7 first-fit (1979)" */
    const char *era;    /* 所属章节/年代 */

    /* arena_bytes: 该代分配器可支配的最大堆空间（第 1 章之前可能忽略） */
    void  *(*init)(size_t arena_bytes);
    void  *(*alloc)(void *ctx, size_t n);
    void   (*free)(void *ctx, void *p);
    void  *(*realloc)(void *ctx, void *p, size_t n);
    /* 返回 p 实际可用字节（含内部碎片），无 p 或不可用时返回 0 */
    size_t (*usable)(void *ctx, void *p);
    /* 打印：碎片率、空闲块数、总扫描次数等诊断信息 */
    void   (*stats)(void *ctx);
    void   (*destroy)(void *ctx);
} alloc_vtable;

typedef struct {
    const alloc_vtable *vt;
    void  *ctx;
    size_t arena_bytes;
} allocator;

allocator *allocator_create(const alloc_vtable *vt, size_t arena_bytes);
void  *allocator_alloc(allocator *a, size_t n);
void   allocator_free(allocator *a, void *p);
void  *allocator_realloc(allocator *a, void *p, size_t n);
size_t allocator_usable(allocator *a, void *p);
void   allocator_stats(allocator *a);
void   allocator_destroy(allocator *a);

#endif /* ALLOCATOR_H */
