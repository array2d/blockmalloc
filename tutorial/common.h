/* Shared test assertion and concurrent stress worker. */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blockmalloc/blockmalloc.h"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static inline uint64_t pool_capacity(const blocks_meta_t *meta)
{
    return meta->total_size / (blocks_head_size(meta) + meta->block_size);
}

/* Allocate nblocks, verify ownership and payload integrity, then free in two passes. */
static inline int stress_worker(blocks_meta_t *meta, uint8_t *pool, uint32_t *owners,
                                uint32_t tag, int nblocks, int rounds)
{
    int64_t *ids = malloc((size_t)nblocks * sizeof *ids);
    if (ids == NULL)
        return 1;
    int rc = 0;
    for (int r = 0; r < rounds && rc == 0; r++) {
        int n = 0;
        for (; n < nblocks; n++) {
            int64_t id = blocks_alloc(meta, pool);
            if (id < 0) {
                printf("[ERROR] tag %u: alloc failed at %d\n", tag, n);
                rc = 1;
                break;
            }
            uint32_t expect = 0;
            if (!__atomic_compare_exchange_n(&owners[id], &expect, tag, 0,
                                             __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                printf("[ERROR] block %lld given to tag %u while held by tag %u\n",
                       (long long)id, tag, expect);
                rc = 1;
                break;
            }
            ids[n] = id;
            memset(pool + blockdata_offset(meta, (uint64_t)id), (int)(tag & 0xFFu), (size_t)meta->block_size);
        }
        for (int i = 0; i < n && rc == 0; i++) {
            const uint8_t *d = pool + blockdata_offset(meta, (uint64_t)ids[i]);
            for (uint64_t k = 0; k < meta->block_size; k++) {
                if (d[k] != (uint8_t)tag) {
                    printf("[ERROR] block %lld byte %llu corrupted\n", (long long)ids[i], (unsigned long long)k);
                    rc = 1;
                    break;
                }
            }
        }
        /* Free even indices then odd indices to permute freelist order. */
        for (int pass = 0; pass < 2; pass++) {
            for (int i = pass; i < n; i += 2) {
                __atomic_store_n(&owners[ids[i]], 0u, __ATOMIC_RELEASE);
                blocks_free(meta, pool, (uint64_t)ids[i]);
            }
        }
    }
    free(ids);
    return rc;
}
