/**
 * @file tpool.h
 * @author Kyle Matuszczak (kyle.l.matuszczak@gmail.com)
 * @brief A fully independent threadpool library. Library has no memory leaks
 *        internally, however, the user defined function in tpool_add_job() must
 *        clean up the arguements passed (if desired), and tpool_destroy() does
 *        no cleanup unless tpool_wait() was called first.
 * @version 0.1
 * @date 2022-06-12
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef TPOOL_H
#define TPOOL_H

#include <signal.h> /* for sig_atomic_t */

/**
 * @brief Relies on user defined global for thread syncronization shutdown
 */
extern volatile sig_atomic_t running;

typedef struct tpool_t tpool_t;
typedef void *(*job_f) (void *);

#define MAX_WORKERS 50

/**
 * @brief Creates threadpool of 1-MAX_WORKERS workers
 *
 * @param workers Number of workers
 * @return tpool_t* Thread pool
 */
tpool_t *tpool_create(size_t workers);

/**
 * @brief Adds job to queue
 *
 * @param pool The pool to add the job to
 * @param job Same function prototype as pthread_create
 * @param arg Arguement used by job
 * @return int 0 on success, -1 on error
 */
int tpool_add_job(tpool_t *pool, job_f job, void *arg);

/**
 * @brief Blocks until all threads are joined back in
 *
 * @param pool Thread pool to take action on
 */
void tpool_wait(tpool_t *pool);

/**
 * @brief Forcibly cancels all threads and joins them if still running.
 *
 * @param pool Thread pool to take action on
 */
void tpool_destroy(tpool_t **pool);

#endif
