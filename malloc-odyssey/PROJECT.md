# malloc 进化博物馆 —— 项目文档与 AI 协作日志

> 在 RK3566 Linux 开发板上，用 7 个"展厅"系统性讲述 C 语言 `malloc` 函数的前世今生。

---

## 一、项目简介

本项目是一个运行在 **RK3566 Linux 开发板**（LubanCat，4×Cortex-A55，ARMv8-A，64 位）上的命令行交互式教学程序，通过 7 个章节把 `malloc` 50 年的进化史压缩成一条**可亲手重演的主线**。

**核心创意**：不止"讲历史"，而是让学习者**亲手重演每一次进化**——每一代分配器都对应一个自己写的、可运行的 mini 分配器（从粗糙的 first-fit，到分箱，再到多线程 arena），跑同一套 benchmark，量化对比碎片率、扫描步数、吞吐，直观看到"为什么现代 malloc 长这样"。

**成果物**：板上交互程序（每章一个演示 + benchmark）+ 主机端可视化（CSV → matplotlib 图）。

---

## 二、功能设计：7 个展厅

| 章节 | 历史锚点 | 内容 |
|---|---|---|
| 第 0 章 · 没有堆的世界 | 1960s 之前 | 观察静态区 / 栈 / 堆雏形在地址空间中的位置，用 mmap 体验"最原始的动态内存"，并观测真实 glibc 的堆状态 |
| 第 1 章 · 蛮荒时代 | V7 Unix（1979） | 自实现 **first-fit**：边界标记（boundary tags）+ 双向自由链表，亲手暴露 O(n) 线性扫描与碎片问题 |
| 第 2 章 · 规范时代 | dlmalloc（1990s） | 自实现 **segregated bins 分箱** + 大块 mmap，把分配从 O(n) 降到近似 O(1) |
| 第 3 章 · 多核时代 | ptmalloc2（2003） | 多线程 arena：单全局锁 vs 每线程 arena 的并发吞吐对比 |
| 第 4 章 · 速度时代 | glibc 2.26（2017） | tcache 观测：自动 fork 子进程，对比"tcache 开 vs 关"的小对象分配吞吐 |
| 第 5 章 · 安全时代 | 2017–今 | tcache LIFO 复用、double free 检测、safe-linking 指针混淆（纯防御性教学） |
| 第 6 章 · 现在与未来 | 现在 | 三代分配器 + 现代 glibc 同负载对比，导出 CSV 供主机画图 |

每章 = **一段"展签"讲解 + 一个可运行演示 + 一个统一 benchmark**，串成完整叙事。

---

## 三、技术架构

### 3.1 统一分配器接口（让"进化"可量化）

```c
typedef struct {
    const char *name;   /* 展示名，如 "V7 first-fit (1979)" */
    const char *era;    /* 所属章节 */
    void  *(*init)(size_t arena_bytes);
    void  *(*alloc)(void *ctx, size_t n);
    void   (*free)(void *ctx, void *p);
    void  *(*realloc)(void *ctx, void *p, size_t n);
    size_t (*usable)(void *ctx, void *p);
    void   (*stats)(void *ctx);   /* 碎片率 / 扫描步数等诊断 */
    void   (*destroy)(void *ctx);
} alloc_vtable;
```

每一代分配器实现同一套 vtable，同一套 `bench` 跑过每一代，量化对比"这一代比上一代好在哪"。

### 3.2 关键设计决策

- **隔离层**：自实现分配器底层只通过 `mem_backend`（Linux 走 `mmap`）向内核"借地"，**绝不调用 libc 的 malloc/free**——否则就成了"用 malloc 实现 malloc"，观测毫无意义。
- **观测层分离**：真实 glibc 用 `glibc_probe`（`mallinfo2()` + `/proc/self/maps`）单独观测，与自实现不混淆。
- **可移植**：`mem_backend` 在 Windows 走 `VirtualAlloc`，便于 PC 上先验证逻辑，再交叉编译上板。

### 3.3 目录结构

```
malloc-odyssey/
├── Makefile                  # CROSS=aarch64-linux-gnu- 切换交叉编译
├── include/allocator.h       # 统一 vtable 接口
├── common/
│   ├── allocator.c/.h        # vtable 包装
│   ├── mem_backend.c/.h      # mmap 页级内存源（隔离层）
│   ├── bench.c/.h            # 统一 benchmark + CSV 输出
│   ├── glibc_probe.c/.h      # 真实 glibc 观测
│   ├── viz.c/.h              # 终端条形图 / 碎片仪表
│   ├── v7_allocator.c/.h     # 第 1 章分配器（可复用模块）
│   └── dl_allocator.c/.h     # 第 2 章分配器（可复用模块）
├── src/
│   ├── ch0_noheap/main.c     # 第 0 章 · 没有堆的世界
│   ├── ch1_v7firstfit/main.c # 第 1 章 · first-fit
│   ├── ch2_dlmalloc/main.c   # 第 2 章 · 分箱 + mmap
│   ├── ch3_ptmalloc/main.c   # 第 3 章 · 多线程 arena
│   ├── ch4_tcache/main.c     # 第 4 章 · tcache 观测
│   ├── ch5_security/main.c   # 第 5 章 · 安全演示
│   └── ch6_future/main.c     # 第 6 章 · 汇总对比
├── scripts/                  # build / cross_build / deploy
└── tools/plot.py             # 主机端 CSV → 柱状图
```

