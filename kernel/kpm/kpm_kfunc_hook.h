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

/*
 * Wrap hooks: run before/after callbacks around the original function instead
 * of replacing it. `argno` is the number of integer arguments the dispatcher
 * forwards, matching KernelPatch's signature.
 */
int kpm_kf_hook_wrap(void *func, int argno, void *before, void *after, void *udata);
void kpm_kf_hook_unwrap_remove(void *func, void *before, void *after, int remove);
int kpm_kf_hook_chain_add(void *chain, void *before, void *after, void *udata);
void kpm_kf_hook_chain_remove(void *chain, void *before, void *after);

#endif // __KSU_H_KPM_KFUNC_HOOK
