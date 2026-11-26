#ifndef __HOST_STUB_LINUX_TYPES_H
#define __HOST_STUB_LINUX_TYPES_H

/* Minimal kernel type stand-ins for host-side unit tests. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef unsigned long uintptr_t_stub;
/* The kernel's loff_t is `long long`, not int64_t: on LP64 hosts those are
   different types, and a `long long *` prototype must not clash with it. */
typedef long long loff_t;
typedef int64_t off_t_stub;

/* uapi/ headers use the double-underscore names. */
typedef int8_t __s8;
typedef uint8_t __u8;
typedef int16_t __s16;
typedef uint16_t __u16;
typedef int32_t __s32;
typedef uint32_t __u32;
typedef int64_t __s64;
typedef uint64_t __u64;
typedef __u16 __le16;
typedef __u16 __be16;
typedef __u32 __le32;
typedef __u32 __be32;
typedef __u64 __le64;
typedef __u64 __be64;
typedef __u64 __aligned_u64;
typedef __u32 __kernel_dev_t;
typedef __u16 umode_t;
/* glibc's <sys/types.h> -- pulled in by <stdio.h> and friends -- defines dev_t
   as an unsigned long. A conflicting retypedef is an error, so stand down. */
#ifndef __dev_t_defined
typedef __u32 dev_t;
#endif
/* ssize_t only exists once <sys/types.h> has been seen, which is not a given;
   the kernel gets it from <linux/types.h>. */
#ifndef __ssize_t_defined
typedef long ssize_t;
#endif

#endif
