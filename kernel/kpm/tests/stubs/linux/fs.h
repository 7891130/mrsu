#ifndef __HOST_STUB_LINUX_FS_H
#define __HOST_STUB_LINUX_FS_H

/*
 * Fake file layer for host tests of kpm_core.c.
 *
 * kpm_core only opens a path, asks for its size and reads it into a buffer, so
 * one in-memory blob registered through host_fs_set_blob() is enough.
 *
 * The state lives in host_fs.c rather than in this header: the header is
 * included by both the test and the kernel source, and `static` globals would
 * give each translation unit its own copy.
 */
#include <linux/err.h>
#include <linux/types.h>

#define O_RDONLY 0

struct inode {
    long long i_size;
};

struct file {
    struct inode inode;
    unsigned long long pos;
};

void host_fs_set_blob(const void *data, size_t len);
int host_fs_opens(void);

struct file *filp_open(const char *path, int flags, int mode);
struct inode *file_inode(struct file *f);
loff_t i_size_read(const struct inode *inode);
long kernel_read(struct file *f, void *buf, size_t count, loff_t *pos);
void filp_close(struct file *f, void *id);

#endif
