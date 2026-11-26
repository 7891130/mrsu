/*
 * Out-of-line implementation of the fake file layer declared in
 * stubs/linux/fs.h. One copy, shared by the test and kpm_core.c.
 */
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/types.h>

#include <stdlib.h>
#include <string.h>

static unsigned char *blob;
static size_t blob_len;
static int open_calls;

void host_fs_set_blob(const void *data, size_t len)
{
    free(blob);
    blob = malloc(len ? len : 1);
    if (len)
        memcpy(blob, data, len);
    blob_len = len;
    open_calls = 0;
}

int host_fs_opens(void)
{
    return open_calls;
}

struct file *filp_open(const char *path, int flags, int mode)
{
    static struct file fake;

    (void)path;
    (void)flags;
    (void)mode;

    open_calls++;
    fake.inode.i_size = (long long)blob_len;
    fake.pos = 0;
    return &fake;
}

struct inode *file_inode(struct file *f)
{
    return &f->inode;
}

loff_t i_size_read(const struct inode *inode)
{
    return inode->i_size;
}

long kernel_read(struct file *f, void *buf, size_t count, loff_t *pos)
{
    size_t offset = (size_t)(pos ? *pos : 0);
    size_t n = count;

    (void)f;
    if (offset >= blob_len)
        return 0;
    if (n > blob_len - offset)
        n = blob_len - offset;
    memcpy(buf, blob + offset, n);
    if (pos)
        *pos = (loff_t)(offset + n);
    return (long)n;
}

void filp_close(struct file *f, void *id)
{
    (void)f;
    (void)id;
}
