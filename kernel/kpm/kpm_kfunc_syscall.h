#ifndef __KSU_H_KPM_KFUNC_SYSCALL
#define __KSU_H_KPM_KFUNC_SYSCALL

#include <linux/types.h>

struct task_struct;

/* Read-only syscall helpers; see kpm_kfunc_syscall.c. */
unsigned long kpm_kf_syscalln_addr(int nr, int is_compat);
unsigned long kpm_kf_syscalln_name_addr(int nr, int is_compat);
struct pt_regs *kpm_kf_task_pt_reg(struct task_struct *task);

#endif // __KSU_H_KPM_KFUNC_SYSCALL
