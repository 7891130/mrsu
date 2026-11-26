#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "klog.h" // IWYU pragma: keep
#include "kpm/kpm.h"
#include "kpm/kpm_provider.h"
#include "kpm/kpm_table.h"
#include "policy/feature.h"

/*
 * Routing layer between KernelSU's control channel (KSU_IOCTL_KPM, boot events)
 * and the module table.
 *
 * The table itself lives in kpm_table.c and knows nothing about KernelSU, so it
 * can be linked into kpm.ko as well. Whoever owns it -- kernelsu.ko by default,
 * or a kpm.ko that registered its ops -- receives the commands. This file keeps
 * the two things that must stay KernelSU's regardless of the owner: the feature
 * gate and the ioctl entry point.
 */
static bool kpm_enabled = true;

/* NULL means "serve from the table inside kernelsu.ko". */
static const struct kpm_ops *kpm_owner;
static DEFINE_MUTEX(kpm_owner_mutex);

/**
 * ksu_kpm_register_ops - take over the KPM control channel
 * @ops: ops implemented by the registering module (kpm.ko)
 *
 * Refused with -EBUSY when an owner is already registered, and also when the
 * built-in table already holds modules: two live tables would silently
 * diverge, and the modules in the built-in one would become invisible.
 *
 * Return: 0 on success, -EINVAL for a malformed ops struct, -EBUSY as above.
 */
int ksu_kpm_register_ops(const struct kpm_ops *ops)
{
    int ret = 0;

    if (!ops || ops->magic != KPM_OPS_MAGIC || ops->version != KPM_OPS_VERSION || !ops->name || !ops->load ||
        !ops->unload || !ops->list || !ops->info)
        return -EINVAL;

    mutex_lock(&kpm_owner_mutex);
    if (kpm_owner) {
        pr_warn("kpm: %s cannot take the control channel, %s holds it\n", ops->name, kpm_owner->name);
        ret = -EBUSY;
    } else if (!kpm_table_is_empty()) {
        pr_warn("kpm: %s cannot take the control channel, the built-in table is in use\n", ops->name);
        ret = -EBUSY;
    } else {
        kpm_owner = ops;
        pr_info("kpm: control channel now served by %s\n", ops->name);
    }
    mutex_unlock(&kpm_owner_mutex);

    return ret;
}

/**
 * ksu_kpm_unregister_ops - give the control channel back
 * @ops: the owner to remove; NULL drops whoever is registered
 *
 * Called from the owner's module_exit, so the mutex guarantees no command is
 * still running inside the (about to disappear) module when this returns.
 */
void ksu_kpm_unregister_ops(const struct kpm_ops *ops)
{
    mutex_lock(&kpm_owner_mutex);
    if (kpm_owner && (!ops || kpm_owner == ops)) {
        pr_info("kpm: %s released the control channel\n", kpm_owner->name);
        kpm_owner = NULL;
    }
    mutex_unlock(&kpm_owner_mutex);
}

int kpm_load_path(const char *path, const char *args, char *err, size_t err_len)
{
    const struct kpm_ops *ops;
    int ret;

    if (!kpm_enabled) {
        kpm_set_err(err, err_len, "kpm feature is disabled");
        return -EPERM;
    }

    mutex_lock(&kpm_owner_mutex);
    ops = kpm_owner;
    ret = ops ? ops->load(path, args, err, err_len) : kpm_table_load(path, args, err, err_len);
    mutex_unlock(&kpm_owner_mutex);

    return ret;
}

int kpm_unload(const char *name)
{
    const struct kpm_ops *ops;
    int ret;

    mutex_lock(&kpm_owner_mutex);
    ops = kpm_owner;
    ret = ops ? ops->unload(name) : kpm_table_unload(name);
    mutex_unlock(&kpm_owner_mutex);

    return ret;
}

int kpm_list(char *out, size_t size)
{
    const struct kpm_ops *ops;
    int ret;

    mutex_lock(&kpm_owner_mutex);
    ops = kpm_owner;
    ret = ops ? ops->list(out, size) : kpm_table_list(out, size);
    mutex_unlock(&kpm_owner_mutex);

    return ret;
}

int kpm_info(const char *name, char *out, size_t size)
{
    const struct kpm_ops *ops;
    int ret;

    mutex_lock(&kpm_owner_mutex);
    ops = kpm_owner;
    ret = ops ? ops->info(name, out, size) : kpm_table_info(name, out, size);
    mutex_unlock(&kpm_owner_mutex);

    return ret;
}

long kpm_ctl0(const char *name, const char *args, char __user *out, int outlen)
{
    const struct kpm_ops *ops;
    long ret;

    mutex_lock(&kpm_owner_mutex);
    ops = kpm_owner;
    ret = ops ? ops->ctl0(name, args, out, outlen) : kpm_table_ctl0(name, args, out, outlen);
    mutex_unlock(&kpm_owner_mutex);

    return ret;
}

long kpm_ctl1(const char *name, void *a1, void *a2, void *a3)
{
    const struct kpm_ops *ops;
    long ret;

    mutex_lock(&kpm_owner_mutex);
    ops = kpm_owner;
    ret = ops ? ops->ctl1(name, a1, a2, a3) : kpm_table_ctl1(name, a1, a2, a3);
    mutex_unlock(&kpm_owner_mutex);

    return ret;
}

int kpm_notify_event(const char *event, const char *args)
{
    const struct kpm_ops *ops;
    int ret;

    mutex_lock(&kpm_owner_mutex);
    ops = kpm_owner;
    if (ops && ops->notify_event)
        ret = ops->notify_event(event, args);
    else
        ret = kpm_table_notify_event(event, args);
    mutex_unlock(&kpm_owner_mutex);

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
    /* Any registered owner lives in another module: drop the pointer first. */
    ksu_kpm_unregister_ops(NULL);
    kpm_table_release_all();

    ksu_unregister_feature_handler(KSU_FEATURE_KPM);
}
