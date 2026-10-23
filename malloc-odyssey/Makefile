# 《堆的进化论》构建脚本
#   host 构建（本地验证逻辑）:  make
#   交叉编译到 RK3566 (aarch64): make CROSS=aarch64-linux-gnu-
#
# 章节目录 src/ch* 自动发现，新增展厅无需改本文件。

CROSS   ?=
CC      := $(CROSS)gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -std=gnu11
CPPFLAGS := -Iinclude -Icommon
LDLIBS   := -lpthread

COMMON_SRC := common/allocator.c common/mem_backend.c common/bench.c \
              common/glibc_probe.c common/viz.c \
              common/v7_allocator.c common/dl_allocator.c
COMMON_OBJ := $(COMMON_SRC:.c=.o)

CHAPTERS := $(notdir $(wildcard src/ch*))
BIN      := $(addprefix build/, $(CHAPTERS))

all: $(BIN)

build/ch%: src/ch%/main.c $(COMMON_OBJ) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(COMMON_OBJ) $(LDLIBS) -o $@

build:
	mkdir -p build

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build $(COMMON_OBJ)

.PHONY: all clean
