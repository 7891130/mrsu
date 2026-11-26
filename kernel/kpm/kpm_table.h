#ifndef __KSU_H_KPM_TABLE
#define __KSU_H_KPM_TABLE

#include <linux/types.h>
#include <linux/uaccess.h>

/*
 * The module table: load, unload, list, info, ctl and event delivery for the
 * .kpm images that are currently loaded.
 *
 * Nothing here touches KernelSU, on purpose. The same object file is linked
 * into kernelsu.ko (built-in runtime) and into kpm.ko (standalone runtime), so
 * each module gets its own private table; kpm_ops (kpm_provider.h) makes sure
 * only one of them is reachable from userspace at a time.
 *
 * Functions here do not apply the KernelSU feature gate: that stays in
 * kpm_core.c so that disabling the `kpm` feature still covers both owners.
 */
int kpm_table_load(const char *path, const char *args, char *err, size_t err_len);
int kpm_table_unload(const char *name);
int kpm_table_list(char *out, size_t size);
int kpm_table_info(const char *name, char *out, size_t size);
long kpm_table_ctl0(const char *name, const char *args, char __user *out, int outlen);
long kpm_table_ctl1(const char *name, void *a1, void *a2, void *a3);

/* Deliver an event to every module that registered .kpm.event. */
int kpm_table_notify_event(const char *event, const char *args);

/* Unload every module; used when the owning runtime goes away. */
void kpm_table_release_all(void);

/* True when nothing is loaded -- how a new owner knows it can take over. */
bool kpm_table_is_empty(void);

#endif // __KSU_H_KPM_TABLE
