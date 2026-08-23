#ifndef LASTOS_FS_PTHREAD_H
#define LASTOS_FS_PTHREAD_H

#include "stdint.h"
#include "stddef.h"
#include "time.h"

typedef unsigned long pthread_t;
typedef struct { int __val; } pthread_mutex_t;
typedef struct { int __val; } pthread_mutexattr_t;
typedef struct { int __val; } pthread_cond_t;
typedef struct { int __val; } pthread_condattr_t;

#define PTHREAD_MUTEX_INITIALIZER {0}
#define PTHREAD_COND_INITIALIZER  {0}

static inline int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) { (void)m; (void)a; return 0; }
static inline int pthread_mutex_lock(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_destroy(pthread_mutex_t *m) { (void)m; return 0; }

static inline int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) { (void)c; (void)a; return 0; }
static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { (void)c; (void)m; return 0; }
static inline int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abstime) {
    (void)c; (void)m; (void)abstime; return 0;
}
static inline int pthread_cond_signal(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_cond_broadcast(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }

#endif /* LASTOS_FS_PTHREAD_H */
