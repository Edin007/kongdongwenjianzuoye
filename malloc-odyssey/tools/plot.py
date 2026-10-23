#!/usr/bin/env python3
"""
《堆的进化论》主机端可视化

读入板上程序导出的 benchmark CSV（各代 allocator 的对比结果），
画出吞吐与峰值存活的柱状对比。

用法: python3 tools/plot.py <results.csv>

CSV 契约（与 common/bench.c 的 bench_csv_header 对齐）:
    allocator,ops,elapsed_sec,ops_per_sec,total_alloc_bytes,peak_bytes
"""
import sys
import csv


def main(path):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        sys.exit("需要 matplotlib，请先: pip install matplotlib")

    names, ops_per_sec, peak = [], [], []
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            names.append(row["allocator"])
            ops_per_sec.append(float(row["ops_per_sec"]))
            peak.append(float(row["peak_bytes"]))

    if not names:
        sys.exit("CSV 为空或字段不匹配")

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))

    ax1.bar(names, ops_per_sec, color="#4C78A8")
    ax1.set_title("吞吐 (ops/s, 越高越好)")
    ax1.set_ylabel("ops/s")
    ax1.tick_params(axis="x", rotation=30)

    ax2.bar(names, peak, color="#F58518")
    ax2.set_title("峰值存活字节 (B)")
    ax2.tick_params(axis="x", rotation=30)

    fig.tight_layout()
    out = path.rsplit(".", 1)[0] + ".png"
    fig.savefig(out, dpi=140)
    print(f"已生成 {out}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("用法: python3 tools/plot.py <results.csv>")
    main(sys.argv[1])
