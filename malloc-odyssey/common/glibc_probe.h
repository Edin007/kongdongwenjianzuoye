#ifndef GLIBC_PROBE_H
#define GLIBC_PROBE_H

#include <stddef.h>

/*
 * 真实 glibc 观测层 —— 解剖"现代 malloc"长什么样。
 *
 * 自实现的 mini allocator 讲"过去"，这里讲"现在"：
 * 通过 mallinfo2() 与 /proc/self/maps 抓取当前进程堆的
 * arena / 已用 / 空闲 / mmap 大块 / [heap] 段大小。
 */

typedef struct {
    int     have_mallinfo;   /* 平台是否提供 mallinfo2/mallinfo */
    size_t  arena_bytes;     /* 已分配的 arena 总量 */
    size_t  inuse_bytes;     /* 已用字节（含 chunk 元数据） */
    size_t  free_bytes;      /* 空闲字节 */
    size_t  free_blocks;     /* 空闲块数（可近似反映外部碎片） */
    size_t  mmap_blocks;     /* 由 mmap 单独服务的大块数量 */
    size_t  heap_vm_bytes;   /* [heap] 段虚拟大小（/proc/self/maps） */
} glibc_heap_snapshot;

glibc_heap_snapshot glibc_probe(void);
void glibc_probe_print(const glibc_heap_snapshot *s);
void glibc_probe_print_maps(void);   /* 打印 /proc/self/maps 关键段 */

#endif /* GLIBC_PROBE_H */
