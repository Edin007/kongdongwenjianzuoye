#ifndef MEM_BACKEND_H
#define MEM_BACKEND_H

#include <stddef.h>

/*
 * 页级内存源 —— mini allocator 只从这里向内核"借地"。
 *
 * 这是刻意设置的隔离层：自实现的每一代分配器绝不调用 libc 的
 * malloc/free，而是通过这里一次性 mmap 一块 arena，再自行管理。
 * 否则就成了"用 malloc 实现 malloc"，观测结果毫无意义。
 *
 * Linux/RK3566 走 mmap；Windows 走 VirtualAlloc（便于本地编译验证逻辑）。
 */

void  *mem_backend_alloc(size_t bytes);   /* 返回可写、页对齐的内存，失败返回 NULL */
void   mem_backend_free(void *p, size_t bytes);
size_t mem_backend_page_size(void);
size_t mem_backend_allocated_ever(void);  /* 累计向内核申请的总字节（观测用，free 不减） */

#endif /* MEM_BACKEND_H */
