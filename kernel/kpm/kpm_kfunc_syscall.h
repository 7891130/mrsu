#ifndef __KSU_H_KPM_KFUNC_SYSCALL
#define __KSU_H_KPM_KFUNC_SYSCALL

#include <linux/types.h>

struct task_struct;

/* Read-only syscall helpers; see kpm_kfunc_syscall.c. */
unsigned long kpm_kf_syscalln_addr(int nr, int is_compat);
unsigned long kpm_kf_syscalln_name_addr(int nr, int is_compat);
struct pt_regs *kpm_kf_task_pt_reg(struct task_struct *task);

/*
 * Wrap a syscall handler. These hook the handler's own entry point rather than
 * writing sys_call_table, so no table patching is involved.
 */
int kpm_kf_hook_syscalln(int nr, int narg, void *before, void *after, void *udata);
void kpm_kf_unhook_syscalln(int nr, void *before, void *after);
int kpm_kf_hook_compat_syscalln(int nr, int narg, void *before, void *after, void *udata);
void kpm_kf_unhook_compat_syscalln(int nr, void *before, void *after);

#endif // __KSU_H_KPM_KFUNC_SYSCALL
