#include "v7_allocator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem_backend.h"

/* 边界标记 + 双向自由链表 + first-fit（K&R 经典 malloc 的简化重现） */

#define ALIGNMENT 16
#define ALIGN(s) (((s) + (ALIGNMENT - 1)) & ~((size_t)(ALIGNMENT - 1)))

typedef struct block {
    size_t size;
    int    free;
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
    block_t  dummy;

    size_t   n_alloc, n_free;
    size_t   scan_steps;   /* first-fit 累计扫描块数：量化 O(n) */
    size_t   live_bytes, peak_live;
} v7_ctx;

static void list_insert(block_t *head, block_t *b) {
    b->next = head->next;
    b->prev = head;
    head->next->prev = b;
    head->next = b;
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

static void *v7_init(size_t arena_bytes) {
    arena_bytes = ALIGN(arena_bytes);
    if (arena_bytes < 4 * MIN_BLOCK)
        arena_bytes = 4 * MIN_BLOCK;

    v7_ctx *c = (v7_ctx *)calloc(1, sizeof(*c));
    if (!c)
        return NULL;
    c->arena_size = arena_bytes;
    c->arena = mem_backend_alloc(arena_bytes);
    if (!c->arena) {
        free(c);
        return NULL;
    }
    c->dummy.next = c->dummy.prev = &c->dummy;

    block_t *first = (block_t *)c->arena;
    size_t body = arena_bytes - HDR_SZ;
    first->size = body;
    first->free = 1;
    first->next = first->prev = NULL;
    *footer_of(first) = (footer_t){ body };

    block_t *sentinel = (block_t *)((char *)first + body);
    sentinel->size = HDR_SZ;
    sentinel->free = 0;
    sentinel->next = sentinel->prev = NULL;

    list_insert(&c->dummy, first);
    return c;
}

static void *v7_alloc(void *ctx, size_t n) {
    v7_ctx *c = (v7_ctx *)ctx;
    if (n == 0)
        n = 1;
    size_t need = ALIGN(HDR_SZ + n + FTR_SZ);
    if (need < MIN_BLOCK)
        need = MIN_BLOCK;

    block_t *b = c->dummy.next;
    for (; b != &c->dummy; b = b->next) {
        c->scan_steps++;
        if (b->size >= need)
            break;
    }
    if (b == &c->dummy)
        return NULL;

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
    c->n_alloc++;
    c->live_bytes += b->size;
    if (c->live_bytes > c->peak_live)
        c->peak_live = c->live_bytes;
    return (char *)b + HDR_SZ;
}

static void v7_free(void *ctx, void *p) {
    if (!p)
        return;
    v7_ctx *c = (v7_ctx *)ctx;
    block_t *b = (block_t *)((char *)p - HDR_SZ);

    if (b->free) {
        fprintf(stderr, "[v7] 检测到 double free @ %p\n", p);
        return;
    }
    c->live_bytes -= b->size;
    c->n_free++;

    block_t *nxt = next_block(b);
    if (nxt->free) {
        list_remove(nxt);
        b->size += nxt->size;
    }
    if ((char *)b != (char *)c->arena) {
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
    list_insert(&c->dummy, b);
}

static void *v7_realloc(void *ctx, void *p, size_t n) {
    if (!p)
        return v7_alloc(ctx, n);
    if (n == 0) {
        v7_free(ctx, p);
        return NULL;
    }
    block_t *b = (block_t *)((char *)p - HDR_SZ);
    size_t old_payload = b->size - HDR_SZ - FTR_SZ;
    if (n <= old_payload)
        return p;
    void *q = v7_alloc(ctx, n);
    if (!q)
        return NULL;
    memcpy(q, p, old_payload);
    v7_free(ctx, p);
    return q;
}

static size_t v7_usable(void *ctx, void *p) {
    (void)ctx;
    if (!p)
        return 0;
    block_t *b = (block_t *)((char *)p - HDR_SZ);
    return b->size - HDR_SZ - FTR_SZ;
}

static void v7_stats(void *ctx) {
    v7_ctx *c = (v7_ctx *)ctx;
    size_t free_blocks = 0, free_bytes = 0;
    for (block_t *b = c->dummy.next; b != &c->dummy; b = b->next) {
        free_blocks++;
        free_bytes += b->size;
    }
    printf("  空闲块数      : %zu\n", free_blocks);
    printf("  空闲字节      : %zu\n", free_bytes);
    printf("  累计扫描步数  : %zu  (first-fit 平均 O(n))\n", c->scan_steps);
    printf("  分配/释放     : %zu / %zu\n", c->n_alloc, c->n_free);
    printf("  峰值存活字节  : %zu\n", c->peak_live);
    printf("  每块元数据    : %zu B (header+footer)\n", (size_t)(HDR_SZ + FTR_SZ));
}

static void v7_destroy(void *ctx) {
    v7_ctx *c = (v7_ctx *)ctx;
    if (c->arena)
        mem_backend_free(c->arena, c->arena_size);
    free(c);
}

const alloc_vtable v7_vtable = {
    .name = "V7 first-fit (1979)",
    .era  = "第 1 章 · 蛮荒时代",
    .init = v7_init,
    .alloc = v7_alloc,
    .free = v7_free,
    .realloc = v7_realloc,
    .usable = v7_usable,
    .stats = v7_stats,
    .destroy = v7_destroy,
};
