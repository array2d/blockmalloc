/* Multi-process x multi-thread concurrent alloc/free over shared meta and pool. */
#define _GNU_SOURCE
#include <pthread.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common.h"

#define NUM_PROCESSES 3
#define NUM_THREADS 3
#define BLOCKS_PER_THREAD 300
#define ROUNDS 20
#define POOL_SIZE (1024 * 1024 * 4)
#define DATA_SIZE 64

static blocks_meta_t *meta;
static uint8_t *pool;
static uint32_t *owners;

static void *shared(size_t n)
{
    void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

static void *thread_func(void *arg)
{
    uint32_t tag = (uint32_t)(uintptr_t)arg;
    return stress_worker(meta, pool, owners, tag, BLOCKS_PER_THREAD, ROUNDS) ? (void *)1 : NULL;
}

int main(void)
{
    meta = shared(sizeof *meta);
    pool = shared(POOL_SIZE);
    CHECK(meta != NULL && pool != NULL && blocks_init(meta, POOL_SIZE, DATA_SIZE) == 0);
    size_t owners_size = pool_capacity(meta) * sizeof(uint32_t);
    owners = shared(owners_size);
    CHECK(owners != NULL);

    for (int p = 0; p < NUM_PROCESSES; p++) {
        pid_t pid = fork();
        CHECK(pid >= 0);
        if (pid == 0) {
            pthread_t threads[NUM_THREADS];
            int rc = 0;
            for (uintptr_t t = 0; t < NUM_THREADS; t++) {
                uintptr_t tag = (uintptr_t)p * NUM_THREADS + t + 1;
                if (pthread_create(&threads[t], NULL, thread_func, (void *)tag) != 0)
                    _exit(1);
            }
            for (int t = 0; t < NUM_THREADS; t++) {
                void *ret;
                pthread_join(threads[t], &ret);
                if (ret != NULL)
                    rc = 1;
            }
            _exit(rc);
        }
    }
    int ok = 1;
    for (int i = 0; i < NUM_PROCESSES; i++) {
        int status;
        if (wait(&status) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            ok = 0;  // fail on signal termination as well
    }
    printf("multiprocess+multithread: used=%llu malloc=%llu\n",
           (unsigned long long)meta->used_blocks, (unsigned long long)blocks_malloc_count(meta));
    CHECK(ok && meta->used_blocks == 0);
    CHECK(blocks_malloc_count(meta) <= (uint64_t)NUM_PROCESSES * NUM_THREADS * BLOCKS_PER_THREAD);
    munmap(owners, owners_size);
    munmap(pool, POOL_SIZE);
    munmap(meta, sizeof *meta);
    return 0;
}
