#!/usr/bin/env bash
# 交叉编译到 RK3566 (aarch64)。
# 用法: CROSS=aarch64-linux-gnu- ./scripts/cross_build.sh
set -euo pipefail
cd "$(dirname "$0")/.."

CROSS="${CROSS:-aarch64-linux-gnu-}"
command -v "${CROSS}gcc" >/dev/null 2>&1 || {
    echo "错误：未找到交叉编译器 ${CROSS}gcc" >&2
    echo "请先安装（如 apt install gcc-aarch64-linux-gnu），或通过 CROSS= 指定路径。" >&2
    exit 1
}

make clean >/dev/null 2>&1 || true
make CROSS="$CROSS"

echo
echo "交叉编译完成，产物位于 build/ 下："
file build/* 2>/dev/null || ls -1 build/
