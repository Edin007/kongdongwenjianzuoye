# 空洞文件「断点续传」下载器

基于文件 I/O 的空洞文件（sparse file）演示程序，运行于**鲁班猫 RK3566**（ARM64 Linux）。
通过 `lseek + write` 定点写入制造/填充文件空洞，模拟下载的分块写入，并支持 **Ctrl+C 断点续传**。

## 功能

| 命令 | 说明 |
|------|------|
| `create <file> <size>` | 创建 ≥128KB 的稀疏文件（全洞），断点初始化为 0 |
| `download <file> [chunk_size]` | 从断点续传，分块定点写入填洞；Ctrl+C 保存断点 |
| `write <file> <offset> <data>` | `lseek + write` 定点写入，不破坏其余空间 |
| `status <file>` | 打印 逻辑大小 / 实际占用块数 / 空洞占比 |
| `help` | 显示用法 |

## 编译

**板载原生编译**（在鲁班猫上，需安装 gcc）：

```bash
make
```

**PC 交叉编译**（在 x86 主机上，产物上传到板子运行）：

```bash
make CC=aarch64-linux-gnu-gcc
```

编译产物为 `hole_download`。`make clean` 清理。

## 使用示例

```bash
# 1. 创建 128KB 稀疏文件（逻辑 128KB，实际几乎不占磁盘）
./hole_download create test.bin 131072

# 2. 查看状态：空洞占比应接近 100%
./hole_download status test.bin

# 3. 开始下载，中途按 Ctrl+C 中断
./hole_download download test.bin 4096
#    输出逐块进度……按 Ctrl+C
#    => "收到 Ctrl+C, 断点已保存: xxx 字节"

# 4. 再次下载，从断点续传
./hole_download download test.bin 4096

# 5. 下载完成后查看状态：空洞占比应为 0%
./hole_download status test.bin

# 6. 演示定点写入（在偏移 100 处写 "hello"，不影响其他区域）
./hole_download write test.bin 100 hello
```

## 空洞文件原理

- **创建空洞**：`lseek(fd, size-1, SEEK_SET)` 把偏移跳到文件末尾之外，再 `write` 1 字节。
  文件逻辑大小变为 `size`，但中间跳过的区域不分配磁盘块，读出来全是 0。
- **定点写入**：每次 `lseek` 到目标偏移再 `write`，只修改目标区域，不破坏其他空间。
- **断点续传**：进度（已完成偏移）写入旁路文件 `<file>.progress`，每写完一块更新一次；
  SIGINT（Ctrl+C）处理器置标志位，循环在块边界保存断点后退出，下次运行读取续传。
- **状态统计**：`stat()` 中 `st_size` 是逻辑大小，`st_blocks * 512` 是实际占用字节，
  空洞占比 = `(1 - 实际占用 / 逻辑大小) × 100%`。

## 文件结构

```
io空洞文件/
├── main.c           # 源码
├── Makefile         # 编译脚本（支持 aarch64 交叉编译）
├── README.md        # 本说明
└── AI协作日志.md     # 人机协作记录
```
