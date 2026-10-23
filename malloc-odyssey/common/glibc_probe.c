#include "glibc_probe.h"
#include <stdio.h>
#include <string.h>

#if defined(__linux__)
  #include <malloc.h>
#endif

static void read_heap_from_maps(size_t *heap_vm) {
    *heap_vm = 0;
#if defined(__linux__)
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f)
        return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "[heap]")) {
            unsigned long lo = 0, hi = 0;
            if (sscanf(line, "%lx-%lx", &lo, &hi) == 2)
                *heap_vm = (size_t)(hi - lo);
            break;
        }
    }
    fclose(f);
#endif
}

glibc_heap_snapshot glibc_probe(void) {
    glibc_heap_snapshot s;
    memset(&s, 0, sizeof(s));

#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
  #if __GLIBC_PREREQ(2, 33)
    struct mallinfo2 mi = mallinfo2();
    s.have_mallinfo = 1;
    s.arena_bytes  = (size_t)mi.arena;
    s.inuse_bytes  = (size_t)mi.uordblks;
    s.free_bytes   = (size_t)mi.fordblks;
    s.free_blocks  = (size_t)mi.ordblks;
    s.mmap_blocks  = (size_t)mi.hblks;
  #else
    struct mallinfo mi = mallinfo();
    s.have_mallinfo = 1;
    s.arena_bytes  = (size_t)mi.arena;
    s.inuse_bytes  = (size_t)mi.uordblks;
    s.free_bytes   = (size_t)mi.fordblks;
    s.free_blocks  = (size_t)mi.ordblks;
    s.mmap_blocks  = (size_t)mi.hblks;
  #endif
#elif defined(__GLIBC__)
    struct mallinfo mi = mallinfo();
    s.have_mallinfo = 1;
    s.arena_bytes  = (size_t)mi.arena;
    s.inuse_bytes  = (size_t)mi.uordblks;
    s.free_bytes   = (size_t)mi.fordblks;
    s.free_blocks  = (size_t)mi.ordblks;
    s.mmap_blocks  = (size_t)mi.hblks;
#endif

    read_heap_from_maps(&s.heap_vm_bytes);
    return s;
}

void glibc_probe_print(const glibc_heap_snapshot *s) {
    printf("  [heap] 段 VM : %8zu B\n", s->heap_vm_bytes);
    if (s->have_mallinfo) {
        printf("  arena        : %8zu B\n", s->arena_bytes);
        printf("  已用(inuse)  : %8zu B\n", s->inuse_bytes);
        printf("  空闲(free)   : %8zu B  (%zu 块)\n", s->free_bytes, s->free_blocks);
        printf("  mmap 大块    : %8zu 块\n", s->mmap_blocks);
    } else {
        printf("  (mallinfo 不可用，仅显示 [heap] VM)\n");
    }
}

void glibc_probe_print_maps(void) {
#if defined(__linux__)
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) {
        printf("  (无法读取 /proc/self/maps)\n");
        return;
    }
    char line[512];
    int printed_exe = 0;
    while (fgets(line, sizeof(line), f)) {
        int show = 0;
        if (strstr(line, "[heap]") || strstr(line, "[stack]") ||
            strstr(line, "[vdso]") || strstr(line, "[vvar]") ||
            strstr(line, "libc.so"))
            show = 1;
        else if (!printed_exe && strstr(line, "r-xp")) {   /* 主程序可执行段 */
            show = 1;
            printed_exe = 1;
        }
        if (show) {
            line[strcspn(line, "\r\n")] = 0;
            printf("    %s\n", line);
        }
    }
    fclose(f);
#else
    printf("  (仅 Linux 支持 /proc/self/maps)\n");
#endif
}
