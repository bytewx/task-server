#ifndef TASK_SERVER_H
#define TASK_SERVER_H

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

/**
 * All sizes are powers of two: simplifies addressing (& instead of %)
 * and improves memory alignment, values are sensible defaults,
 * in production, those must be replaced with benchmark results
 */

/**
 * Number of worker threads, to find the exact value: sysconf(_SC_NPROCESSORS_ONLN)
 */
#define POOL_SIZE 4

/**
 * Ring buffer capacity for the task queue, power of two allows replacing %
 * with bitwise &: (i + 1) & (QUEUE_CAPACITY - 1)
 */
#define QUEUE_CAPACITY 64

/**
 * Maximum number of entries in the result store, this number is chosen because
 * the current project submits no more than approximately 20 tasks
 */
#define MAX_RESULTS 256

/**
 * Task name length in bytes, 64 bytes = one x86 cache line, structs are copied by value,
 * so alignment speeds up copying
 */
#define TASK_NAME_LEN 64

/**
 * An enum assigns a human-readable name to each integer constant,
 * the compiler assigns 0, 1, 2, 3 automatically unless you override,
 * using an enum (rather than plain integers) lets the compiler warn you
 * if a switch statement is missing a case
 */
typedef enum {
 TASK_FIBONACCI,
 TASK_PRIMES,
 TASK_SORT,
 TASK_HASH,
} TaskType;

/**
 * Describes one unit of work that gets pushed into the queue,
 * the struct is copied by value when pushed into the ring buffer,
 * so every field must be self-contained - except str_param
 */
typedef struct {
 uint32_t id;
 TaskType type;
 char     name[TASK_NAME_LEN];
 long     param;

 /**
  * Pointer to an externally owned string used by TASK_HASH,
  * we store a pointer, not a copy, so the caller must guarantee the
  * string outlives the task, this is the kind of lifetime contract
  * that Rust enforces at compile time, in C it is documentation only
  */
 char     *str_param;
} Task;

/**
 * Written by a worker thread after completing a task,
 * stored in ResultStore under a mutex so multiple workers can write
 * concurrently without corrupting each other's data
 */
typedef struct {
 uint32_t  task_id;      /** links this result back to its Task           */
 long      value;        /** numeric answer produced by the algorithm     */
 double    elapsed_ms;   /** wall-clock time the worker spent on the task */
 int       worker_id;    /** which thread executed this task              */
 char      info[128];    /** human-readable summary string                */
} TaskResult;

/**
 * A classic bounded ring (circular) buffer shared between the producer
 * (main thread) and consumers (worker threads)
 *
 * Why a ring buffer?
 *   - Fixed memory footprint: no malloc per task
 *   - O(1) push and pop: just increment head/tail modulo capacity
 *   - Cache-friendly: elements sit in a contiguous array
 *
 * Thread safety is provided by a single mutex (mu), two condition
 * variables let threads sleep instead of spinning:
 *   not_empty — workers wait here when the queue is empty
 *   not_full  — the producer waits here when the queue is full (back-pressure)
 *
 * 'shutdown' is a flag set by queue_shutdown(), workers check it after
 * waking up: if set and the queue is empty, they exit their loop
 */
typedef struct {
 Task            buf[QUEUE_CAPACITY]; /** the ring buffer itself                     */
 size_t          head;                /** index of the next slot to read  (consumer) */
 size_t          tail;                /** index of the next slot to write (producer) */
 size_t          count;               /** number of tasks currently in the buffer    */
 pthread_mutex_t mu;                  /** guards all fields above                    */
 pthread_cond_t  not_empty;           /** signaled when count goes from 0 to 1       */
 pthread_cond_t  not_full;            /** signaled when count drops below capacity   */
 int             shutdown;            /** set to 1 to tell workers to stop           */
} TaskQueue;

/**
 * A simple append-only array of TaskResult values,
 * multiple worker threads call store_add() concurrently,
 * so writes are protected by a dedicated mutex - separate from the queue's mutex
 * to avoid unnecessary contention between the two subsystems
 */
typedef struct {
    TaskResult      results[MAX_RESULTS]; /** flat array, no heap involved   */
    size_t          count;                /** how many results stored so far */
    pthread_mutex_t mu;                   /** guards results[] and count     */
} ResultStore;

/**
 * Everything a worker thread needs, bundled into one struct and passed
 * as a void* to pthread_create(), using a struct avoids global variables
 * and makes each worker's state explicit and self-contained
 *
 * 'queue' and 'store' are pointers - all workers share the same queue
 * and the same result store, the pointed-to objects live in main()'s
 * stack frame for the entire program lifetime
 *
 * 'tasks_done' is written only by the owning thread, never by others,
 * so it needs no mutex, in Rust this maps to a plain field on a
 * thread-local struct, here, discipline enforces the invariant
 */
typedef struct {
    int          id;           /** worker index: 0 … POOL_SIZE-1                   */
    TaskQueue   *queue;        /** shared task queue (pointer, not a copy)         */
    ResultStore *store;        /** shared result store (pointer, not a copy)       */
    pthread_t    thread;       /** the OS thread handle returned by pthread_create */
    uint64_t     tasks_done;   /** private counter — only this thread writes it    */
} Worker;

/**
 * Function declarations (prototypes) tell the compiler the name,
 * return type, and parameter types of every function defined in the
 * corresponding .c files, without these, calling a function from
 * another translation unit would be an error (or undefined behavior)
 *
 * 'const Task *t' means: we pass a pointer for efficiency (no copy),
 * but the function promises not to modify what the pointer points to
 */

/** queue.c */
void   queue_init(TaskQueue *q);
void   queue_destroy(TaskQueue *q);
void   queue_shutdown(TaskQueue *q);
int    queue_push(TaskQueue *q, const Task *t); /** returns 0 on success, -1 if shutdown */
int    queue_pop(TaskQueue *q, Task *out);      /** returns 0 on success, -1 if shutdown */

/** store.c */
void   store_init(ResultStore *s);
void   store_destroy(ResultStore *s);
void   store_add(ResultStore *s, const TaskResult *r);
void   store_print_all(const ResultStore *s);

/** worker.c */
void   worker_start(Worker *w, int id, TaskQueue *q, ResultStore *s);
void   worker_join(const Worker *w);

#endif /** TASK_SERVER_H */ 
