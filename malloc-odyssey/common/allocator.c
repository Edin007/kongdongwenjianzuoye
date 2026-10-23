#include "allocator.h"
#include <stdlib.h>

allocator *allocator_create(const alloc_vtable *vt, size_t arena_bytes) {
    allocator *a;
    if (!vt || !vt->init)
        return NULL;
    a = (allocator *)calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    a->vt = vt;
    a->arena_bytes = arena_bytes;
    a->ctx = vt->init(arena_bytes);
    if (!a->ctx) {
        free(a);
        return NULL;
    }
    return a;
}

void *allocator_alloc(allocator *a, size_t n) {
    return a && a->vt->alloc ? a->vt->alloc(a->ctx, n) : NULL;
}

void allocator_free(allocator *a, void *p) {
    if (a && a->vt->free && p)
        a->vt->free(a->ctx, p);
}

void *allocator_realloc(allocator *a, void *p, size_t n) {
    return a && a->vt->realloc ? a->vt->realloc(a->ctx, p, n) : NULL;
}

size_t allocator_usable(allocator *a, void *p) {
    return a && a->vt->usable ? a->vt->usable(a->ctx, p) : 0;
}

void allocator_stats(allocator *a) {
    if (a && a->vt->stats)
        a->vt->stats(a->ctx);
}

void allocator_destroy(allocator *a) {
    if (!a)
        return;
    if (a->vt->destroy)
        a->vt->destroy(a->ctx);
    free(a);
}
