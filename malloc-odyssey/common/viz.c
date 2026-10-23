#include "viz.h"
#include <stdio.h>

void viz_hbar(const char *label, size_t value, size_t total) {
    int filled = 0;
    if (total > 0) {
        unsigned long long scaled =
            (unsigned long long)value * VIZ_BAR_W / total;
        filled = (int)scaled;
        if (filled > VIZ_BAR_W)
            filled = VIZ_BAR_W;
    }
    double pct = total > 0 ? 100.0 * (double)value / (double)total : 0.0;
    printf("%-22s |", label);
    for (int i = 0; i < VIZ_BAR_W; i++)
        putchar(i < filled ? '=' : ' ');
    printf("| %8zu B  (%5.1f%%)\n", value, pct);
}

void viz_frag_gauge(size_t free_bytes, size_t free_blocks, size_t total_bytes) {
    double avg = free_blocks ? (double)free_bytes / (double)free_blocks : 0.0;
    printf("  平均空闲块: %8.1f B  (%zu 块)\n", avg, free_blocks);
    viz_hbar("  空闲字节", free_bytes, total_bytes);
}
