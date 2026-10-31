/* Multi-process concurrent alloc/free over MAP_SHARED anonymous memory. */
#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common.h"

#define NUM_PROCESSES 4
#define BLOCKS_PER_PROCESS 500
#define ROUNDS 20
#define POOL_SIZE (1024 * 1024 * 4)
#define DATA_SIZE 64

static void *shared(size_t n)
{
    void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

int main(void)
{
    blocks_meta_t *meta = shared(sizeof *meta);
    uint8_t *pool = shared(POOL_SIZE);
    CHECK(meta != NULL && pool != NULL && blocks_init(meta, POOL_SIZE, DATA_SIZE) == 0);
    size_t owners_size = pool_capacity(meta) * sizeof(uint32_t);
    uint32_t *owners = shared(owners_size);
    CHECK(owners != NULL);

    for (int p = 0; p < NUM_PROCESSES; p++) {
        pid_t pid = fork();
        CHECK(pid >= 0);
        if (pid == 0)
            _exit(stress_worker(meta, pool, owners, (uint32_t)p + 1, BLOCKS_PER_PROCESS, ROUNDS));
    }
    int ok = 1;
    for (int i = 0; i < NUM_PROCESSES; i++) {
        int status;
        if (wait(&status) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            ok = 0;  // fail on signal termination as well
    }
    printf("multiprocess: used=%llu malloc=%llu\n",
           (unsigned long long)meta->used_blocks, (unsigned long long)blocks_malloc_count(meta));
    CHECK(ok && meta->used_blocks == 0);
    CHECK(blocks_malloc_count(meta) <= (uint64_t)NUM_PROCESSES * BLOCKS_PER_PROCESS);
    munmap(owners, owners_size);
    munmap(pool, POOL_SIZE);
    munmap(meta, sizeof *meta);
    return 0;
}
