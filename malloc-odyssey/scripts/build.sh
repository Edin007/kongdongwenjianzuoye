#!/usr/bin/env bash
# host 构建：本机 gcc，用于在 PC/Linux 上验证分配器逻辑，无需上板。
set -euo pipefail
cd "$(dirname "$0")/.."

make clean >/dev/null 2>&1 || true
make

echo
echo "构建完成，产物位于 build/ 下："
ls -1 build/
