/* Multi-threaded concurrent alloc/free within a single process. */
#include <pthread.h>
#include "common.h"

#define NUM_THREADS 8
#define BLOCKS_PER_THREAD 500
#define ROUNDS 20
#define POOL_SIZE (1024 * 1024 * 4)
#define DATA_SIZE 64

static blocks_meta_t meta;
static uint8_t *pool;
static uint32_t *owners;

static void *thread_func(void *arg)
{
    uint32_t tag = (uint32_t)(uintptr_t)arg;
    return stress_worker(&meta, pool, owners, tag, BLOCKS_PER_THREAD, ROUNDS) ? (void *)1 : NULL;
}

int main(void)
{
    pool = calloc(1, POOL_SIZE);
    CHECK(pool != NULL && blocks_init(&meta, POOL_SIZE, DATA_SIZE) == 0);
    owners = calloc(pool_capacity(&meta), sizeof *owners);
    CHECK(owners != NULL);

    pthread_t threads[NUM_THREADS];
    for (uintptr_t i = 0; i < NUM_THREADS; i++)
        CHECK(pthread_create(&threads[i], NULL, thread_func, (void *)(i + 1)) == 0);
    int errors = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        void *ret;
        pthread_join(threads[i], &ret);
        if (ret != NULL)
            errors++;
    }
    printf("multithread: used=%llu malloc=%llu errors=%d\n",
           (unsigned long long)meta.used_blocks, (unsigned long long)blocks_malloc_count(&meta), errors);
    CHECK(errors == 0 && meta.used_blocks == 0);
    CHECK(blocks_malloc_count(&meta) <= (uint64_t)NUM_THREADS * BLOCKS_PER_THREAD);
    free(owners);
    free(pool);
    return 0;
}
