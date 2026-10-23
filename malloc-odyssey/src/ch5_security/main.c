/*
 * 第 5 章 · 安全时代 —— 内存错误与缓解（2017~今）
 *
 * tcache 在加速分配的同时，也把攻击面暴露了出来：double free、
 * use-after-free 可以篡改 tcache 空闲块的 next 指针，实现任意地址写
 * （即 CTF 里的 "tcache poisoning"）。
 *
 * glibc 的防线也随之演进：
 *   1. double free 检测（tcache/fastbin 直接 abort）；
 *   2. safe-linking（glibc 2.32，对 next 指针做异或混淆，篡改变得极难）；
 *   3. 编译期 AddressSanitizer（ASan）检测 UAF/double free；
 *   4. 硬件 ARM MTE（内存标签，RK3566 未来可扩展）。
 *
 * 本演示只做"观测"，不构造真实利用，纯防御性教学。
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>

static void demo_lifo(void) {
    printf("[演示 1] tcache 的 LIFO 复用\n");
    void *a = malloc(32);
    void *b = malloc(32);
    printf("  malloc 两个 32B 块 : a=%p  b=%p\n", a, b);
    free(a);
    free(b);                 /* tcache 链表：b -> a（后进先出） */
    void *c = malloc(32);
    void *d = malloc(32);
    printf("  再 malloc 两个 32B 块: c=%p  d=%p\n", c, d);
    printf("  => c 复用了 b，d 复用了 a（LIFO 后进先出）\n");
    free(c);
    free(d);
}

static void demo_double_free(void) {
    printf("\n[演示 2] double free 检测\n");
    pid_t pid = fork();
    if (pid == 0) {
        void *p = malloc(32);
        free(p);
        free(p);             /* 触发 glibc 检测，abort */
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    printf("  子进程被 glibc 终止（检测到 double free）\n");
}

static void demo_safe_linking(void) {
    printf("\n[演示 3] safe-linking 观测（glibc 2.32+）\n");
    void *a = malloc(32);
    void *b = malloc(32);
    free(a);
    free(b);                 /* tcache: b -> a */

    /* 故意读取已释放块的内部 next 字段，仅教学演示 */
    uintptr_t raw_fd  = *(uintptr_t *)b;
    uintptr_t decoded = raw_fd ^ ((uintptr_t)b >> 12);

    printf("  b 的 next 原始值    = 0x%lx\n", (unsigned long)raw_fd);
    printf("  safe-linking 解码    = 0x%lx  (a = %p)\n",
           (unsigned long)decoded, a);

    if ((void *)raw_fd == a)
        printf("  => next 未混淆（glibc < 2.32，无 safe-linking）\n");
    else if ((void *)decoded == a)
        printf("  => next 被异或混淆，safe-linking 生效（glibc >= 2.32）\n");
    else
        printf("  => 未知状态\n");
}

static void banner(void) {
    printf(
"==============================================================\n"
" 第 5 章 · 安全时代 —— 内存错误与缓解（2017~今）\n"
"==============================================================\n"
" tcache 加速分配的同时，也把攻击面暴露出来：double free、\n"
" use-after-free 可篡改 tcache 空闲块的 next 指针，实现任意\n"
" 地址写（CTF 里的 \"tcache poisoning\"）。\n"
"\n"
" glibc 防线随之演进：\n"
"   1) double free 检测（直接 abort）；\n"
"   2) safe-linking（2.32，对 next 指针异或混淆）；\n"
"   3) 编译期 ASan 检测 UAF/double free；\n"
"   4) 硬件 ARM MTE（内存标签）。\n"
"\n"
" 本演示只做观测，不构造真实利用，纯防御性教学。\n"
"==============================================================\n");
}

int main(void) {
    banner();
    demo_lifo();
    demo_double_free();
    demo_safe_linking();
    return 0;
}
