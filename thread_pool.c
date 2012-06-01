/**
 * threadpool.c
 *
 * Example:
 * void dispatch_to_me(void *arg) {
 *      int seconds = (int) arg;
 *      do shomething here...
 * }
 *
 * int main(int argc, char **argv) 
 * {
 *    threadpool tp;
 * 
 *    tp = create_threadpool(7);
 *    for(;i < 16;i ++) {
 *       dispatch(tp, dispatch_to_me, (void *) arg);    
 *    }
 * 
 *    sleep(1);
 *    destroy_threadpool(tp);
 *    sleep(5);
 *    exit(-1);
 * }
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "thread_pool.h"

/* _threadpool is the internal threadpool structure that is
    cast to type "threadpool" before it given out to callers
 */
typedef struct work_st{
    void (*routine) (void*);
    void * arg;
    struct work_st* next;
} work_t;

/*
   you should fill in this structure with whatever you need
   queue is for holding jobs need to process by thread pool 
 */
typedef struct _threadpool_st {
    uint16          num_threads; /* number of threads in the pool     */
    uint16          qsize;       /* how many jobs in the queue        */
    pthread_t *     threads;     /* an array,pointer to threads       */
    work_t *        qhead;       /* queue head pointer                */
    work_t *        qtail;       /* queue tail pointer                */
    pthread_mutex_t qlock;       /* lock on the queue list            */
    pthread_cond_t  q_not_empty; /* non empty condidtion vairiables   */
    pthread_cond_t  q_empty;     /* empty condidtion vairiables       */
    uint8           shutdown;    /* TODO:Do I really need this to destory pool?  */
    uint8           dont_accept; /* TODO:Do I really need this to destory pool?  */
} _threadpool;

/* This function is the work function of the thread */
void * tp_working_thread(threadpool p)
{
    _threadpool * pool = (_threadpool *) p;
    work_t * cur;  /* The q element */

    while(1)  {
        //pool->qsize = pool->qsize;
        pthread_mutex_lock(&(pool->qlock));  /* get the q lock.  */

        while( pool->qsize == 0) {  /* if the size is 0 then wait. */
            if(pool->shutdown) {
                pthread_mutex_unlock(&(pool->qlock));
                pthread_exit(NULL);
            }
            
            /*  wait until the condition says its no emtpy and give up the lock.  */
            pthread_mutex_unlock(&(pool->qlock));  /* get the qlock. */
            pthread_cond_wait(&(pool->q_not_empty),&(pool->qlock));

            /*  check to see if in shutdown mode. */                         
            if(pool->shutdown) {
                pthread_mutex_unlock(&(pool->qlock));
                pthread_exit(NULL);
            }
       } /* while( pool->qsize == 0) */

       cur = pool->qhead;  /* set the cur variable. */

       pool->qsize--;    /* decriment the size. */

       if(pool->qsize == 0) {
           pool->qhead = NULL;
           pool->qtail = NULL;
       } else {
           pool->qhead = cur->next;
       }

       if(pool->qsize == 0 && ! pool->shutdown) {
           /* the q is empty again, now signal that its empty. */
           pthread_cond_signal(&(pool->q_empty));
       }
    
       pthread_mutex_unlock(&(pool->qlock));
       
       (cur->routine) (cur->arg);   /*  actually do work.      */
       free(cur);                   /*  free the work storage. */
       
    } /* end-while(1) */
}

threadpool tp_create_threadpool(uint8 num_threads_in_pool) 
{
    _threadpool *pool;
    uint8 i;
    int s;

    /* sanity check the argument */
    if ((num_threads_in_pool <= 0) || (num_threads_in_pool > MAXT_IN_POOL))  {
        return NULL;
    }

    pool = (_threadpool *) malloc(sizeof(_threadpool));
    if (pool == NULL) {
        fprintf(stderr, "Out of memory creating a new threadpool!\n");
        return NULL;
    }

    pool->threads = (pthread_t*) malloc (sizeof(pthread_t) * num_threads_in_pool);

    if(!pool->threads) {
        fprintf(stderr, "Out of memory creating a new threadpool!\n");
        return NULL;  
    }

    pool->num_threads = num_threads_in_pool; /*set up structure members */
    pool->qsize       = 0;
    pool->qhead       = NULL;
    pool->qtail       = NULL;
    pool->shutdown    = 0;
    pool->dont_accept = 0;

    /* initialize mutex and condition variables. */
    if(pthread_mutex_init(&pool->qlock,NULL))  {
        fprintf(stderr, "Mutex initiation error!\n");
        return NULL;
    }

    if(pthread_cond_init(&(pool->q_empty),NULL))   {
        fprintf(stderr, "CV initiation error!\n");  
        return NULL;
    }

    if(pthread_cond_init(&(pool->q_not_empty),NULL)) {
        fprintf(stderr, "CV initiation error!\n");  
        return NULL;
    }

    /* make threads */
    for (i = 0;i < num_threads_in_pool;i++)  {
        s = pthread_create(&(pool->threads[i]),NULL,tp_working_thread,pool);
        if (s != 0)  {
            handle_error_en(s, "pthread_create failed");
        }
    }

    return (threadpool) pool;
}

void tp_dispatch(threadpool tpool, dispatch_fn dispatch_to_here, void *arg) 
{
    _threadpool *pool = (_threadpool *) tpool;
    work_t * cur;

    /* make a work queue element.   */
    cur = (work_t*) malloc(sizeof(work_t));
    if(cur == NULL) {
        fprintf(stderr, "Out of memory creating a work struct!\n");
        return;  
    }

    cur->routine = dispatch_to_here;
    cur->arg     = arg;
    cur->next    = NULL;

    pthread_mutex_lock(&(pool->qlock));

    /* Just in case someone is trying to queue more */
    if(pool->dont_accept) {
        free(cur);
        return;
    }
    
    if(pool->qsize == 0) {
        pool->qhead = cur;  /* set to only one */
        pool->qtail = cur;
        
        /* I am not empty now */
        pthread_cond_signal(&(pool->q_not_empty));
    } else {
        pool->qtail->next = cur;
        pool->qtail = cur;      
    }
  
    pool->qsize++;
    pthread_mutex_unlock(&(pool->qlock));
}

void tp_destroy_threadpool(threadpool destroyme)
{
    _threadpool *pool = (_threadpool *) destroyme;
    
    void* nothing;
    uint8 i;

    pthread_mutex_lock(&(pool->qlock));
    pool->dont_accept = 1;
    
    while(pool->qsize != 0) {
        pthread_cond_wait(&(pool->q_empty),&(pool->qlock));  /* wait until the q is empty. */
    }

    pool->shutdown = 1;                            /* allow shutdown */
    pthread_cond_broadcast(&(pool->q_not_empty));  /* allow code to return NULL; */
    pthread_mutex_unlock(&(pool->qlock));

    /* kill everything. */
    for(i = 0; i < pool->num_threads; i++) {
        pthread_cond_broadcast(&(pool->q_not_empty));
    
        /* allowcode to return NULL */
        pthread_join(pool->threads[i],&nothing);
    }

    free(pool->threads);

    pthread_mutex_destroy(&(pool->qlock));
    pthread_cond_destroy(&(pool->q_empty));
    pthread_cond_destroy(&(pool->q_not_empty));

    return;
}


