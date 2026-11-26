#ifndef __HOST_STUB_LINUX_MUTEX_H
#define __HOST_STUB_LINUX_MUTEX_H

/* Single-threaded host tests: locks are no-ops. */
typedef struct {
    int unused;
} mutex_t;

#define DEFINE_MUTEX(name) mutex_t name = { 0 }
#define mutex_lock(m) ((void)(m))
#define mutex_unlock(m) ((void)(m))

#endif
