#include "bench.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <time.h>
  static double now_sec(void) {
      return (double)clock() / (double)CLOCKS_PER_SEC;
  }
#else
  #include <time.h>
  static double now_sec(void) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
  }
#endif

/* 简单 LCG，跨平台确定，保证各代跑的是完全相同的负载序列 */
static size_t rand_range(unsigned *seed, size_t lo, size_t hi) {
    *seed = *seed * 1103515245u + 12345u;
    if (hi <= lo)
        return lo;
    return lo + ((size_t)(*seed) % (hi - lo + 1));
}

bench_result bench_run(allocator *a, const bench_config *cfg) {
    bench_result r;
    memset(&r, 0, sizeof(r));
    if (!a || cfg->max_live == 0 || cfg->n_ops == 0)
        return r;

    void   **ptrs  = (void **)calloc(cfg->max_live, sizeof(void *));
    size_t  *sizes = (size_t *)calloc(cfg->max_live, sizeof(size_t));
    if (!ptrs || !sizes) {
        free(ptrs);
        free(sizes);
        return r;
    }

    unsigned seed = cfg->seed;
    size_t live_bytes = 0;
    double t0 = now_sec();

    for (size_t i = 0; i < cfg->n_ops; i++) {
        size_t idx = rand_range(&seed, 0, cfg->max_live - 1);
        if (ptrs[idx] == NULL) {
            size_t n = rand_range(&seed, cfg->min_size, cfg->max_size);
            void *p = allocator_alloc(a, n);
            if (p) {
                ptrs[idx] = p;
                sizes[idx] = n;
                r.alloc_calls++;
                r.total_alloc_bytes += n;
                live_bytes += n;
                if (live_bytes > r.peak_live_bytes)
                    r.peak_live_bytes = live_bytes;
            }
        } else {
            allocator_free(a, ptrs[idx]);
            ptrs[idx] = NULL;
            live_bytes -= sizes[idx];
            sizes[idx] = 0;
            r.free_calls++;
        }
    }

    /* 收尾：释放全部存活块，避免把清理成本算到别处 */
    for (size_t i = 0; i < cfg->max_live; i++) {
        if (ptrs[i]) {
            allocator_free(a, ptrs[i]);
            r.free_calls++;
        }
    }

    r.elapsed_sec = now_sec() - t0;
    free(ptrs);
    free(sizes);
    return r;
}

void bench_print(const allocator *a, const bench_result *r) {
    double ops = (double)(r->alloc_calls + r->free_calls);
    double ops_per_sec = r->elapsed_sec > 0 ? ops / r->elapsed_sec : 0.0;
    printf("%-24s | %9zu ops | %7.3f s | %10.0f ops/s | peak %8zu B\n",
           a->vt->name, r->alloc_calls + r->free_calls,
           r->elapsed_sec, ops_per_sec, r->peak_live_bytes);
}

void bench_csv_header(FILE *out) {
    fprintf(out, "allocator,ops,elapsed_sec,ops_per_sec,total_alloc_bytes,peak_bytes\n");
}

void bench_csv_row(FILE *out, const allocator *a, const bench_result *r) {
    double ops = (double)(r->alloc_calls + r->free_calls);
    double ops_per_sec = r->elapsed_sec > 0 ? ops / r->elapsed_sec : 0.0;
    fprintf(out, "%s,%zu,%.6f,%.1f,%zu,%zu\n",
            a->vt->name, r->alloc_calls + r->free_calls,
            r->elapsed_sec, ops_per_sec, r->total_alloc_bytes, r->peak_live_bytes);
}
