#ifndef BENCH_H
#define BENCH_H

#include "allocator.h"
#include <stddef.h>
#include <stdio.h>

/*
 * 统一 benchmark —— "进化"的量尺。
 *
 * 同一个随机负载跑过每一代 allocator（以及真实 glibc），
 * 输出吞吐、峰值存活、总申请字节，让"这一代比上一代好在哪"可量化。
 */

typedef struct {
    size_t  n_ops;              /* 总操作数（malloc/free 各占约一半） */
    size_t  max_live;           /* 槽位数，决定同时存活的块数上限 */
    size_t  min_size;           /* 随机分配大小下限（字节） */
    size_t  max_size;           /* 随机分配大小上限（字节） */
    unsigned seed;              /* 固定种子保证各代可复现对比 */
} bench_config;

typedef struct {
    double  elapsed_sec;        /* 耗时 */
    size_t  alloc_calls;        /* 成功 malloc 次数 */
    size_t  free_calls;         /* free 次数 */
    size_t  total_alloc_bytes;  /* 累计请求字节 */
    size_t  peak_live_bytes;    /* 峰值同时存活字节 */
} bench_result;

/* 随机分配/释放负载，模拟"对象池/服务端请求"的生命周期 */
bench_result bench_run(allocator *a, const bench_config *cfg);
void         bench_print(const allocator *a, const bench_result *r);

/* CSV 输出，供 tools/plot.py 画图（字段契约见 tools/plot.py 头注释） */
void         bench_csv_header(FILE *out);
void         bench_csv_row(FILE *out, const allocator *a, const bench_result *r);

#endif /* BENCH_H */
