#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <errno.h>

#include "types.h"

/* maximum number of threads allowed in a pool   */
#define MAXT_IN_POOL 200

#define handle_error_en(en, msg) do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)
#define handle_error(msg)        do { perror(msg); exit(EXIT_FAILURE); } while (0)

/**
 * You must hide the internal details of the threadpool structure from callers, thus declare threadpool of type "void".
 * In threadpool.c, you will use type conversion to coerce variables of type "threadpool" back and forth to a
 * richer, internal type.
*/
typedef void * threadpool;

/**
 * "dispatch_fn" declares a typed function pointer.  A variable of type "dispatch_fn" points to a function
 * with the following signature:
 */ 
typedef void (*dispatch_fn)(void *);

/**
 * init a fixed-sized thread pool.  If the function succeeds, it returns a (non-NULL)
 * "threadpool", else it returns NULL.
 */
threadpool tp_init_threadpool(uint8 num_threads_in_pool);


/**
 * dispatch sends a thread off to do some work.  If all threads in the pool are busy, dispatch will
 * block until a thread becomes free and is dispatched.
 * 
 * Once a thread is dispatched, this function returns immediately.
 * 
 * The dispatched thread calls into the function "dispatch_to_here" with argument "arg".
 */
void tp_dispatch(threadpool tpool, dispatch_fn dispatch_to_here,void *arg);

/**
 * destroy_threadpool kills the threadpool, causing all threads in it to commit suicide, and then
 * frees all the memory associated with the threadpool.
 */
void tp_destroy_threadpool(threadpool destroyme);

/**
 * create threads
 */
void tp_start_threadpool(threadpool tpool);

#endif

