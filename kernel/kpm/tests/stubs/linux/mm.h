#ifndef __HOST_STUB_LINUX_MM_H
#define __HOST_STUB_LINUX_MM_H

#include <linux/types.h>

#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define PAGE_MASK (~(PAGE_SIZE - 1))
#define PAGE_ALIGN(x) (((x) + PAGE_SIZE - 1) & PAGE_MASK)

#endif
