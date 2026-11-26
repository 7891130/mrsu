#ifndef __HOST_STUB_LINUX_CRED_H
#define __HOST_STUB_LINUX_CRED_H

#include <linux/types.h>

/* Defined by the test so both translation units observe the same value. */
extern u32 host_current_uid_value;

typedef struct {
    u32 val;
} kuid_t;

static inline kuid_t current_uid(void)
{
    kuid_t k = { host_current_uid_value };
    return k;
}

#endif
