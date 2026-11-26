#ifndef __HOST_STUB_LINUX_SLAB_H
#define __HOST_STUB_LINUX_SLAB_H

#include <linux/types.h>
#include <stdlib.h>
#include <string.h>

#define GFP_KERNEL 0
#define GFP_ATOMIC 0
#define __GFP_ZERO 0

static inline void *kmalloc(size_t size, int flags)
{
    (void)flags;
    return malloc(size);
}

static inline void *kzalloc(size_t size, int flags)
{
    (void)flags;
    return calloc(1, size);
}

static inline void *kmalloc_array(size_t n, size_t size, int flags)
{
    (void)flags;
    return calloc(n, size);
}

static inline void kfree(const void *p)
{
    free((void *)p);
}

static inline char *kstrdup(const char *s, int flags)
{
    (void)flags;
    return s ? strdup(s) : NULL;
}

#define kvfree(p) kfree(p)

#endif
