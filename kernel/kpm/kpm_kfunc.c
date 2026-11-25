#include <linux/cred.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "klog.h" // IWYU pragma: keep
#include "kpm/kpm_kfunc.h"
#include "kpm/kpm_symbol.h"

/*
 * Encoded kernel version, as kpatch exports it. KPMs compare it against
 * KERNEL_VERSION()-style values to pick code paths.
 */
u32 kpm_kver = LINUX_VERSION_CODE;

unsigned long kpm_kf_symbol_lookup_name(const char *name)
{
    return kpm_symbol_lookup(name);
}

unsigned long kpm_kf_kallsyms_lookup_name(const char *name)
{
    return kpm_symbol_lookup(name);
}

/*
 * kpatch returns the number of bytes copied; callers treat 0 as failure. We run
 * in a regular ioctl context, so the kernel primitive is used directly.
 */
int kpm_kf_compat_copy_to_user(void __user *to, const void *from, int n)
{
    if (n <= 0)
        return 0;
    if (!to || !from)
        return 0;
    if (copy_to_user(to, from, n))
        return 0;
    return n;
}

long kpm_kf_compat_strncpy_from_user(char *dest, const char __user *src, long count)
{
    if (!dest || !src || count <= 0)
        return -EINVAL;
    return strncpy_from_user(dest, src, count);
}

u32 kpm_kf_current_uid(void)
{
    return (u32)current_uid().val;
}

int kpm_kf_symbol_count(void)
{
    return (int)kpm_symbol_count();
}

int kpm_kf_symbol_list(char *out, int size)
{
    return kpm_symbol_list(out, size);
}

const char *kpm_kf_feature_list(void)
{
    return "kpm,selinux_hide,su_compat,kernel_umount,sulog,adb_root";
}
