#ifndef VIZ_H
#define VIZ_H

#include <stddef.h>

/* 终端 ANSI 可视化工具（各章碎片图与第 6 章汇总共用） */

#define VIZ_BAR_W 40

/* 打印一行水平条形图：label |====    | value (pct) */
void viz_hbar(const char *label, size_t value, size_t total);

/* 打印碎片度量：平均空闲块大小（越大说明合并越充分、外部碎片越小） */
void viz_frag_gauge(size_t free_bytes, size_t free_blocks, size_t total_bytes);

#endif /* VIZ_H */
