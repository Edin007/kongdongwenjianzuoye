#include <stdio.h>
#include "mem_backend.h"
#include "glibc_probe.h"

/*
 * 第 0 章 · 没有堆的世界（1960s 之前）
 *
 * 观察静态区 / 栈 / 堆雏形在地址空间中的位置，
 * 并用 mmap 直接向内核借一页，体验"最原始的动态内存"。
 */

static int g_data = 42;   /* .data —— 已初始化静态区 */
static int g_bss;         /* .bss  —— 零初始化静态区 */

static void banner(void) {
    printf(
"==============================================================\n"
" 第 0 章 · 没有堆的世界（1960s 之前）\n"
"==============================================================\n"
" 最早的程序内存是静态的：编译器决定每个变量放哪。\n"
" 栈提供函数调用的临时空间，但数据随函数返回就消失；\n"
" 静态区活到进程结束，但大小编译期就写死。\n"
"\n"
" 于是有了一个朴素的问题：运行时如何按需\"要一块内存\"？\n"
" 答案：把地址空间里一片没人用的区域向内核申请出来，\n"
" 自己管理 —— 这就是\"堆\"(heap) 的雏形。\n"
"\n"
" 本演示：\n"
"   1) 观察静态区 / 栈 / 堆雏形各在地址空间哪一端；\n"
"   2) 用 mmap 直接向内核借一页，体验最原始的动态内存。\n"
"==============================================================\n");
}

static void show_layout(void) {
    int local = 0;                          /* 栈 */
    size_t ps = mem_backend_page_size();
    void *p = mem_backend_alloc(ps);        /* 堆雏形：匿名 mmap */

    printf("\n[1] 三种内存各在地址空间哪一端\n");
    printf("  &g_data (静态区 .data)  : %p\n", (void *)&g_data);
    printf("  &g_bss  (静态区 .bss )  : %p\n", (void *)&g_bss);
    printf("  &local  (栈)            : %p\n", (void *)&local);
    printf("  mmap 匿名映射（堆雏形） : %p\n", p);

    /* 体验最原始的动态内存：直接向这块页写数据 */
    char *raw = (char *)p;
    raw[0] = 'H';
    raw[1] = 'i';
    printf("\n[2] 最原始的动态内存：直接向 mmap 页写入 \"%c%c\"\n",
           raw[0], raw[1]);

    mem_backend_free(p, ps);
}

int main(void) {
    banner();
    show_layout();

    printf("\n[3] 进程地址空间关键段（/proc/self/maps）\n");
    glibc_probe_print_maps();

    printf("\n[4] 当前进程堆状态（真实 glibc 观测）\n");
    glibc_heap_snapshot s = glibc_probe();
    glibc_probe_print(&s);

    return 0;
}
