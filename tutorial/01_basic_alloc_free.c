/* Basic alloc/free: sequential append, LIFO reuse, double/OOB free ignored, offset helpers. */
#include "common.h"

int main(void)
{
    static uint8_t pool[1024 * 4];
    blocks_meta_t m;
    CHECK(blocks_init(&m, sizeof pool, 64) == 0);
    CHECK(blocks_head_size(&m) == 2);

    for (int64_t i = 0; i < 11; i++)
        CHECK(blocks_alloc(&m, pool) == i);  // append when freelist is empty
    blocks_free(&m, pool, 9);
    CHECK(blocks_alloc(&m, pool) == 9);      // reuse freed block
    blocks_free(&m, pool, 9);
    blocks_free(&m, pool, 3);
    CHECK(blocks_alloc(&m, pool) == 3);      // LIFO reuse
    CHECK(blocks_alloc(&m, pool) == 9);
    CHECK(blocks_alloc(&m, pool) == 11);     // freelist empty, append again
    blocks_free(&m, pool, 5);
    blocks_free(&m, pool, 4);
    blocks_free(&m, pool, 4);                // double free: ignored
    blocks_free(&m, pool, 100);              // OOB free: ignored
    CHECK(m.used_blocks == 10);
    CHECK(blocks_alloc(&m, pool) == 4);
    CHECK(blocks_alloc(&m, pool) == 5);
    CHECK(m.used_blocks == 12 && blocks_malloc_count(&m) == 12);

    // Offset helpers: stride = 2 + 64
    CHECK(block_offset(&m, 3) == 3 * 66);
    CHECK(blockdata_offset(&m, 3) == 3 * 66 + 2);
    CHECK(blockid_byblockoffset(&m, 3 * 66) == 3);
    CHECK(blockid_bydataoffset(&m, 3 * 66 + 2) == 3);
    CHECK(blockid_byblockoffset(&m, 3 * 66 + 1) == -1);  // misaligned offset
    CHECK(blockid_bydataoffset(&m, 1) == -1);            // inside first block head
    CHECK(blockid_bydataoffset(&m, 12 * 66 + 2) == -1);  // beyond allocated blocks

    printf("basic alloc/free ok\n");
    return 0;
}
