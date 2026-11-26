#ifndef __HOST_STUB_KLOG_H
#define __HOST_STUB_KLOG_H

/* Stub for the kernel's kernel/include/klog.h during host-side tests. */
#include <linux/kernel.h>

#define pr_fmt(fmt) "KernelSU: " fmt

#endif
