#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "tpool.h"

#define MAX_WORKERS 50
#define TENTH_SECOND 100000000

typedef struct job_t
{
    job_f job;
    void *arg;
} job_t;

typedef struct node_t
{
    struct node_t *next;
    job_t *job;
} node_t;

typedef struct queue_t
{
    node_t *head;
    node_t *tail;
    pthread_cond_t bell;
    pthread_mutex_t lock;
} queue_t;

struct tpool_t
{
    size_t workers;
    queue_t *jobs;
    pthread_t *tids;
};

static queue_t *queue_create(void);
static void queue_destroy(queue_t **queue);
static void queue_append(queue_t *queue, job_t *job);
static job_t *queue_extract(queue_t *queue);
static void *worker(void *);

static void *worker(void *data)
{
    queue_t *queue = (queue_t *)data;
    job_t *task = NULL;
     struct timespec delay = {0, 0};

    for (;;) {
        /*
        Only a single thread can acquire a lock at once, however, the moment
        timedwait is reached, the thread that had the lock releases it. This
        allows another thread to acquire lock, enter, and block on condition.
        This continues until one of 2 things occur:
        1) A signal comes in, then one/all threads that recieved the signal will
           try to reaquire the lock.
        2) Their personal timer ran out and they attempt to reaquire lock in a
           FIFO basis.
        */
        pthread_mutex_lock(&queue->lock);
        clock_gettime(CLOCK_REALTIME, &delay);
        delay.tv_nsec += TENTH_SECOND;
        pthread_cond_timedwait(&queue->bell, &queue->lock, &delay);
        pthread_mutex_unlock(&queue->lock);

        task = queue_extract(queue);
        if (task) {
            task->job(task->arg);
            free(task);
        } else if (!running) {
            break;
        }
    }
    return NULL;
}

tpool_t *tpool_create(size_t workers)
{
    if (!workers || MAX_WORKERS < workers) {
        fprintf(stderr, "Workers must be in range 1-%d\n", MAX_WORKERS);
        return NULL;
    }

    tpool_t *pool = calloc(1, sizeof(*pool));
    if (!pool) {
        perror("tpool_create");
        errno = 0;
        return NULL;
    }

    pool->jobs = queue_create();
    if (!pool->jobs) {
        perror("tpool_create");
        free(pool);
        errno = 0;
        return NULL;
    }

    pool->tids = calloc(workers, sizeof(pthread_t));
    if (!pool->tids) {
        perror("tpool_create");
        free(pool->jobs);
        free(pool);
        errno = 0;
        return NULL;
    }

    pool->workers = workers;
    for (size_t id = 0; id < workers; ++id) {
        pthread_create(pool->tids + id, NULL, worker, pool->jobs);
    }

    return pool;
}

int tpool_add_job(tpool_t *pool, job_f job, void *arg)
{
    if (!pool || !job) {
        return -1;
    }

    job_t *task = calloc(1, sizeof(*task));
    if (!task) {
        perror("tpool_add_job");
        return -1;
    }
    task->job = job;
    task->arg = arg;

    queue_append(pool->jobs, task);
    pthread_cond_signal(&pool->jobs->bell);
    return 0;
}

void tpool_wait(tpool_t *pool)
{
    if (!pool) {
        return;
    }

    pthread_cond_broadcast(&pool->jobs->bell);
    for (size_t i = 0; i < pool->workers; ++i) {
        pthread_join(pool->tids[i], NULL);
        pool->tids[i] = 0;
    }
}

void tpool_destroy(tpool_t **pool)
{
    if (!pool || !*pool) {
        return;
    }

    pthread_cond_broadcast(&(*pool)->jobs->bell);
    for (size_t i = 0; i < (*pool)->workers; ++i) {
        if ((*pool)->tids[i]) {
            pthread_cancel((*pool)->tids[i]);
            pthread_join((*pool)->tids[i], NULL);
        }
    }

    queue_destroy(&(*pool)->jobs);
    free((*pool)->tids);
    free(*pool);
    *pool = NULL;
}


static queue_t *queue_create(void)
{
    queue_t *queue = calloc(1, sizeof(*queue));
    if (queue) {
        pthread_mutex_init(&queue->lock, NULL);
        pthread_cond_init(&queue->bell, NULL);
    }
    return queue;
}

static void queue_destroy(queue_t **queue)
{
    node_t *node = (*queue)->head;
    node_t *temp = NULL;
    pthread_mutex_lock(&(*queue)->lock);
    {
        while (node) {
            temp = node;
            node = node->next;

            free(temp->job);
            temp->next = NULL;
            free(temp);
        }
    }
    pthread_mutex_unlock(&(*queue)->lock);
    pthread_mutex_destroy(&(*queue)->lock);
    pthread_cond_destroy(&(*queue)->bell);
    (*queue)->head = NULL;
    (*queue)->tail = NULL;
    free(*queue);
    *queue = NULL;
}

static void queue_append(queue_t *queue, job_t *job)
{
    struct node_t *node = calloc(1, sizeof(*node));
    if (!node) {
        return;
    }

    node->job = job;
    node->next = NULL;

    pthread_mutex_lock(&queue->lock);
    {
        if (queue->tail) {
            queue->tail->next = node;
        } else {
            queue->head = node;
        }

        queue->tail = node;
    }
    pthread_mutex_unlock(&queue->lock);
}

static job_t *queue_extract(queue_t *queue)
{
    job_t *job = NULL;
    pthread_mutex_lock(&queue->lock);
    {
        if (queue->head) {
            node_t *temp = queue->head;
            job = temp->job;
            queue->head = temp->next;
            if (!queue->head) {
                queue->tail = NULL;
            }
            temp->next = NULL;
            temp->job = NULL;
            free(temp);
        }
    }
    pthread_mutex_unlock(&queue->lock);
    return job;
}