---

## 四、制作过程（AI 协作日志）

### 阶段 1：需求澄清与方案设计

- **用户需求**：查阅 malloc 相关资料，学习其背景、机制、用法，设计一个创意案例，系统性讲述 malloc 的前世今生，部署到 RK3566 Linux 板。
- **AI 调研**：梳理 malloc 历史时间线——Knuth《TAOCP》(1968) 首次系统描述分配策略 → V7 Unix (1979) 经典 first-fit → Doug Lea dlmalloc (1990s) 分箱 + 大块 mmap → Wolfram Gloger ptmalloc2 (2003) 多 arena → jemalloc/tcmalloc (2005) per-thread cache → glibc 2.26 tcache (2017) → safe-linking (2020) 与 ARM MTE。
- **方案成型**：提出"进化博物馆"主线（7 个展厅，每代分配器亲手重演 + 量化对比）。
- **用户确认**：主线形态 = 进化博物馆；成果物 = 板上交互程序 + 主机端可视化。

### 阶段 2：M1 骨架搭建

搭建项目骨架：统一 `alloc_vtable` 接口、`mem_backend` 隔离层、`bench` 基准框架、`glibc_probe` 观测层、`viz` 可视化、Makefile（含交叉编译）、部署脚本、`plot.py`。

### 阶段 3：各章功能实现

依次实现第 0~6 章：从"没有堆的世界"到"现在与未来"，每一章都是自包含的 C 程序，底层只向内核 mmap 借地，自己管理 chunk / 自由链表 / bins。

### 阶段 4：重构与汇总

- 把第 1、2 章的分配器实现从各自 `main.c` 中抽出，变成 `common/v7_allocator.c`、`common/dl_allocator.c` 可复用模块，供第 6 章总览复用。
- `bench` 增加 CSV 输出，与主机端 `plot.py` 对齐字段契约。

### 阶段 5：板上验证与修复

在 RK3566（LubanCat 板）上编译运行，逐章验证。第 0~2 章一次跑通；第 3 章暴露出一个偶发段错误（见第六节 #5），修复后全部 7 章验证通过。

---

## 五、运行指南

### 5.1 编译（板上有 gcc 或交叉编译）

```bash
cd malloc-odyssey
make                            # 本机编译
# 或交叉编译到 aarch64：
make CROSS=aarch64-linux-gnu-
```

产物在 `build/` 下。

### 5.2 运行

```bash
./build/ch0_noheap              # 依次运行 7 个展厅
./build/ch1_v7firstfit
./build/ch2_dlmalloc
./build/ch3_ptmalloc
./build/ch4_tcache
./build/ch5_security
./build/ch6_future
```

### 5.3 导出可视化数据

```bash
./build/ch6_future --csv > results.csv   # 导出三代分配器对比结果
# 在主机上：
python3 tools/plot.py results.csv        # 生成吞吐 / 峰值存活柱状图
```

---

## 六、踩坑与修复记录

| # | 问题 | 现象 | 根因与修复 |
|---|---|---|---|
| 1 | `-std=c11` 严格模式 | Linux 上 `clock_gettime`/`mmap`/`sysconf` 等 POSIX 函数不声明，编译失败 | 严格 C11 关闭了 POSIX 扩展，改用 `-std=gnu11` |
| 2 | ch2 分箱 fallback 缺失 | 首次分配即返回 NULL | small bins 为空时未回退到 large bin 切分，补充 fallback 逻辑 |
| 3 | ch2 printf 格式 | `%zu` 匹配 `int`，编译 warning | 对 `i * SMALL_STEP` 强转 `size_t` |
| 4 | ch2 扫描步数统计口径 | 对比失真（ch2 反而更慢） | 原先统计"检查的 bin 数（含空 bin）"，改为"实际检查的空闲块数" |
| 5 | **ch3 段错误** | 多线程高负载下偶发 `Segmentation fault` | `arena_alloc` 整块分配时 footer 误写 `need` 而非 `b->size`，导致相邻块向前合并时读错前块大小、`prv` 指向非法地址 |
| 6 | ch6 glibc 包装签名 | vtable 的 `init` 只接收一个 `size_t` 参数，误写成两个参数 | 修正函数签名 |

**最有价值的一课**（#5）：边界标记分配器里，块头部的 `size` 与块尾 footer 的 `size` 必须始终一致——任何一处不一致，都会在"相邻块合并"这条依赖 footer 定位前块的路径上，表现为难以复现的偶发崩溃。

---

## 七、技术要点回顾

- **malloc 的"前世今生"**：固定内存 → 边界标记 + first-fit（V7）→ 分箱 + 大块 mmap（dlmalloc）→ 多 arena（ptmalloc2）→ per-thread cache（tcache/jemalloc/tcmalloc/mimalloc）→ 安全缓解（safe-linking / ASan / ARM MTE）。
- **机制**：chunk 元数据（size/标志位）、多级缓存（tcache → fast/small/large bins → unsorted → top chunk）、相邻空闲块合并、内部/外部碎片。
- **用法与陷阱**：`malloc/calloc/realloc/free`；内存泄漏、double free、use-after-free、越界写、忽略 `realloc` 返回值。
