/*
 * 第 3 章 · 多核时代 —— ptmalloc2 多 arena（2003）
 *
 * 2000 年后多核普及，单堆 + 一把锁的问题暴露：所有线程的
 * malloc/free 都要抢同一把锁，锁竞争让多线程反而变慢。
 *
 * Wolfram Gloger 的 ptmalloc2（2003 进 glibc）解法：给不同线程
 * 分配不同的 arena（分配区），线程优先在自己的 arena 上分配，
 * 大大减少锁竞争。
 *
 * 本演示对比两种模式在 4 线程并发下的吞吐。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "mem_backend.h"

/* ---------- 简化 first-fit 分配器（复用 ch1 的边界标记思路） ---------- */

#define ALIGNMENT 16
#define ALIGN(s) (((s) + (ALIGNMENT - 1)) & ~((size_t)(ALIGNMENT - 1)))

typedef struct block {
    size_t size;
    int    free;
    struct block *next, *prev;
} block_t;
typedef struct { size_t size; } footer_t;

#define HDR_SZ    sizeof(block_t)
#define FTR_SZ    sizeof(footer_t)
#define MIN_BLOCK ALIGN(HDR_SZ + FTR_SZ + 16)

typedef struct {
    void    *base;
    size_t   size;
    block_t  dummy;   /* free list 哨兵 */
} arena_t;

static footer_t *footer_of(block_t *b) {
    return (footer_t *)((char *)b + b->size - FTR_SZ);
}

static void list_push(block_t *head, block_t *b) {
    b->next = head->next;
    b->prev = head;
    head->next->prev = b;
    head->next = b;
}

static void list_remove(block_t *b) {
    b->prev->next = b->next;
    b->next->prev = b->prev;
}

static arena_t *arena_init(size_t bytes) {
    bytes = ALIGN(bytes);
    if (bytes < 8 * MIN_BLOCK)
        bytes = 8 * MIN_BLOCK;
    arena_t *a = (arena_t *)calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    a->base = mem_backend_alloc(bytes);
    if (!a->base) {
        free(a);
        return NULL;
    }
    a->size = bytes;
    a->dummy.next = a->dummy.prev = &a->dummy;

    block_t *first = (block_t *)a->base;
    size_t body = bytes - HDR_SZ;
    first->size = body;
    first->free = 1;
    first->next = first->prev = NULL;
    *footer_of(first) = (footer_t){ body };

    block_t *sentinel = (block_t *)((char *)first + body);
    sentinel->size = HDR_SZ;
    sentinel->free = 0;

    list_push(&a->dummy, first);
    return a;
}

static void *arena_alloc(arena_t *a, size_t n) {
    if (n == 0)
        n = 1;
    size_t need = ALIGN(HDR_SZ + n + FTR_SZ);
    if (need < MIN_BLOCK)
        need = MIN_BLOCK;

    for (block_t *b = a->dummy.next; b != &a->dummy; b = b->next) {
        if (b->size >= need) {
            if (b->size - need >= MIN_BLOCK) {
                block_t *rest = (block_t *)((char *)b + need);
                rest->size = b->size - need;
                rest->free = 1;
                *footer_of(rest) = (footer_t){ rest->size };
                rest->next = b->next;
                rest->prev = b->prev;
                b->next->prev = rest;
                b->prev->next = rest;
                b->size = need;
            } else {
                list_remove(b);
            }
            b->free = 0;
            *footer_of(b) = (footer_t){ b->size };
            return (char *)b + HDR_SZ;
        }
    }
    return NULL;
}

static void arena_free(arena_t *a, void *p) {
    if (!p)
        return;
    block_t *b = (block_t *)((char *)p - HDR_SZ);
    if (b->free)
        return;   /* double free 保护 */

    block_t *nxt = (block_t *)((char *)b + b->size);
    if (nxt->free) {
        list_remove(nxt);
        b->size += nxt->size;
    }
    if ((char *)b != (char *)a->base) {
        footer_t *pf = (footer_t *)((char *)b - FTR_SZ);
        block_t *prv = (block_t *)((char *)b - pf->size);
        if (prv->free) {
            list_remove(prv);
            prv->size += b->size;
            b = prv;
        }
    }
    b->free = 1;
    *footer_of(b) = (footer_t){ b->size };
    list_push(&a->dummy, b);
}

/* ---------- 两种并发模式 ---------- */

/* 模式 A：单全局 arena + 一把互斥锁（早期单一堆） */
static arena_t        *g_arena;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* 模式 B：每线程独立 arena（thread-local，无锁） */
#define TL_ARENA_BYTES (1u << 20)
static _Thread_local arena_t *tl_arena;

