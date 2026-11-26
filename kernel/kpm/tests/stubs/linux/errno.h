#ifndef __HOST_STUB_LINUX_ERRNO_H
#define __HOST_STUB_LINUX_ERRNO_H
/* The kernel's errno.h is a thin wrapper over the uapi one; on the host the
   libc values are what the tests compare against. */
#include <errno.h>
#endif
