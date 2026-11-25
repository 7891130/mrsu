#include <linux/err.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "klog.h" // IWYU pragma: keep
#include "kpm/kpm.h"
#include "policy/feature.h"
#include "uapi/ksu.h"

#define KPM_MAX_IMAGE KSU_KPM_MAX_IMAGE

static LIST_HEAD(kpm_modules);
static DEFINE_MUTEX(kpm_mutex);
static bool kpm_enabled = true;

static void kpm_set_err(char *err, size_t err_len, const char *msg)
{
    if (err && err_len)
        snprintf(err, err_len, "%s", msg);
}

static struct kpm_module *kpm_find_locked(const char *name)
{
    struct kpm_module *pos;

    list_for_each_entry(pos, &kpm_modules, list) {
        if (!strcmp(pos->name, name))
            return pos;
    }
    return NULL;
}

static bool kpm_name_exists(const char *name)
{
    bool exists;

    mutex_lock(&kpm_mutex);
    exists = kpm_find_locked(name) != NULL;
    mutex_unlock(&kpm_mutex);

    return exists;
}

static void kpm_release_locked(struct kpm_module *mod)
{
    if (mod->exit) {
        pr_info("kpm: calling exit of %s\n", mod->name);
        mod->exit(NULL);
    }
    kpm_elf_release(mod);
    kfree(mod->args);
    kfree(mod);
}

int kpm_load_path(const char *path, const char *args, char *err, size_t err_len)
{
    struct kpm_module *mod = NULL;
    struct file *filp;
    void *buf = NULL;
    loff_t size, pos = 0;
    ssize_t read;
    int ret;

    if (!kpm_enabled) {
        kpm_set_err(err, err_len, "kpm feature is disabled");
        return -EPERM;
    }
    if (!path || path[0] != '/') {
        kpm_set_err(err, err_len, "invalid module path");
        return -EINVAL;
    }
    if (args && strlen(args) >= KPM_ARGS_MAX) {
        kpm_set_err(err, err_len, "module args too long");
        return -EINVAL;
    }

    filp = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(filp)) {
        ret = PTR_ERR(filp);
        kpm_set_err(err, err_len, "open module file failed");
        return ret;
    }

    size = i_size_read(file_inode(filp));
    if (size <= 0 || size > KPM_MAX_IMAGE) {
        ret = -EFBIG;
        kpm_set_err(err, err_len, "module file size is invalid");
        goto out_close;
    }

    buf = vmalloc(size);
    if (!buf) {
        ret = -ENOMEM;
        kpm_set_err(err, err_len, "allocate module buffer failed");
        goto out_close;
    }

    read = kernel_read(filp, buf, size, &pos);
    if (read != size) {
        ret = -EIO;
        kpm_set_err(err, err_len, "read module file failed");
        goto out_free;
    }
    filp_close(filp, NULL);
    filp = NULL;

    mod = kzalloc(sizeof(*mod), GFP_KERNEL);
    if (!mod) {
        ret = -ENOMEM;
        kpm_set_err(err, err_len, "allocate module state failed");
        goto out_free;
    }

    ret = kpm_elf_load(mod, buf, size, args, kpm_name_exists, err, err_len);
    if (ret) {
        kfree(mod->args);
        kfree(mod);
        goto out_free;
    }

    mutex_lock(&kpm_mutex);
    if (kpm_find_locked(mod->name)) {
        mutex_unlock(&kpm_mutex);
        ret = -EEXIST;
        kpm_set_err(err, err_len, "module already loaded");
        /* Lost a race with another load: undo what init already did. */
        if (mod->exit)
            mod->exit(NULL);
        kpm_elf_release(mod);
        kfree(mod->args);
        kfree(mod);
        goto out_free;
    }
    list_add_tail(&mod->list, &kpm_modules);
    mutex_unlock(&kpm_mutex);

    pr_info("kpm: loaded %s (%s)\n", mod->name, mod->version);
    vfree(buf);
    return 0;

out_free:
    vfree(buf);
out_close:
    if (filp && !IS_ERR(filp))
        filp_close(filp, NULL);
    return ret;
}

int kpm_unload(const char *name)
{
    struct kpm_module *mod;

    if (!name || !name[0])
        return -EINVAL;

    mutex_lock(&kpm_mutex);
    mod = kpm_find_locked(name);
    if (!mod) {
        mutex_unlock(&kpm_mutex);
        return -ENOENT;
    }
    list_del(&mod->list);
    mutex_unlock(&kpm_mutex);

    pr_info("kpm: unloading %s\n", mod->name);
    kpm_release_locked(mod);
    return 0;
}

