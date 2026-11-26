#ifndef __HOST_STUB_LINUX_ERRNO_H
#define __HOST_STUB_LINUX_ERRNO_H

/*
 * Error numbers, spelled out here instead of including the host's <errno.h>.
 *
 * On glibc, <errno.h> pulls in bits/errno.h, which asks <linux/errno.h> for the
 * numbers -- and `-I stubs` makes *this* file the answer to that include.
 * Including <errno.h> from here would therefore close the recursion and leave
 * every E* undefined (which is exactly how the first Linux CI run failed, while
 * MinGW was happy because its <errno.h> is self-contained).
 *
 * The values are the generic Linux ones, i.e. what the kernel code expects.
 * The guard means a host libc that defines them first wins, so nothing here
 * fights the CRT.
 */
#ifndef EPERM

/* errno-base.h */
#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define ENOTBLK 15
#define EBUSY 16
#define EEXIST 17
#define EXDEV 18
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOTTY 25
#define ETXTBSY 26
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EMLINK 31
#define EPIPE 32
#define EDOM 33
#define ERANGE 34

/* errno.h */
#define EDEADLK 35
#define ENAMETOOLONG 36
#define ENOLCK 37
#define ENOSYS 38
#define ENOTEMPTY 39
#define ELOOP 40
#define ENOMSG 42
#define EIDRM 43
#define EOVERFLOW 75
#define ENOTSUP 95
#define EOPNOTSUPP 95
#define ETIMEDOUT 110
#define EALREADY 114
#define EINPROGRESS 115

#define EWOULDBLOCK EAGAIN
#define EDEADLOCK EDEADLK

#endif /* EPERM */

#endif
