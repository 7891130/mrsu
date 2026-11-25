// SPDX-License-Identifier: GPL-2.0
/*
 * Inline hook installation: the only part of the hook engine that touches real
 * kernel text, so it is deliberately thin.
 *
 * Everything that can be decided ahead of time (patch shape, prologue
 * relocation, trampoline layout) lives in kpm_hook.c and is unit-tested on a
 * host. This file allocates the trampoline, writes the patch and keeps the
 * registry in sync.
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include "hook/patch_memory.h" /* ksu_patch_text */
#include "klog.h"              // IWYU pragma: keep
#include "kpm/kpm.h"           /* kpm_alloc_exec / kpm_protect_exec */
#include "kpm/kpm_hook.h"
#include "kpm/kpm_hook_table.h"

#define KPM_HOOK_TRAMP_INSNS (2 * KPM_HOOK_MAX_PATCH_INSNS + KPM_HOOK_MAX_JUMP_INSNS)

static void set_err(char *err, size_t err_len, const char *msg)
{
    if (err && err_len)
        snprintf(err, err_len, "%s", msg);
}

/*
 * Write the whole patch in one go.
 *
 * ksu_patch_text() performs the write inside stop_machine(), so every other CPU
 * is parked while the bytes land: a thread can never fetch a half-updated
 * prologue. Splitting this into per-instruction calls would give exactly that
 * window, so it must stay a single call even for the 3-instruction form.
 */
static int patch_target(u64 target, const struct kpm_hook_plan *plan)
{
    return ksu_patch_text((void *)(uintptr_t)target, (void *)plan->patch, plan->patch_count * sizeof(u32),
                          KSU_PATCH_TEXT_FLUSH_ICACHE);
}

int kpm_hook_install(u64 target, u64 replacement, const char *owner, char *err, size_t err_len)
{
    struct kpm_hook_entry *entry;
    u32 prologue[KPM_HOOK_MAX_PATCH_INSNS];
    size_t tramp_size;
    int ret;

    if (err && err_len)
        err[0] = '\0';

    if (!target || !replacement) {
        set_err(err, err_len, "invalid hook target");
        return -EINVAL;
    }
    if (target & 0x3) {
        set_err(err, err_len, "target is not instruction aligned");
        return -EINVAL;
    }

    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (!entry)
        return -ENOMEM;

    /* Snapshot the instructions the patch will displace. */
    memcpy(prologue, (const void *)(uintptr_t)target, sizeof(prologue));

    tramp_size = PAGE_ALIGN(KPM_HOOK_TRAMP_INSNS * sizeof(u32));
    entry->trampoline = kpm_alloc_exec(tramp_size);
    if (!entry->trampoline) {
        ret = -ENOMEM;
        set_err(err, err_len, "cannot allocate the trampoline");
        goto out_free_entry;
    }
    entry->trampoline_size = tramp_size;
    entry->trampoline_pc = (u64)(uintptr_t)entry->trampoline;

    ret = kpm_hook_build(target, prologue, KPM_HOOK_MAX_PATCH_INSNS, entry->trampoline_pc, replacement, &entry->plan);
    if (ret) {
        set_err(err, err_len, "cannot build a hook for this function");
        goto out_free_trampoline;
    }

    memcpy(entry->trampoline, entry->plan.trampoline, entry->plan.trampoline_count * sizeof(u32));
    kpm_protect_exec(entry->trampoline, tramp_size);

    entry->target = target;
    entry->replacement = replacement;
    entry->owner = owner;

    ret = kpm_hook_table_add(entry);
    if (ret) {
        set_err(err, err_len, "target is already hooked");
        goto out_free_trampoline;
    }

    ret = patch_target(target, &entry->plan);
    if (ret) {
        kpm_hook_table_remove(target);
        set_err(err, err_len, "cannot patch the target");
        goto out_free_trampoline;
    }

    pr_info("kpm: hooked 0x%llx -> 0x%llx (%u-insn patch)\n", (unsigned long long)target,
            (unsigned long long)replacement, entry->plan.patch_count);
    return 0;

out_free_trampoline:
    kpm_free_exec(entry->trampoline, entry->trampoline_size);
out_free_entry:
    kfree(entry);
    return ret;
}

int kpm_hook_uninstall(u64 target)
{
    struct kpm_hook_entry *entry;
    u32 restore[KPM_HOOK_MAX_PATCH_INSNS];
    int ret = 0;

    entry = kpm_hook_table_remove(target);
    if (!entry)
        return -ENOENT;

    memcpy(restore, entry->plan.saved, sizeof(restore));

    /*
     * Restore in a single call for the same reason the patch is written in one:
     * per-instruction writes would briefly leave the original first instruction
     * in front of the leftover jump tail, sending threads to a garbage address.
     */
    ret = ksu_patch_text((void *)(uintptr_t)target, restore, entry->plan.patch_count * sizeof(u32),
                         KSU_PATCH_TEXT_FLUSH_ICACHE);
    if (ret)
        pr_err("kpm: failed to restore 0x%llx (%d)\n", (unsigned long long)target, ret);

    kpm_free_exec(entry->trampoline, entry->trampoline_size);
    kfree(entry);

    return ret;
}
