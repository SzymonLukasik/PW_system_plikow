#pragma  once

#include <pthread.h>
#include "err.h"

enum rw_change {
    NONE, READERS, WRITERS
};

typedef enum rw_change rw_change;

struct rwlock {
    pthread_mutex_t lock;
    pthread_cond_t readers;
    pthread_cond_t writers;
    int rcount, wcount, rwait, wwait;
    rw_change change;
};

typedef struct rwlock rwlock;

void rwlock_init(rwlock*);

void rwlock_destroy(rwlock*);

void rwlock_wrlock(rwlock*);

void rwlock_wrunlock(rwlock*);

void rwlock_rdlock(rwlock*);

void rwlock_rdunlock(rwlock*);