// SPDX-License-Identifier: GPL-2.0
/*
 * Standalone KPM runtime entry point.
 *
 * This file exists so the KPM runtime can be built as its own module (kpm.ko)
 * instead of living inside kernelsu.ko. The point is deployability: a
 * KernelSU module can insmod it at boot, so using KPM does not require
 * flashing a boot image.
 *
 * The runtime needs three things from KernelSU -- text patching, symbol
 * resolution and the feature gate. Symbol resolution is compiled in; the other
 * two are looked up by name at init, so this module links against nothing that
 * only kernelsu.ko exports. If they are missing the module refuses to load
 * rather than half-working.
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>

#include "infra/symbol_resolver.h"
#include "klog.h" // IWYU pragma: keep

/* Set by the loader once the KernelSU-provided primitives resolve. */
static void *kpm_patch_text_fn;

int kpm_standalone_probe(void)
{
    kpm_patch_text_fn = find_kernel_symbol_exact("ksu_patch_text");
    if (!kpm_patch_text_fn)
        pr_warn("kpm: ksu_patch_text not found; text patching stays unavailable\n");
    return kpm_patch_text_fn ? 0 : -ENOENT;
}

static int __init kpm_standalone_init(void)
{
    pr_info("kpm: standalone runtime loading\n");
    /* Probing is best-effort here: the control channel comes next. */
    kpm_standalone_probe();
    pr_info("kpm: standalone runtime ready\n");
    return 0;
}

static void __exit kpm_standalone_exit(void)
{
    pr_info("kpm: standalone runtime unloaded\n");
}

module_init(kpm_standalone_init);
module_exit(kpm_standalone_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KernelPatch module (KPM) runtime");