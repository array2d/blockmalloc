/* Allocate to capacity, verify full data write preserves heads, free all and reallocate. */
#include "common.h"

int main(void)
{
    const uint64_t total = 1024 * 4, bs = 64;
    uint8_t *pool = malloc(total);
    blocks_meta_t m;
    CHECK(pool != NULL && blocks_init(&m, total, bs) == 0);
    const int64_t cap = (int64_t)(total / (2 + bs));  // 62
    CHECK((int64_t)pool_capacity(&m) == cap);

    for (int64_t i = 0; i < cap; i++) {
        CHECK(blocks_alloc(&m, pool) == i);
        memset(pool + blockdata_offset(&m, (uint64_t)i), 0xFF, bs);
    }
    CHECK(blocks_alloc(&m, pool) == -1);
    CHECK(m.used_blocks == (uint64_t)cap);

    for (int64_t i = 0; i < cap; i++)
        blocks_free(&m, pool, (uint64_t)i);
    CHECK(m.used_blocks == 0);

    uint8_t seen[64] = {0};
    for (int64_t i = 0; i < cap; i++) {
        int64_t id = blocks_alloc(&m, pool);
        CHECK(id >= 0 && id < cap && !seen[id]);
        seen[id] = 1;
    }
    CHECK(blocks_alloc(&m, pool) == -1);

    printf("alloc until full ok: %lld blocks\n", (long long)cap);
    free(pool);
    return 0;
}
