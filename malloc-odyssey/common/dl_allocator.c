#include "dl_allocator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem_backend.h"

/* segregated free lists（bins）+ 大块 mmap（dlmalloc 的核心思想） */

#define ALIGNMENT 16
#define ALIGN(s) (((s) + (ALIGNMENT - 1)) & ~((size_t)(ALIGNMENT - 1)))

#define NSMALL         33
#define SMALL_STEP     16
#define SMALL_MAX      512
#define MMAP_THRESHOLD (128 * 1024)

enum { CHUNK_USED = 0, CHUNK_FREE = 1, CHUNK_MMAP = 2 };

typedef struct block {
    size_t size;
    int    state;
    struct block *next;
    struct block *prev;
} block_t;

typedef struct {
    size_t size;
} footer_t;

#define HDR_SZ    sizeof(block_t)
#define FTR_SZ    sizeof(footer_t)
#define MIN_BLOCK ALIGN(HDR_SZ + FTR_SZ + 16)

typedef struct {
    void    *arena;
    size_t   arena_size;
    block_t  bins[NSMALL];
    block_t  large;

    size_t   n_alloc, n_free, n_mmap;
    size_t   scan_steps;   /* 分配时实际检查的空闲块数 */
    size_t   live_bytes, peak_live;
} dl_ctx;

static void list_init(block_t *h)          { h->next = h->prev = h; }
static int  list_empty(block_t *h)         { return h->next == h; }

static void list_push_front(block_t *h, block_t *b) {
    b->next = h->next;
    b->prev = h;
    h->next->prev = b;
    h->next = b;
}

static void list_remove(block_t *b) {
    b->prev->next = b->next;
    b->next->prev = b->prev;
    b->next = b->prev = NULL;
}

static footer_t *footer_of(block_t *b) {
    return (footer_t *)((char *)b + b->size - FTR_SZ);
}

static block_t *next_block(block_t *b) {
    return (block_t *)((char *)b + b->size);
}

static size_t bin_index(size_t block_size) {
    size_t idx = block_size / SMALL_STEP;
    return idx < NSMALL ? idx : NSMALL - 1;
}

static void insert_free(dl_ctx *c, block_t *b) {
    block_t *head = (b->size <= SMALL_MAX) ? &c->bins[bin_index(b->size)]
                                           : &c->large;
    list_push_front(head, b);
}

static void *dl_init(size_t arena_bytes) {
    arena_bytes = ALIGN(arena_bytes);
    if (arena_bytes < 8 * MIN_BLOCK)
        arena_bytes = 8 * MIN_BLOCK;

    dl_ctx *c = (dl_ctx *)calloc(1, sizeof(*c));
    if (!c)
        return NULL;
    c->arena_size = arena_bytes;
    c->arena = mem_backend_alloc(arena_bytes);
    if (!c->arena) {
        free(c);
        return NULL;
    }

    for (int i = 0; i < NSMALL; i++)
        list_init(&c->bins[i]);
    list_init(&c->large);

    block_t *first = (block_t *)c->arena;
    size_t body = arena_bytes - HDR_SZ;
    first->size = body;
    first->state = CHUNK_FREE;
    first->next = first->prev = NULL;
    *footer_of(first) = (footer_t){ body };

    block_t *sentinel = (block_t *)((char *)first + body);
    sentinel->size = HDR_SZ;
    sentinel->state = CHUNK_USED;
    sentinel->next = sentinel->prev = NULL;

    insert_free(c, first);
    return c;
}

static void *dl_alloc_mmap(dl_ctx *c, size_t n) {
    size_t total = ALIGN(HDR_SZ + n);
    void *p = mem_backend_alloc(total);
    if (!p)
        return NULL;
    block_t *b = (block_t *)p;
    b->size = total;
    b->state = CHUNK_MMAP;
    b->next = b->prev = NULL;
    c->n_mmap++;
    c->n_alloc++;
    return (char *)b + HDR_SZ;
}

static block_t *dl_alloc_small(dl_ctx *c, size_t need) {
    size_t idx = bin_index(need);
    for (size_t i = idx; i < NSMALL; i++) {
        if (!list_empty(&c->bins[i])) {
            c->scan_steps++;                  /* 命中：实际检查 1 个空闲块 */
            block_t *b = c->bins[i].next;
            list_remove(b);
            return b;
        }
    }
    return NULL;
}

static block_t *dl_alloc_large(dl_ctx *c, size_t need) {
    for (block_t *b = c->large.next; b != &c->large; b = b->next) {
        c->scan_steps++;
        if (b->size >= need) {
            list_remove(b);
            return b;
        }
    }
    return NULL;
}

