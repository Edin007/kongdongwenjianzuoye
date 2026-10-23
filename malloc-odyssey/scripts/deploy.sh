#!/usr/bin/env bash
# 部署到 RK3566（scp）。
# 用法: ./scripts/deploy.sh root@<板子IP> [目标目录]
set -euo pipefail
cd "$(dirname "$0")/.."

TARGET="${1:?用法: ./scripts/deploy.sh root@<板子IP> [目标目录]}"
DEST="${2:-/root/malloc-odyssey}"

[ -d build ] || { echo "错误：build/ 不存在，请先交叉编译。" >&2; exit 1; }

scp -r build "$TARGET:$DEST"
echo "已部署到 $TARGET:$DEST"
