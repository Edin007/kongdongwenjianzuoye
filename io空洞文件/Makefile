# 空洞文件「断点续传」下载器
# 板载原生编译:   make
# PC 交叉编译:    make CC=aarch64-linux-gnu-gcc
# 清理:           make clean

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2
TARGET  := hole_download
SRC     := main.c

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -f $(TARGET)
