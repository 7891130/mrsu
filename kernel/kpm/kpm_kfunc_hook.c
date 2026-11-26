// SPDX-License-Identifier: GPL-2.0
/*
 * kfunc wrappers that expose inline hooking to .kpm modules.
 *
 * Signatures and return codes mirror KernelPatch's hook.h, so a module built
 * against kpatch's headers links and behaves the same:
 *
 *   hook_err_t hook(void *func, void *replace, void **backup);
 *   void       unhook(void *func);
 *   void       fp_hook(uintptr_t fp_addr, void *replace, void **backup);
 *   void       fp_unhook(uintptr_t fp_addr, void *backup);
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#include "hook/patch_memory.h" /* ksu_patch_text */
#include "klog.h" // IWYU pragma: keep
#include "kpm/kpm_hook.h"
#include "kpm/kpm_hook_chain.h"
#include "kpm/kpm_hook_table.h"

int kpm_kf_hook(void *func, void *replace, void **backup)
{
    struct kpm_hook_entry *entry;
    char err[128] = { 0 };
    u64 target;
    int ret;

    if (!func || !replace || !backup)
        return KPM_HOOK_ERR_BAD_ADDRESS;

    target = (u64)(uintptr_t)func;

    ret = kpm_hook_install(target, (u64)(uintptr_t)replace, NULL, err, sizeof(err));
    if (ret) {
        pr_warn("kpm: hook 0x%llx failed: %s\n", (unsigned long long)target, err);
        return kpm_hook_errno_to_kpatch(ret);
    }

    entry = kpm_hook_table_find(target);
    if (!entry) {
        /* Cannot happen unless the registry was cleared underneath us. */
        kpm_hook_uninstall(target);
        return KPM_HOOK_ERR_BAD_ADDRESS;
    }

    /* The trampoline runs the displaced prologue, then returns to the caller. */
    *backup = (void *)(uintptr_t)entry->trampoline_pc;
    return KPM_HOOK_OK;
}

void kpm_kf_unhook(void *func)
{
    if (!func)
        return;

    if (kpm_hook_uninstall((u64)(uintptr_t)func))
        pr_warn("kpm: unhook 0x%llx failed\n", (unsigned long long)(uintptr_t)func);
}

/*
 * Function-pointer hooks do not touch text: they swap the pointer stored at
 * `fp_addr`. Modules that hook an ops table use these instead of the inline
 * form.
 */
int kpm_kf_fp_hook(uintptr_t fp_addr, void *replace, void **backup)
{
    void *original;
    int ret;

    if (!fp_addr || !replace || !backup)
        return KPM_HOOK_ERR_BAD_ADDRESS;

    original = *(void **)fp_addr;
    if (original == replace)
        return KPM_HOOK_ERR_DUPLICATED;

    if (backup)
        *backup = original;

    /* The slot may live in read-only memory after init, so write it like text. */
    ret = ksu_patch_text((void *)fp_addr, &replace, sizeof(replace), KSU_PATCH_TEXT_FLUSH_DCACHE);
    if (ret) {
        pr_warn("kpm: fp_hook 0x%llx failed: %d\n", (unsigned long long)fp_addr, ret);
        return kpm_hook_errno_to_kpatch(ret);
    }

    return KPM_HOOK_OK;
}

void kpm_kf_fp_unhook(uintptr_t fp_addr, void *backup)
{
    if (!fp_addr || !backup)
        return;

    if (ksu_patch_text((void *)fp_addr, &backup, sizeof(backup), KSU_PATCH_TEXT_FLUSH_DCACHE))
        pr_warn("kpm: fp_unhook 0x%llx failed\n", (unsigned long long)fp_addr);
}

int kpm_kf_hook_wrap(void *func, int argno, void *before, void *after, void *udata)
{
    char err[128] = { 0 };
    int ret;

    if (!func || (!before && !after) || argno < 0)
        return KPM_HOOK_ERR_BAD_ADDRESS;

    ret = kpm_hook_wrap((u64)(uintptr_t)func, (unsigned int)argno, (kpm_hook_before_t)before, (kpm_hook_after_t)after,
                        udata, err, sizeof(err));
    if (ret) {
        pr_warn("kpm: hook_wrap 0x%llx failed: %s\n", (unsigned long long)(uintptr_t)func, err);
        return kpm_hook_errno_to_kpatch(ret);
    }

    return KPM_HOOK_OK;
}

void kpm_kf_hook_unwrap_remove(void *func, void *before, void *after, int remove)
{
    if (!func)
        return;

    if (kpm_hook_unwrap_remove((u64)(uintptr_t)func, (kpm_hook_before_t)before, (kpm_hook_after_t)after, remove))
        pr_warn("kpm: hook_unwrap_remove 0x%llx failed\n", (unsigned long long)(uintptr_t)func);
}

int kpm_kf_hook_chain_add(void *chain, void *before, void *after, void *udata)
{
    int ret;

    if (!chain || (!before && !after))
        return KPM_HOOK_ERR_BAD_ADDRESS;

    ret = kpm_hook_chain_add((struct kpm_hook_chain *)chain, 0, (kpm_hook_before_t)before, (kpm_hook_after_t)after,
                             udata);
    if (ret)
        return kpm_hook_errno_to_kpatch(ret);

    return KPM_HOOK_OK;
}

void kpm_kf_hook_chain_remove(void *chain, void *before, void *after)
{
    if (!chain)
        return;

    kpm_hook_chain_remove((struct kpm_hook_chain *)chain, (kpm_hook_before_t)before, (kpm_hook_after_t)after);
}