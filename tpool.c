#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "tpool.h"

// Selected based on lowest CPU usage on my testing machine
#define HUNDREDTH_SECOND 10000000

typedef struct task_t
{
    job_f job;
    void *arg;
} task_t;

typedef struct node_t
{
    struct node_t *next;
    task_t        *job;
} node_t;

typedef struct queue_t
{
    node_t         *head;
    node_t         *tail;
    pthread_cond_t  bell;
    pthread_mutex_t lock;
} queue_t;

struct tpool_t
{
    size_t       workers;
    queue_t     *jobs;
    pthread_t   *tids;
};


static void     queue_append(queue_t *queue, task_t *job);
static queue_t *queue_create(void);
static void     queue_destroy(queue_t **queue);
static task_t  *queue_extract(queue_t *queue);
static void    *worker(void *);


static void *worker(void *data)
{
    queue_t *queue = (queue_t *)data;
    task_t *task = NULL;
    struct timespec delay = {0, 0};

    for (;;) {
        pthread_mutex_lock(&queue->lock);
        {
            clock_gettime(CLOCK_REALTIME, &delay);
            delay.tv_nsec += HUNDREDTH_SECOND;
            pthread_cond_timedwait(&queue->bell, &queue->lock, &delay);

            task = queue_extract(queue);
        }
        pthread_mutex_unlock(&queue->lock);

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
        perror("tpool_create: pool");
        errno = 0;
        return NULL;
    }

    pool->jobs = queue_create();
    if (!pool->jobs) {
        perror("tpool_create: queue");
        free(pool);
        errno = 0;
        return NULL;
    }

    pool->tids = calloc(workers, sizeof(pthread_t));
    if (!pool->tids) {
        perror("tpool_create: threads");
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

    task_t *task = calloc(1, sizeof(*task));
    if (!task) {
        perror("tpool_add_job");
        errno = 0;
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

static void queue_append(queue_t *queue, task_t *job)
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

// No mutex locking because worker already has lock on dequeue
static task_t *queue_extract(queue_t *queue)
{
    task_t *job = NULL;
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
    return job;
}
