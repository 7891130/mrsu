#ifndef __HOST_STUB_LINUX_VMALLOC_H
#define __HOST_STUB_LINUX_VMALLOC_H

#include <linux/types.h>
#include <stdlib.h>

static inline void *vmalloc(size_t size)
{
    return malloc(size);
}
static inline void *vzalloc(size_t size)
{
    return calloc(1, size);
}
static inline void vfree(const void *p)
{
    free((void *)p);
}

#endif