static void *locked_alloc(size_t n) {
    pthread_mutex_lock(&g_lock);
    void *p = arena_alloc(g_arena, n);
    pthread_mutex_unlock(&g_lock);
    return p;
}

static void locked_free(void *p) {
    pthread_mutex_lock(&g_lock);
    arena_free(g_arena, p);
    pthread_mutex_unlock(&g_lock);
}

static void *tl_alloc(size_t n) {
    if (!tl_arena)
        tl_arena = arena_init(TL_ARENA_BYTES);   /* 懒创建本线程的 arena */
    return arena_alloc(tl_arena, n);
}

static void tl_free(void *p) {
    arena_free(tl_arena, p);
}

/* ---------- 多线程 benchmark ---------- */

static size_t rand_range(unsigned *seed, size_t lo, size_t hi) {
    *seed = *seed * 1103515245u + 12345u;
    if (hi <= lo)
        return lo;
    return lo + ((size_t)(*seed) % (hi - lo + 1));
}

typedef struct {
    int      use_tl;
    int      n_ops;
    int      max_live;
    unsigned seed;
} worker_arg;

static void *worker(void *v) {
    worker_arg *w = (worker_arg *)v;
    void **ptrs = (void **)calloc((size_t)w->max_live, sizeof(void *));
    unsigned seed = w->seed;

    for (int i = 0; i < w->n_ops; i++) {
        int idx = (int)rand_range(&seed, 0, (size_t)w->max_live - 1);
        if (ptrs[idx] == NULL) {
            size_t n = rand_range(&seed, 8, 256);
            void *p = w->use_tl ? tl_alloc(n) : locked_alloc(n);
            if (p)
                ptrs[idx] = p;
        } else {
            if (w->use_tl)
                tl_free(ptrs[idx]);
            else
                locked_free(ptrs[idx]);
            ptrs[idx] = NULL;
        }
    }
    for (int i = 0; i < w->max_live; i++) {
        if (ptrs[i]) {
            if (w->use_tl)
                tl_free(ptrs[i]);
            else
                locked_free(ptrs[i]);
        }
    }
    free(ptrs);
    return NULL;
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void run_bench(int use_tl, int nthreads, int n_ops, int max_live) {
    pthread_t  *th   = (pthread_t *)calloc((size_t)nthreads, sizeof(pthread_t));
    worker_arg *args = (worker_arg *)calloc((size_t)nthreads, sizeof(worker_arg));

    for (int i = 0; i < nthreads; i++) {
        args[i].use_tl   = use_tl;
        args[i].n_ops    = n_ops;
        args[i].max_live = max_live;
        args[i].seed     = 42u + (unsigned)i;
    }

    double t0 = now_sec();
    for (int i = 0; i < nthreads; i++)
        pthread_create(&th[i], NULL, worker, &args[i]);
    for (int i = 0; i < nthreads; i++)
        pthread_join(th[i], NULL);
    double t1 = now_sec();

    double total_ops = (double)nthreads * n_ops;
    double secs = t1 - t0;
    printf("  %-22s | %d 线程 x %d ops | %6.3f s | %9.0f ops/s\n",
           use_tl ? "每线程 arena（无锁）" : "单 arena + 全局锁",
           nthreads, n_ops, secs, total_ops / secs);

    free(th);
    free(args);
}

/* ---------- 演示 ---------- */

static void banner(void) {
    printf(
"==============================================================\n"
" 第 3 章 · 多核时代 —— ptmalloc2 多 arena（2003）\n"
"==============================================================\n"
" 2000 年后多核 CPU 普及，单堆 + 一把锁的问题暴露：\n"
" 所有线程的 malloc/free 都要抢同一把锁，锁竞争让多线程\n"
" 反而变慢。\n"
"\n"
" Wolfram Gloger 的 ptmalloc2（2003 进 glibc）解法：\n"
" 给不同线程分配不同的 arena（分配区），线程优先在自己的\n"
" arena 上分配，大大减少锁竞争。\n"
"\n"
" 本演示对比两种模式在 4 线程并发下的吞吐。\n"
"==============================================================\n");
}

int main(void) {
    banner();

    const int nthreads = 4;       /* RK3566 是 4 核 Cortex-A55 */
    const int n_ops    = 200000;
    const int max_live = 512;

    printf("\n[模式 A] 单全局 arena + 一把互斥锁（早期单一堆）\n");
    g_arena = arena_init(4u << 20);   /* 4MB 全线程共享 */
    run_bench(0, nthreads, n_ops, max_live);

    printf("\n[模式 B] 每线程独立 arena（ptmalloc2 的多 arena 思想）\n");
    run_bench(1, nthreads, n_ops, max_live);

    return 0;
}
