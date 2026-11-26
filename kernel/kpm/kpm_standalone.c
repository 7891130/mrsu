// SPDX-License-Identifier: GPL-2.0
/*
 * Standalone KPM runtime entry point.
 *
 * This file exists so the KPM runtime can be built as its own module (kpm.ko)
 * instead of living inside kernelsu.ko. The point is deployability: a KernelSU
 * module can insmod it at boot, so using KPM does not require flashing a boot
 * image.
 *
 * What the runtime needs from kernelsu.ko is only the control channel: the
 * ioctl entry point, the feature gate and a single owner for the module table.
 * Text patching is compiled in (hook/arm64/patch_memory.o), and symbol lookup
 * uses kallsyms directly, so nothing else crosses the module boundary.
 *
 * kpm.ko and kernelsu.ko are separate modules, so the two registration
 * functions are resolved by name at load time instead of being linked. If they
 * are missing the module refuses to load instead of carrying a runtime nobody
 * can reach.
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>

#include "infra/symbol_resolver.h"
#include "klog.h" // IWYU pragma: keep
#include "kpm/kpm_provider.h"
#include "kpm/kpm_table.h"

/* Resolved from kernelsu.ko at load time. */
typedef int (*kpm_register_fn_t)(const struct kpm_ops *ops);
typedef void (*kpm_unregister_fn_t)(const struct kpm_ops *ops);

static kpm_register_fn_t kpm_register_ops;
static kpm_unregister_fn_t kpm_unregister_ops;
static bool kpm_ops_registered;

static const struct kpm_ops kpm_standalone_ops = {
    .magic = KPM_OPS_MAGIC,
    .version = KPM_OPS_VERSION,
    .name = "kpm.ko",
    .load = kpm_table_load,
    .unload = kpm_table_unload,
    .list = kpm_table_list,
    .info = kpm_table_info,
    .ctl0 = kpm_table_ctl0,
    .ctl1 = kpm_table_ctl1,
    .notify_event = kpm_table_notify_event,
};

static int kpm_resolve_control_channel(void)
{
    unsigned long reg = find_any_symbol_exact("ksu_kpm_register_ops");
    unsigned long unreg = find_any_symbol_exact("ksu_kpm_unregister_ops");

    if (!reg || !unreg) {
        /* Be specific: "no channel" is a different problem from "bad ELF". */
        pr_err("kpm: no KernelSU control channel (register=%px unregister=%px)\n", (void *)reg, (void *)unreg);
        return -ENOENT;
    }

    kpm_register_ops = (kpm_register_fn_t)reg;
    kpm_unregister_ops = (kpm_unregister_fn_t)unreg;
    return 0;
}

int kpm_standalone_init(void)
{
    int ret;

    pr_info("kpm: standalone runtime loading\n");

    ret = kpm_resolve_control_channel();
    if (ret)
        return ret;

    ret = kpm_register_ops(&kpm_standalone_ops);
    if (ret) {
        pr_err("kpm: cannot take the control channel (%d)\n", ret);
        return ret;
    }
    kpm_ops_registered = true;

    pr_info("kpm: runtime owns the control channel\n");
    return 0;
}

void kpm_standalone_exit(void)
{
    if (kpm_ops_registered) {
        kpm_unregister_ops(&kpm_standalone_ops);
        kpm_ops_registered = false;
    }

    /* The modules belong to this runtime; unload them with it. */
    kpm_table_release_all();

    pr_info("kpm: standalone runtime unloaded\n");
}

module_init(kpm_standalone_init);
module_exit(kpm_standalone_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KernelPatch module (KPM) runtime");
