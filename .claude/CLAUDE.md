# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目定位

blockmalloc 是固定大小内存块分配器（block pool），为 deepx 生态的底层依赖（与 memkv、boxmalloc 同级辅助库）。C11 共享库，线程安全，无外部依赖。

## 构建与测试

```bash
./build.sh          # Release 构建 → build/libblockmalloc.so
./build_debug.sh    # Debug 构建（启用 ENABLE_LOG 宏）
```

测试分为 CTest 和 Python test runner 两种方式：

```bash
# CTest（单次，无耗时统计）
cd build && ctest

# Python test runner（含耗时、exit code、CSV 报告，支持 --repeat N）
python3 tutorial/test.py
python3 tutorial/test.py --filter 03        # 按名称过滤
python3 tutorial/test.py --repeat 10        # 每 case 重复 10 轮
python3 tutorial/test.py --no-build         # 跳过编译
python3 tutorial/test.py --errorexit        # 遇错即停
```

tutorial 下 case 清单：

| 文件 | 说明 |
|------|------|
| `01_basic_alloc_free.c` | 基础 alloc/free，复用空闲块 |
| `02_alloc_max.c` | 分配至 OOM，验证上限 |
| `03_multithread.c` | 多线程并发（pthread），同进程内竞态 |
| `04_multiprocess.c` | 多进程并发（mmap 共享内存 + fork） |
| `05_multiprocess_multithread.c` | 多进程 × 多线程（每进程内多线程） |

## 架构

内存布局分为两个独立区域（地址不连续）：

- **meta 区**：`blocks_meta_t` 结构体，含总大小、块大小、已用计数、空闲链表头、自旋锁。
- **block 区**：连续 block 数组，每个 block = `block_head + data`。

关键设计：
- **三种 block head 大小**（2/4/8 字节），`blocks_init` 根据 `total_size / (最小head + block_size)` 自动选择，以节省内存。内部用 `block16_t`/`block32_t`/`block64_t` 位域结构体，通过 `getblock_t`/`setblock_t` 统一读写。
- **空闲链表**：单向链表，`meta->free_next_id` 指向首个空闲块，每个空闲块的 `next_free_id` 指向下一块。分配时优先复用空闲块，无空闲时追加新块。
- **线程安全**：`spinlock.c` 用 `int64_t` 原子操作实现自旋锁，`cpu_relax()` 按 x86（`pause`）/ARM（`yield`）/MSVC（`YieldProcessor`）平台适配。
- **日志**：`logutil.h` 在 `ENABLE_LOG` 宏定义时启用，用户可定义 `PLATFORM_LOG` 宏接管输出（裸机场景用 UART 等），未定义时静默。

## 作为依赖使用

CMake 安装后提供 `blockmallocConfig.cmake`：
```cmake
find_package(blockmalloc REQUIRED)
target_link_libraries(myapp PRIVATE blockmalloc::blockmalloc)
```

## API

```c
int   blocks_init(blocks_meta_t *meta, uint64_t total_size, uint64_t block_size);
int64_t blocks_alloc(blocks_meta_t *meta, void *block_start);
void  blocks_free(blocks_meta_t *meta, void *block_start, uint64_t block_id);

// 辅助偏移计算
int64_t block_offset(const blocks_meta_t *meta, uint64_t block_id);
int64_t blockdata_offset(const blocks_meta_t *meta, uint64_t block_id);
int64_t blockid_byblockoffset(const blocks_meta_t *meta, uint64_t block_offset);
int64_t blockid_bydataoffset(const blocks_meta_t *meta, uint64_t data_offset);
```

注意：meta 区和 block 区地址独立——调用者自行管理两块内存。`blocks_meta_t` 本身也在 meta 区开头，`block_start` 是 block 区的起始指针（通常为调用者分配的字节数组）。
