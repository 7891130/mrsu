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

/*
 * dev_t / loff_t / ssize_t are names the kernel shares with the host libc.
 * glibc's <sys/types.h> defines them as `unsigned long`, `long` and `long`, and
 * it can be pulled in before or after this header (<stdio.h> and <stdlib.h> do
 * it), so "define only if absent" is not enough -- both orders happened in CI.
 *
 * C11 allows a typedef to be repeated as long as it denotes the same type, so
 * on a POSIX host the stub uses the libc's own underlying types instead of the
 * kernel's `__u32`/`long long`. Nothing in the suites cares about the
 * difference: dev_t is unused, and loff_t is only ever a 64-bit file offset.
 */
#if defined(_WIN32)
typedef __u32 dev_t;
typedef long long loff_t;
#else
typedef unsigned long dev_t; /* __dev_t */
typedef long loff_t; /* __loff_t */
#endif

#ifndef __ssize_t_defined
typedef long ssize_t;
#endif

#endif
