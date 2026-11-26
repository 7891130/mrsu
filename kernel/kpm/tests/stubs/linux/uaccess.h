#ifndef __HOST_STUB_LINUX_UACCESS_H
#define __HOST_STUB_LINUX_UACCESS_H

#include <linux/err.h>
#include <linux/types.h>

#include <string.h>

#define __user
#define __kernel
#define __iomem

/* Set by the test to make the copy helpers report a fault. */
extern int host_uaccess_fault;

static inline int copy_to_user(void *to, const void *from, unsigned long n)
{
    if (host_uaccess_fault || !to || !from)
        return (int)n; /* bytes not copied */
    memcpy(to, from, n);
    return 0;
}

static inline long strncpy_from_user(char *dest, const char *src, long count)
{
    size_t len;

    if (host_uaccess_fault)
        return -EFAULT;
    if (!dest || !src || count <= 0)
        return -EINVAL;

    len = strlen(src);
    if (len >= (size_t)count)
        return -ENAMETOOLONG;

    memcpy(dest, src, len + 1);
    return (long)len;
}

#endif
