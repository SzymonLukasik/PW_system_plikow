#include <pthread.h>
#include "err.h"
#include "rwlock.h"

#define pthread_exec(expr) do { \
    int errno; \
    if ((errno = (expr)) != 0) syserr(errno, "Error in pthread"); \
} while (0)

void rwlock_init(rwlock* rw) {
    pthread_exec(pthread_mutex_init(&rw->lock, 0));
    pthread_exec(pthread_cond_init(&rw->readers, 0));
    pthread_exec(pthread_cond_init(&rw->writers, 0));
    rw->rcount = 0;
    rw->wcount = 0;
    rw->rwait = 0;
    rw->wwait = 0;
    rw->change = NONE;
}

void rwlock_destroy(rwlock *rw) {
    pthread_exec(pthread_mutex_destroy (&rw->lock));
    pthread_exec(pthread_cond_destroy (&rw->readers));
    pthread_exec(pthread_cond_destroy (&rw->writers));
}

void rwlock_wrlock(rwlock* rw) {
    pthread_exec(pthread_mutex_lock(&rw->lock));
    rw->wwait++;
    while (rw->wcount + rw->rcount > 0 && rw->change != WRITERS)
        pthread_exec(pthread_cond_wait(&rw->writers, &rw->lock));
    rw->wwait--;
    rw->change = NONE;
    rw->wcount++;
    pthread_exec(pthread_mutex_unlock(&rw->lock));
}

void rwlock_wrunlock(rwlock* rw) {
    pthread_exec(pthread_mutex_lock(&rw->lock));
    rw->wcount--;
    if (rw->rwait > 0) {
        rw->change = READERS;
        pthread_exec(pthread_mutex_unlock(&rw->lock));
        pthread_exec(pthread_cond_signal(&rw->readers));
    } else if (rw->wwait > 0) {
        pthread_exec(pthread_mutex_unlock(&rw->lock));
        pthread_exec(pthread_cond_signal(&rw->writers));
    } else pthread_exec(pthread_mutex_unlock(&rw->lock));
}


void rwlock_rdlock(rwlock* rw) {
    pthread_exec(pthread_mutex_lock(&rw->lock));
    rw->rwait++;
    while (rw->wcount + rw->wwait > 0 && rw->change != READERS)
        pthread_exec(pthread_cond_wait(&rw->readers, &rw->lock));
    rw->rwait--;
    rw->change = NONE;
    rw->rcount++;
    if (rw->rwait > 0) {
        pthread_exec(pthread_mutex_unlock(&rw->lock));
        pthread_exec(pthread_cond_signal(&rw->readers));
    } else pthread_exec(pthread_mutex_unlock(&rw->lock));
}

void rwlock_rdunlock(rwlock* rw) {
    pthread_exec(pthread_mutex_lock(&rw->lock));
    rw->rcount--;
    if (rw->rcount == 0 && rw->wwait > 0) {
        rw->change = WRITERS;
        pthread_exec(pthread_mutex_unlock(&rw->lock));
        pthread_exec(pthread_cond_signal(&rw->writers));
    } else pthread_exec(pthread_mutex_unlock(&rw->lock));
}
