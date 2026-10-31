/*
 * blockmalloc: fixed-size block pool allocator (C11, header-only, no external deps).
 *
 * Layout: separate meta (blocks_meta_t, 48B) and block pool array.
 * Each block i at i*(head+block_size) is [head (2/4/8B LE): bit0=used, bit1=has_next, bits2..=next_free_id][data (block_size B)].
 * Lock-free readers use blocks_head_size / blocks_malloc_count; updates to malloc_blocks must hold meta->lock and use blocks_set_malloc_count.
 */

#ifndef blockmalloc_H
#define blockmalloc_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t total_size;
    uint64_t block_size;
    union {
        struct {
            uint64_t sizeof_block_head : 4;  // 2/4/8
            uint64_t malloc_blocks     : 60; // high-water allocated block count
        };
        uint64_t head_and_blocks;            // full-word alias for atomic access
    };
    uint64_t used_blocks;
    int64_t  free_next_id;   // head of free list, -1 if empty
    int64_t  lock;           // atomic spinlock
} blocks_meta_t;

// Persisted in shared memory and files; ABI layout is fixed.
_Static_assert(sizeof(blocks_meta_t) == 48, "blocks_meta_t layout changed");
_Static_assert(offsetof(blocks_meta_t, head_and_blocks) == 16, "blocks_meta_t layout changed");
_Static_assert(offsetof(blocks_meta_t, lock) == 40, "blocks_meta_t layout changed");

int     blocks_init(blocks_meta_t *meta, uint64_t total_size, uint64_t block_size);
int64_t blocks_alloc(blocks_meta_t *meta, void *block_start);
void    blocks_free(blocks_meta_t *meta, void *block_start, uint64_t block_id);
int64_t blockid_byblockoffset(const blocks_meta_t *meta, uint64_t offset);
int64_t blockid_bydataoffset(const blocks_meta_t *meta, uint64_t data_offset);