int kpm_list(char *out, size_t size)
{
    struct kpm_module *pos;
    size_t off = 0;

    if (!out || !size)
        return -EINVAL;
    out[0] = '\0';

    mutex_lock(&kpm_mutex);
    list_for_each_entry(pos, &kpm_modules, list) {
        int n = snprintf(out + off, size - off, "%s\t%s\t%s\n", pos->name, pos->version,
                         pos->args ? pos->args : "");
        if (n < 0 || (size_t)n >= size - off)
            break;
        off += n;
    }
    mutex_unlock(&kpm_mutex);

    return (int)off;
}

int kpm_info(const char *name, char *out, size_t size)
{
    struct kpm_module *mod;
    int n;

    if (!name || !name[0] || !out || !size)
        return -EINVAL;
    out[0] = '\0';

    mutex_lock(&kpm_mutex);
    mod = kpm_find_locked(name);
    if (!mod) {
        mutex_unlock(&kpm_mutex);
        return -ENOENT;
    }
    n = snprintf(out, size,
                 "name=%s\n"
                 "version=%s\n"
                 "license=%s\n"
                 "author=%s\n"
                 "description=%s\n"
                 "args=%s\n"
                 "has_ctl0=%d\n"
                 "has_ctl1=%d\n"
                 "has_event=%d\n",
                 mod->name, mod->version, mod->license, mod->author, mod->description,
                 mod->args ? mod->args : "", mod->ctl0 ? 1 : 0, mod->ctl1 ? 1 : 0, mod->event ? 1 : 0);
    mutex_unlock(&kpm_mutex);

    return n < 0 ? n : (int)min((size_t)n, size - 1);
}

long kpm_ctl0(const char *name, const char *args, char __user *out, int outlen)
{
    struct kpm_module *mod;
    long ret;

    if (!name || !name[0] || !args)
        return -EINVAL;

    mutex_lock(&kpm_mutex);
    mod = kpm_find_locked(name);
    if (!mod) {
        mutex_unlock(&kpm_mutex);
        return -ENOENT;
    }
    if (!mod->ctl0) {
        mutex_unlock(&kpm_mutex);
        return -ENOSYS;
    }
    ret = mod->ctl0(args, out, outlen);
    mutex_unlock(&kpm_mutex);

    return ret;
}

long kpm_ctl1(const char *name, void *a1, void *a2, void *a3)
{
    struct kpm_module *mod;
    long ret;

    if (!name || !name[0])
        return -EINVAL;

    mutex_lock(&kpm_mutex);
    mod = kpm_find_locked(name);
    if (!mod) {
        mutex_unlock(&kpm_mutex);
        return -ENOENT;
    }
    if (!mod->ctl1) {
        mutex_unlock(&kpm_mutex);
        return -ENOSYS;
    }
    ret = mod->ctl1(a1, a2, a3);
    mutex_unlock(&kpm_mutex);

    return ret;
}

static int kpm_feature_get(u64 *value)
{
    *value = kpm_enabled ? 1 : 0;
    return 0;
}

static int kpm_feature_set(u64 value)
{
    kpm_enabled = value != 0;
    pr_info("kpm: feature set to %d\n", kpm_enabled ? 1 : 0);
    return 0;
}

static const struct ksu_feature_handler kpm_feature_handler = {
    .feature_id = KSU_FEATURE_KPM,
    .name = "kpm",
    .get_handler = kpm_feature_get,
    .set_handler = kpm_feature_set,
};

void __init kpm_runtime_init(void)
{
    if (ksu_register_feature_handler(&kpm_feature_handler))
        pr_err("kpm: failed to register feature handler\n");
}

void __exit kpm_runtime_exit(void)
{
    struct kpm_module *pos, *n;

    mutex_lock(&kpm_mutex);
    list_for_each_entry_safe(pos, n, &kpm_modules, list) {
        list_del(&pos->list);
        kpm_release_locked(pos);
    }
    mutex_unlock(&kpm_mutex);

    ksu_unregister_feature_handler(KSU_FEATURE_KPM);
}

int kpm_notify_event(const char *event, const char *args)
{
    struct kpm_module *pos;
    int count = 0;
    int result = 0;

    if (!event || !event[0])
        return -EINVAL;

    mutex_lock(&kpm_mutex);
    list_for_each_entry(pos, &kpm_modules, list) {
        int rc;

        if (!pos->event)
            continue;

        rc = pos->event(event, args, NULL);
        if (rc < 0 && !result)
            result = rc;
        count++;
    }
    mutex_unlock(&kpm_mutex);

    return result ? result : count;
}
