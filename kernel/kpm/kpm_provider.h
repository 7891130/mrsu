#ifndef __KSU_H_KPM_PROVIDER
#define __KSU_H_KPM_PROVIDER

#include <linux/types.h>
#include <linux/uaccess.h>

/*
 * Cross-module control channel for the KPM runtime.
 *
 * The runtime can live in two places:
 *
 *   - inside kernelsu.ko (the built-in table), and
 *   - inside kpm.ko, a separate module that a KernelSU module insmods at boot.
 *
 * Either way userspace keeps using KSU_IOCTL_KPM. kernelsu.ko receives the
 * ioctl, applies the "kpm" feature gate and forwards the command to whichever
 * owner registered itself here; with no owner registered it serves the command
 * from its own table. That keeps one single control path and one single table
 * of loaded modules -- two live tables would silently diverge.
 *
 * kpm.ko and kernelsu.ko are separate modules, so nothing links them together:
 * kpm.ko looks the two registration functions up by name at load time (see
 * kpm_standalone.c). Struct layout therefore *is* the ABI -- magic and version
 * are checked on registration so a mismatched kpm.ko is refused instead of
 * being called through a wrong function-pointer offset.
 */
#define KPM_OPS_MAGIC 0x4b504d4fU /* "KPMO" */
#define KPM_OPS_VERSION 1U

struct kpm_ops {
    __u32 magic; /* KPM_OPS_MAGIC */
    __u32 version; /* KPM_OPS_VERSION */
    const char *name; /* who owns the table, for diagnostics only */

    int (*load)(const char *path, const char *args, char *err, size_t err_len);
    int (*unload)(const char *name);
    int (*list)(char *out, size_t size);
    int (*info)(const char *name, char *out, size_t size);
    long (*ctl0)(const char *name, const char *args, char __user *out, int outlen);
    long (*ctl1)(const char *name, void *a1, void *a2, void *a3);
    int (*notify_event)(const char *event, const char *args);
};

/*
 * Implemented by kernelsu.ko. register() fails with -EBUSY when another owner
 * is already registered or when the built-in table already holds modules, so a
 * late kpm.ko can never shadow modules that were loaded into kernelsu.ko.
 */
int ksu_kpm_register_ops(const struct kpm_ops *ops);
void ksu_kpm_unregister_ops(const struct kpm_ops *ops);

#endif // __KSU_H_KPM_PROVIDER
