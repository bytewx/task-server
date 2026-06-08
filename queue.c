/**
 * Bounded ring buffer shared between the producer (main thread)
 * and the consumer workers, all operations are thread-safe via
 * a mutex and two condition variables
 */

#include "task_server.h"
#include <string.h>

/**
 * Prepares a TaskQueue for use, must be called exactly once, before any thread
 * touches the queue
 *
 * memset(q, 0, sizeof(*q)) zeroes every byte of the struct: head, tail, count,
 * and shutdown all become 0, this is safe here because the struct contains
 * no pointers that need to point anywhere valid yet
 *
 * pthread_mutex_init(mutex, attr):
 *    Creates a mutex, attr=NULL means default attributes
 *    (non-recursive, not shared across processes),
 *    the mutex starts in the "unlocked" state
 *
 * pthread_cond_init(cond, attr):
 *    Creates a condition variable, attr=NULL means default attributes,
 *    a condition variable has no state of its own - it is always used
 *    together with a mutex
 */
void queue_init(TaskQueue *q) {
    memset(q, 0, sizeof(TaskQueue));
    pthread_mutex_init(&q->mu, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

/**
 * Releases OS resources held by the mutex and condition variables,
 * must be called after all threads have finished using the queue
 *
 * Order: condition variables are destroyed before the mutex,
 * because internally they may reference it, destroying in the wrong
 * order is undefined behavior
 */
void queue_destroy(TaskQueue *q) {
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    pthread_mutex_destroy(&q->mu);
}

/**
 * Called by the producer (main thread) to enqueue one task,
 * blocks if the buffer is full - this is intentional back-pressure:
 * the producer slows down instead of losing tasks or growing unbounded
 *
 * Returns  0 on success
 * Returns -1 if shutdown was requested (task is NOT enqueued)
 */
int queue_push(TaskQueue *q, const Task *t) {
    /*
     * Lock the mutex before touching any shared field,
     * from this point until unlock, no other thread can enter
     * a critical section that also lock q->mu
     */
    pthread_mutex_lock(&q->mu);

    /*
     * Wait while the buffer is full AND we have not been asked to stop
     *
     * pthread_cond_wait(cond, mutex) does three things atomically:
     *    1. Releases the mutex - so other threads can make progress
     *    2. Puts this thread to sleep on 'cond'
     *    3. When signaled, re-acquires the mutex before returning
     *
     * We use 'while', not 'if', because of spurious wakeups: POSIX
     * allows a thread to wake up even without a signal, re-checking
     * the condition after waking is mandatory
     */
    while (q->count == QUEUE_CAPACITY && !q->shutdown) {
        pthread_cond_wait(&q->not_full, &q->mu);
    }

    /*
     * If shutdown was set while we were waiting, abort cleanly
     */
    if (q->shutdown) {
        pthread_mutex_unlock(&q->mu);
        return -1;
    }

    /*
     * Copy the task into the next free slot at 'tail',
     * '*t' dereferences the pointer, giving us the Task value,
     * the assignment copies every field of the struct by value
     */
    q->buf[q->tail] = *t;

    /*
     * Advance tail, wrapping around to 0 when it reaches the end,
     * this is what makes the buffer a "ring": it reuses slots from
     * the beginning once the end is reached
     */
    q->tail = (q->tail + 1) % QUEUE_CAPACITY;
    q->count++;

    /*
     * Signal ONE waiting that there is now something to read,
     * pthread_cond_signal wakes exactly one thread blocked on not_empty,
     * we use signal (not broadcast) because only one worker should take
     * this single new task
     */
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mu);
    return 0;
}

/**
 * Called by each worker thread in a loop to fetch the next task,
 * blocks if the buffer is empty - the worker sleeps instead of spinning,
 * consuming no CPU while waiting
 *
 * Returns  0 and writes the task into *out on success
 * Returns -1 when shutdown is set AND the buffer is empty - the worker
 * should exit its loop upon seeing -1
 */
int queue_pop(TaskQueue *q, Task *out) {
    pthread_mutex_lock(&q->mu);

    /* Same pattern as queue_push: sleep while there is nothing to do */
    while (q->count == 0 && !q->shutdown) {
        pthread_cond_wait(&q->not_empty, &q->mu);
    }

    /*
     * Shutdown was set, if there are still tasks left in the buffer,
     * we fall through and process them - we do NOT discard work,
     * only when the buffer is truly empty do we tell the caller to stop
     */
    if (q->count == 0) {
        pthread_mutex_unlock(&q->mu);
        return -1;
    }

    /*
     * Copy the task at 'head' out to the caller's variable,
     * then advance head and decrement count
     */
    *out = q->buf[q->head];
    q->head = (q->head + 1) % QUEUE_CAPACITY;
    q->count--;

    /* Signal the producer that a slot just opened up */
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mu);
    return 0;
}

/**
 * Called by the producer when all tasks have been submitted,
 * sets the shutdown flag and wakes every sleeping thread so
 * none of them gets stuck waiting forever
 *
 * pthread_cond_broadcast wakes ALL threads blocked on a condition,
 * unlike pthread_cond_signal which wakes only one,
 * we broadcast on both condition variables because:
 *    - workers may be sleeping on not_empty (queue was empty)
 *    - the producer itself could be sleeping on not_full (queue was full)
 *
 * Every woken thread will re-check its 'while' condition, see shutdown=1,
 * and exit cleanly
 */
void queue_shutdown(TaskQueue *q) {
    pthread_mutex_lock(&q->mu);
    q->shutdown = 1;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mu);
}
