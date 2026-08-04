/*
blockmalloc: 固定大小内存块分配器（block pool）。C11，无外部依赖。

用途：管理固定 block_size 的块数组，线程/进程安全（原子自旋锁）。
boxmalloc 依赖本库管理 box_head_t 节点。

布局：meta 区（blocks_meta_t）+ block 区（连续 block 数组，每 block = block_head + data）。
空闲链表：单向链，free_next_id 哨兵 -1。block head 自动选 2/4/8 字节节省内存。
*/

#ifndef blockmalloc_H
#define blockmalloc_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t total_size;
    uint64_t block_size;
    uint8_t  sizeof_block_head:4;
    uint64_t malloc_blocks:60;
    uint64_t used_blocks;
    int64_t  free_next_id;   // 首个空闲块 ID，-1=无空闲
    int64_t  lock;            // 原子自旋锁
} blocks_meta_t;

int     blocks_init(blocks_meta_t *meta, uint64_t total_size, uint64_t block_size);
int64_t blocks_alloc(blocks_meta_t *meta, void *block_start);
void    blocks_free(blocks_meta_t *meta, void *block_start, uint64_t block_id);

// 辅助偏移计算
int64_t block_offset(const blocks_meta_t *meta, uint64_t block_id);
int64_t blockdata_offset(const blocks_meta_t *meta, uint64_t block_id);
int64_t blockid_byblockoffset(const blocks_meta_t *meta, uint64_t block_offset);
int64_t blockid_bydataoffset(const blocks_meta_t *meta, uint64_t data_offset);

#endif
