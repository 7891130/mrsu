#ifndef __KSU_H_KPM_KFUNC_HOOK
#define __KSU_H_KPM_KFUNC_HOOK

#include <linux/types.h>

/*
 * Inline and function-pointer hooking as .kpm modules see it.
 *
 * Return values are kpatch's hook_err_t (see kpm_hook.h), not kernel errnos.
 */
int kpm_kf_hook(void *func, void *replace, void **backup);
void kpm_kf_unhook(void *func);
int kpm_kf_fp_hook(uintptr_t fp_addr, void *replace, void **backup);
void kpm_kf_fp_unhook(uintptr_t fp_addr, void *backup);

#endif // __KSU_H_KPM_KFUNC_HOOK
