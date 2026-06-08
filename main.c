#include "task_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Task make_task(const uint32_t id, const TaskType type, const char *name, const long param, char *str_param)
{
    Task t;
    t.id        = id;
    t.type      = type;
    t.param     = param;
    t.str_param = str_param;
    strncpy(t.name, name, TASK_NAME_LEN - 1);
    t.name[TASK_NAME_LEN - 1] = '\0';
    return t;
}

int main(void)
{
    printf("=== Thread Pool Task Server ===\n");
    printf("Workers: %d | Queue capacity: %d\n\n", POOL_SIZE, QUEUE_CAPACITY);

    TaskQueue   queue;
    ResultStore store;
    queue_init(&queue);
    store_init(&store);

    Worker workers[POOL_SIZE];

    for (int i = 0; i < POOL_SIZE; i++) {
        worker_start(&workers[i], i, &queue, &store);
    }

    /* Producer push tasks into queue, consumers take them and finish */
    uint32_t next_id = 1;

    const long fibs[] = {10, 20, 30, 40, 42};
    for (size_t i = 0; i < sizeof(fibs) / sizeof(fibs[0]); i++) {
        Task t = make_task(next_id++, TASK_FIBONACCI, "fibonacci", fibs[i], NULL);
        queue_push(&queue, &t);
    }

    const long sieve_limits[] = {100000, 500000, 1000000};
    for (size_t i = 0; i < sizeof(sieve_limits) / sizeof(sieve_limits[0]); i++) {
        Task t = make_task(next_id++, TASK_PRIMES, "sieve", sieve_limits[i], NULL);
        queue_push(&queue, &t);
    }

    const long sort_sizes[] = {50000, 100000, 200000, 500000};
    for (size_t i = 0; i < sizeof(sort_sizes) / sizeof(sort_sizes[0]); i++) {
        Task t = make_task(next_id++, TASK_SORT, "mergesort", sort_sizes[i], NULL);
        queue_push(&queue, &t);
    }

    char *strings[] = {
        "Hello, World!",
        "Rust is great, but C teaches the fundamentals.",
        "pthread_mutex_lock",
    };

    for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); i++) {
        Task t = make_task(next_id++, TASK_HASH, "djb2-hash", 0, strings[i]);
        queue_push(&queue, &t);
    }

    printf("[main] all %u tasks submitted\n\n", next_id - 1);

    /*
     * queue_shutdown sets the flag and wakes up all thread using broadcast,
     * threads finishes their tasks and go out of the loop
     */
    queue_shutdown(&queue);

    for (int i = 0; i < POOL_SIZE; i++) {
        worker_join(&workers[i]);
    }

    store_print_all(&store);

    printf("\n[main] per-worker stats:\n");
    for (int i = 0; i < POOL_SIZE; i++) {
        printf("  worker %d: %llu tasks\n", i, (unsigned long long)workers[i].tasks_done);
    }

    queue_destroy(&queue);
    store_destroy(&store);

    printf("\n[main] clean exit\n");
    return 0;
}
