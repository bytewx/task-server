#include "task_server.h"
#include <stdio.h>
#include <string.h>

void store_init(ResultStore *s) {
    memset(s, 0, sizeof(*s));
    pthread_mutex_init(&s->mu, NULL);
}

void store_destroy(ResultStore *s) {
    pthread_mutex_destroy(&s->mu);
}

void store_add(ResultStore *s, const TaskResult *r) {
    pthread_mutex_lock(&s->mu);
    if (s->count < MAX_RESULTS) {
        s->results[s->count++] = *r;
    }
    pthread_mutex_unlock(&s->mu);
}

void store_print_all(const ResultStore *s)
{
    for (size_t i = 0; i < s->count; i++) {
        const TaskResult *r = &s->results[i];
        printf("  [#%03u] worker=%-2d  %7.2f ms  %s\n", r->task_id, r->worker_id, r->elapsed_ms, r->info);
    }
}
