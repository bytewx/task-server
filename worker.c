#define _POSIX_C_SOURCE 200809L
#include "task_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static long task_fibonacci(const long n) {
    if (n <= 0) {
        return 0;
    }

    if (n == 1) {
        return 1;
    }

    long a = 0, b = 1;

    for (long i = 2; i <= n; i++) {
        long c = a + b;
        a = b;
        b = c;
    }

    return b;
}

static long task_primes(const long limit) {
    if (limit < 2) {
        return 0;
    }

    char *sieve = calloc((size_t)(limit +1), 1);

    if (!sieve) {
        return -1;
    }

    for (long i = 2; (long)(i * i) <= limit; i++) {
        if (!sieve[i]) {
            for (long j = i * i; j <= limit; j += i) {
                sieve[j] = 1;
            }
        }
    }

    long count = 0;

    for (long i = 2; i <= limit; i++) {
        if (!sieve[i]) {
            count++;
        }
    }

    free(sieve);

    return count;
}

static void merge(long *arr, long *tmp, const long l, const long m, const long r) {
    long i = l, j = m + 1, k = l;

    while (i <= m && j <= r) {
        tmp[k++] = arr[i] <= arr[j] ? arr[i++] : arr[j++];
    }

    while (i <= m) {
        tmp[k++] = arr[i++];
    }

    while (j <= r) {
        tmp[k++] = arr[j++];
    }

    memcpy(arr + l, tmp + l, (size_t)(r - l + 1) * sizeof(long));
}

static void merge_sort(long *arr, long *tmp, const long l, const long r) {
    if (l >= r) {
        return;
    }

    const long m = l + (r - l) / 2;

    merge_sort(arr, tmp, l, m);
    merge_sort(arr, tmp, m + 1, r);
    merge(arr, tmp, l, m, r);
}

static long task_sort(const long n) {
    if (n <= 0) {
        return 0;
    }

    long *arr = malloc((size_t)n * sizeof(long));
    long *tmp = malloc((size_t)n * sizeof(long));

    if (!arr || !tmp) {
        free(arr);
        free(tmp);
        return -1;
    }

    /* Every thread has its own seed */
    unsigned seed = (unsigned)(uintptr_t)arr;

    for (long i = 0; i < n; i++) {
        arr[i] = rand_r(&seed) % (n * 10);
    }

    merge_sort(arr, tmp, 0, n - 1);

    /* Return last element to clarify that function works correctly */
    long result = arr[n - 1];
    free(arr);
    free(tmp);
    return result;
}

static long task_hash(const char *s) {
    unsigned long h = 5381;
    int c;

    while ((c = (unsigned char)*s++)) {
        h = (h << 5) + h + (unsigned long)c; /* h * 33 + c */
    }

    return (long)(h & 0x7FFFFFFF);
}

static void execute_task(const Task *t, TaskResult *r, int worker_id)
{
    r->task_id   = t->id;
    r->worker_id = worker_id;

    double t0 = now_ms();

    switch (t->type) {
        case TASK_FIBONACCI:
            r->value = task_fibonacci(t->param);
            snprintf(r->info, sizeof(r->info),
                     "fib(%ld) = %ld", t->param, r->value);
            break;

        case TASK_PRIMES:
            r->value = task_primes(t->param);
            snprintf(r->info, sizeof(r->info),
                     "primes up to %ld: count=%ld", t->param, r->value);
            break;

        case TASK_SORT:
            r->value = task_sort(t->param);
            snprintf(r->info, sizeof(r->info),
                     "sorted %ld elements, max=%ld", t->param, r->value);
            break;

        case TASK_HASH:
            r->value = task_hash(t->str_param ? t->str_param : "");
            snprintf(r->info, sizeof(r->info),
                     "hash(\"%s\") = 0x%lX",
                     t->str_param ? t->str_param : "", r->value);
            break;
    }

    r->elapsed_ms = now_ms() - t0;
}

static void *worker_loop(void *arg)
{
    Worker *w = arg;
    printf("[worker %d] started\n", w->id);

    Task task;
    while (queue_pop(w->queue, &task) == 0) {
        printf("[worker %d] executing task #%u \"%s\"\n", w->id, task.id, task.name);

        TaskResult result = {0};
        execute_task(&task, &result, w->id);
        store_add(w->store, &result);

        w->tasks_done++;

        printf("[worker %d] done task #%u in %.2f ms | %s\n", w->id, task.id, result.elapsed_ms, result.info);
    }

    printf("[worker %d] shutting down (processed %llu tasks)\n", w->id, (unsigned long long)w->tasks_done);
    return NULL;
}

void worker_start(Worker *w, const int id, TaskQueue *q, ResultStore *s)
{
    w->id         = id;
    w->queue      = q;
    w->store      = s;
    w->tasks_done = 0;
    pthread_create(&w->thread, NULL, worker_loop, w);
}

void worker_join(const Worker *w)
{
    pthread_join(w->thread, NULL);
}
