#ifndef __HOST_STUB_LINUX_KERNEL_H
#define __HOST_STUB_LINUX_KERNEL_H

#include <errno.h>
#include <linux/types.h>
#include <stdio.h>
#include <string.h>

/* NOTE: MinGW is LLP64, so `unsigned long` is 32-bit here while the kernel's
   1UL is 64-bit. Pinning BIT() to a 64-bit type keeps the harness faithful. */
#define BIT(nr) ((u64)1 << (nr))

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ALIGN(x, a) (((x) + ((typeof(x))(a) - 1)) & ~((typeof(x))(a) - 1))

/* compiler_types.h defines these as empty for normal kernel builds. */
#ifndef __user
#define __user
#define __iomem
#define __kernel
#endif

/* compiler_attributes.h provides this keyword wrapper. */
#ifndef fallthrough
#define fallthrough __attribute__((__fallthrough__))
#endif

#define pr_err(fmt, ...) printf("pr_err: " fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...) printf("pr_warn: " fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...) printf("pr_info: " fmt, ##__VA_ARGS__)

#endif