// ---- Full-word accessors for head_and_blocks ----
#ifdef _MSC_VER
static inline uint64_t bm_load_u64(const uint64_t *p) { return *(const volatile uint64_t *)p; }
static inline uint64_t bm_load_u64_acquire(const uint64_t *p)
{
    uint64_t v = *(const volatile uint64_t *)p;
    _ReadWriteBarrier();
    return v;
}
static inline void bm_store_u64(uint64_t *p, uint64_t v) { *(volatile uint64_t *)p = v; }
static inline void bm_store_u64_release(uint64_t *p, uint64_t v)
{
    _ReadWriteBarrier();
    *(volatile uint64_t *)p = v;
}
#else
static inline uint64_t bm_load_u64(const uint64_t *p) { return __atomic_load_n(p, __ATOMIC_RELAXED); }
static inline uint64_t bm_load_u64_acquire(const uint64_t *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static inline void bm_store_u64(uint64_t *p, uint64_t v) { __atomic_store_n(p, v, __ATOMIC_RELAXED); }
static inline void bm_store_u64_release(uint64_t *p, uint64_t v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
#endif

// Bit-field positions within head_and_blocks (verified in blocks_init).
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define BLOCKMALLOC_HEAD_SHIFT  60
#define BLOCKMALLOC_COUNT_SHIFT 0
#else
#define BLOCKMALLOC_HEAD_SHIFT  0
#define BLOCKMALLOC_COUNT_SHIFT 4
#endif
#define BLOCKMALLOC_COUNT_MASK ((UINT64_C(1) << 60) - 1)

static inline uint64_t bm_word_head(uint64_t w) { return (w >> BLOCKMALLOC_HEAD_SHIFT) & 0xFu; }
static inline uint64_t bm_word_count(uint64_t w) { return (w >> BLOCKMALLOC_COUNT_SHIFT) & BLOCKMALLOC_COUNT_MASK; }
static inline uint64_t bm_word_make(uint64_t head, uint64_t count)
{
    return ((head & 0xFu) << BLOCKMALLOC_HEAD_SHIFT) | ((count & BLOCKMALLOC_COUNT_MASK) << BLOCKMALLOC_COUNT_SHIFT);
}

// ---- Public lock-free inline helpers ----
static inline uint64_t blocks_head_size(const blocks_meta_t *meta)
{
    return bm_word_head(bm_load_u64(&meta->head_and_blocks));
}
static inline uint64_t blocks_malloc_count(const blocks_meta_t *meta)
{
    return bm_word_count(bm_load_u64_acquire(&meta->head_and_blocks));
}
// Caller must hold meta->lock.
static inline void blocks_set_malloc_count(blocks_meta_t *meta, uint64_t n)
{
    uint64_t w = bm_load_u64(&meta->head_and_blocks);
    bm_store_u64_release(&meta->head_and_blocks, bm_word_make(bm_word_head(w), n));
}
static inline int64_t block_offset(const blocks_meta_t *meta, uint64_t block_id)
{
    return (int64_t)(block_id * (blocks_head_size(meta) + meta->block_size));
}
static inline int64_t blockdata_offset(const blocks_meta_t *meta, uint64_t block_id)
{
    uint64_t head = blocks_head_size(meta);
    return (int64_t)(block_id * (head + meta->block_size) + head);
}

#endif // blockmalloc_H

#if defined(BLOCKMALLOC_IMPLEMENTATION) && !defined(BLOCKMALLOC_IMPLEMENTED)
#define BLOCKMALLOC_IMPLEMENTED

#if defined(ENABLE_LOG) && defined(PLATFORM_LOG)
#define BM_LOG(...) PLATFORM_LOG("[blockmalloc] " __VA_ARGS__)
#else
#define BM_LOG(...) ((void)0)
#endif

// ---- spinlock ----
#ifdef _MSC_VER
#include <windows.h>
#include <intrin.h>
#endif

#if defined(__x86_64__) || defined(__i386__)
static inline void bm_cpu_relax(void) { __asm__ __volatile__("pause" ::: "memory"); }
#elif defined(__aarch64__) || defined(__arm__)
static inline void bm_cpu_relax(void) { __asm__ __volatile__("yield" ::: "memory"); }
#elif defined(_MSC_VER)
static inline void bm_cpu_relax(void) { YieldProcessor(); }
#else
static inline void bm_cpu_relax(void) { (void)0; }
#endif

#ifdef _MSC_VER
static inline int64_t bm_atomic_xchg(int64_t *p, int64_t v)
{
    return (int64_t)InterlockedExchange64((volatile LONG64 *)p, (LONG64)v);
}
static inline int64_t bm_atomic_load_relaxed(const int64_t *p) { return *(const volatile int64_t *)p; }
static inline void bm_atomic_store_release(int64_t *p, int64_t v)
{
    InterlockedExchange64((volatile LONG64 *)p, (LONG64)v);
}
#else
static inline int64_t bm_atomic_xchg(int64_t *p, int64_t v) { return __atomic_exchange_n(p, v, __ATOMIC_ACQ_REL); }
static inline int64_t bm_atomic_load_relaxed(const int64_t *p) { return __atomic_load_n(p, __ATOMIC_RELAXED); }
static inline void bm_atomic_store_release(int64_t *p, int64_t v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
#endif

// Keep spin_lock / spin_unlock / setblock_t names stable for kvspace-c art_bump.
// TTAS spinlock avoids cacheline bouncing while spinning.
static inline void spin_lock(int64_t *lock)
{
    while (bm_atomic_xchg(lock, 1)) {
        while (bm_atomic_load_relaxed(lock))
            bm_cpu_relax();
    }
}
static inline void spin_unlock(int64_t *lock) { bm_atomic_store_release(lock, 0); }

// ---- block head (2/4/8-byte little-endian integer) ----
typedef struct {
    uint8_t  used;
    uint8_t  has_next_free;
    uint64_t next_free_id;
} bm_head_t;

// Unaligned-safe memcpy access; byte-swap on big-endian targets.
#if defined(__GNUC__) || defined(__clang__)
#define BM_MEMCPY __builtin_memcpy
#else
#include <string.h>
#define BM_MEMCPY memcpy
#endif
#if BLOCKMALLOC_HEAD_SHIFT != 0
#define BM_LE16(x) __builtin_bswap16(x)
#define BM_LE32(x) __builtin_bswap32(x)
#define BM_LE64(x) __builtin_bswap64(x)
#else
#define BM_LE16(x) (x)
#define BM_LE32(x) (x)
#define BM_LE64(x) (x)
#endif

static inline uint64_t bm_head_load(const void *p, uint64_t head)
{
    switch (head) {
    case 2: { uint16_t v; BM_MEMCPY(&v, p, 2); return BM_LE16(v); }
    case 4: { uint32_t v; BM_MEMCPY(&v, p, 4); return BM_LE32(v); }
    case 8: { uint64_t v; BM_MEMCPY(&v, p, 8); return BM_LE64(v); }
    default:
        BM_LOG("[ERROR] invalid sizeof_block_head %u", (unsigned)head);
        return 0;
    }
}
static inline void bm_head_store(void *p, uint64_t head, uint8_t used, int64_t free_next_id)
{
    uint64_t v = (uint64_t)(used & 1u);
    if (free_next_id != -1)
        v |= 2u | ((uint64_t)free_next_id << 2);
    switch (head) {
    case 2: { uint16_t x = (uint16_t)BM_LE16((uint16_t)v); BM_MEMCPY(p, &x, 2); break; }
    case 4: { uint32_t x = (uint32_t)BM_LE32((uint32_t)v); BM_MEMCPY(p, &x, 4); break; }
    case 8: { uint64_t x = BM_LE64(v); BM_MEMCPY(p, &x, 8); break; }
    default: BM_LOG("[ERROR] invalid sizeof_block_head %u", (unsigned)head); break;
    }
}

static inline void setblock_t(const blocks_meta_t *meta, void *block_ptr, uint8_t used, int64_t free_next_id)
{
    bm_head_store(block_ptr, blocks_head_size(meta), used, free_next_id);
}

static inline bm_head_t getblock_t(const blocks_meta_t *meta, const void *block_ptr)
{
    uint64_t v = bm_head_load(block_ptr, blocks_head_size(meta));
    return (bm_head_t){
        .used = (uint8_t)(v & 1u),
        .has_next_free = (uint8_t)((v >> 1) & 1u),
        .next_free_id = v >> 2,
    };
}

int blocks_init(blocks_meta_t *meta, uint64_t total_size, uint64_t block_size)
{
    if (meta == NULL || block_size > UINT64_MAX - 8) {
        BM_LOG("[ERROR] meta is NULL or block_size too large");
        return -1;
    }
    // Estimate max blocks with 2B head; pick 2/4/8B head based on next_free_id width (14/30/62 bits).
    uint64_t max_blocks = total_size / (2 + block_size);
    uint64_t head = max_blocks <= 32767ULL / 4 ? 2 : max_blocks <= 2147483647ULL / 4 ? 4 : 8;
    *meta = (blocks_meta_t){
        .total_size = total_size,
        .block_size = block_size,
        .used_blocks = 0,
        .free_next_id = -1,
        .lock = 0,
    };
    meta->head_and_blocks = bm_word_make(head, 0);
    if (meta->sizeof_block_head != head || meta->malloc_blocks != 0) {
        BM_LOG("[ERROR] unsupported bit-field layout of blocks_meta_t");
        return -1;
    }
    return 0;
}

int64_t blockid_byblockoffset(const blocks_meta_t *meta, uint64_t offset)
{
    uint64_t w = bm_load_u64_acquire(&meta->head_and_blocks);
    uint64_t stride = bm_word_head(w) + meta->block_size;
    if (offset % stride != 0) {
        BM_LOG("[ERROR] offset %llu is not a multiple of block stride %llu",
               (unsigned long long)offset, (unsigned long long)stride);
        return -1;
    }
    uint64_t block_id = offset / stride;
    if (block_id >= bm_word_count(w)) {
        BM_LOG("[ERROR] offset %llu is beyond the allocated blocks", (unsigned long long)offset);
        return -1;
    }
    return (int64_t)block_id;
}

int64_t blockid_bydataoffset(const blocks_meta_t *meta, uint64_t data_offset)
{
    uint64_t head = blocks_head_size(meta);
    if (data_offset < head) {
        BM_LOG("[ERROR] data offset %llu is inside the first block head", (unsigned long long)data_offset);
        return -1;
    }
    return blockid_byblockoffset(meta, data_offset - head);
}

int64_t blocks_alloc(blocks_meta_t *meta, void *block_start)
{
    if (meta == NULL || block_start == NULL) {
        BM_LOG("[ERROR] meta or block_start is NULL");
        return -1;
    }
    uint8_t *base = (uint8_t *)block_start;
    int64_t block_id;
    spin_lock(&meta->lock);
    // Load head_and_blocks once under lock.
    uint64_t w = bm_load_u64(&meta->head_and_blocks);
    uint64_t head = bm_word_head(w);
    uint64_t stride = head + meta->block_size;
    if (meta->free_next_id != -1) {
        block_id = meta->free_next_id;
        uint64_t v = bm_head_load(base + (uint64_t)block_id * stride, head);
        meta->free_next_id = (v & 2u) ? (int64_t)(v >> 2) : -1;
        bm_head_store(base + (uint64_t)block_id * stride, head, 1, -1);
        BM_LOG("[INFO] reuse block %lld", (long long)block_id);
    } else {
        uint64_t n = bm_word_count(w);
        uint64_t used_size = n * stride;
        if (used_size > meta->total_size || meta->total_size - used_size < stride) {
            BM_LOG("[ERROR] out of memory: %llu blocks * %llu bytes, total_size %llu",
                   (unsigned long long)n, (unsigned long long)stride, (unsigned long long)meta->total_size);
            spin_unlock(&meta->lock);
            return -1;
        }
        block_id = (int64_t)n;
        bm_head_store(base + (uint64_t)block_id * stride, head, 1, -1);
        bm_store_u64_release(&meta->head_and_blocks, bm_word_make(head, n + 1));
        BM_LOG("[INFO] append block %lld", (long long)block_id);
    }
    meta->used_blocks++;
    spin_unlock(&meta->lock);
    return block_id;
}

void blocks_free(blocks_meta_t *meta, void *block_start, uint64_t block_id)
{
    if (meta == NULL || block_start == NULL) {
        BM_LOG("[ERROR] meta or block_start is NULL");
        return;
    }
    spin_lock(&meta->lock);
    uint64_t w = bm_load_u64(&meta->head_and_blocks);
    uint64_t head = bm_word_head(w);
    if (block_id >= bm_word_count(w)) {
        BM_LOG("[ERROR] block id %llu out of range", (unsigned long long)block_id);
        spin_unlock(&meta->lock);
        return;
    }
    uint8_t *h = (uint8_t *)block_start + block_id * (head + meta->block_size);
    if (!(bm_head_load(h, head) & 1u)) {
        BM_LOG("[WARN] block id %llu already free", (unsigned long long)block_id);
    } else {
        bm_head_store(h, head, 0, meta->free_next_id);
        meta->free_next_id = (int64_t)block_id;
        meta->used_blocks--;
        BM_LOG("[INFO] block id %llu freed", (unsigned long long)block_id);
    }
    spin_unlock(&meta->lock);
}

#undef BM_LOG
#undef BM_MEMCPY
#undef BM_LE16
#undef BM_LE32
#undef BM_LE64

#endif // BLOCKMALLOC_IMPLEMENTATION