static void *dl_alloc(void *ctx, size_t n) {
    dl_ctx *c = (dl_ctx *)ctx;
    if (n == 0)
        n = 1;
    size_t need = ALIGN(HDR_SZ + n + FTR_SZ);
    if (need < MIN_BLOCK)
        need = MIN_BLOCK;

    if (need >= MMAP_THRESHOLD)
        return dl_alloc_mmap(c, n);

    block_t *b = NULL;
    if (need <= SMALL_MAX) {
        b = dl_alloc_small(c, need);
        if (!b)
            b = dl_alloc_large(c, need);      /* small bins 空，去 large 切一块 */
    } else {
        b = dl_alloc_large(c, need);
    }
    if (!b)
        return NULL;

    if (b->size - need >= MIN_BLOCK) {
        block_t *rest = (block_t *)((char *)b + need);
        rest->size = b->size - need;
        rest->state = CHUNK_FREE;
        *footer_of(rest) = (footer_t){ rest->size };
        insert_free(c, rest);
        b->size = need;
    }

    b->state = CHUNK_USED;
    *footer_of(b) = (footer_t){ b->size };
    c->n_alloc++;
    c->live_bytes += b->size;
    if (c->live_bytes > c->peak_live)
        c->peak_live = c->live_bytes;
    return (char *)b + HDR_SZ;
}

static void dl_free(void *ctx, void *p) {
    if (!p)
        return;
    dl_ctx *c = (dl_ctx *)ctx;
    block_t *b = (block_t *)((char *)p - HDR_SZ);

    if (b->state == CHUNK_MMAP) {
        mem_backend_free(b, b->size);
        c->n_mmap--;
        c->n_free++;
        return;
    }
    if (b->state == CHUNK_FREE) {
        fprintf(stderr, "[dlmalloc] 检测到 double free @ %p\n", p);
        return;
    }

    c->live_bytes -= b->size;
    c->n_free++;

    block_t *nxt = next_block(b);
    if (nxt->state == CHUNK_FREE) {
        list_remove(nxt);
        b->size += nxt->size;
    }
    if ((char *)b != (char *)c->arena) {
        footer_t *pf = (footer_t *)((char *)b - FTR_SZ);
        block_t *prv = (block_t *)((char *)b - pf->size);
        if (prv->state == CHUNK_FREE) {
            list_remove(prv);
            prv->size += b->size;
            b = prv;
        }
    }

    b->state = CHUNK_FREE;
    *footer_of(b) = (footer_t){ b->size };
    insert_free(c, b);
}

static void *dl_realloc(void *ctx, void *p, size_t n) {
    if (!p)
        return dl_alloc(ctx, n);
    if (n == 0) {
        dl_free(ctx, p);
        return NULL;
    }
    block_t *b = (block_t *)((char *)p - HDR_SZ);
    size_t old_payload = (b->state == CHUNK_MMAP) ? (b->size - HDR_SZ)
                                                  : (b->size - HDR_SZ - FTR_SZ);
    if (n <= old_payload)
        return p;
    void *q = dl_alloc(ctx, n);
    if (!q)
        return NULL;
    memcpy(q, p, old_payload);
    dl_free(ctx, p);
    return q;
}

static size_t dl_usable(void *ctx, void *p) {
    (void)ctx;
    if (!p)
        return 0;
    block_t *b = (block_t *)((char *)p - HDR_SZ);
    return (b->state == CHUNK_MMAP) ? (b->size - HDR_SZ)
                                    : (b->size - HDR_SZ - FTR_SZ);
}

static void dl_stats(void *ctx) {
    dl_ctx *c = (dl_ctx *)ctx;
    size_t free_blocks = 0, free_bytes = 0;
    for (int i = 0; i < NSMALL; i++)
        for (block_t *b = c->bins[i].next; b != &c->bins[i]; b = b->next) {
            free_blocks++;
            free_bytes += b->size;
        }
    for (block_t *b = c->large.next; b != &c->large; b = b->next) {
        free_blocks++;
        free_bytes += b->size;
    }

    printf("  空闲块数      : %zu\n", free_blocks);
    printf("  空闲字节      : %zu\n", free_bytes);
    printf("  mmap 大块数   : %zu (>=128KB 单独服务)\n", c->n_mmap);
    printf("  累计扫描步数  : %zu  (对比 ch1 的 O(n))\n", c->scan_steps);
    printf("  分配/释放     : %zu / %zu\n", c->n_alloc, c->n_free);
    printf("  峰值存活字节  : %zu\n", c->peak_live);
}

static void dl_destroy(void *ctx) {
    dl_ctx *c = (dl_ctx *)ctx;
    if (c->arena)
        mem_backend_free(c->arena, c->arena_size);
    free(c);
}

const alloc_vtable dl_vtable = {
    .name = "dlmalloc bins (1990s)",
    .era  = "第 2 章 · 规范时代",
    .init = dl_init,
    .alloc = dl_alloc,
    .free = dl_free,
    .realloc = dl_realloc,
    .usable = dl_usable,
    .stats = dl_stats,
    .destroy = dl_destroy,
};
