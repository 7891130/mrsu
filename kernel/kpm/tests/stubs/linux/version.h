#ifndef __HOST_STUB_LINUX_VERSION_H
#define __HOST_STUB_LINUX_VERSION_H

#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + ((c) > 255 ? 255 : (c)))
#define LINUX_VERSION_CODE KERNEL_VERSION(6, 1, 0)

#endif
