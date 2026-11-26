// SPDX-License-Identifier: GPL-2.0
/*
 * Syscall helpers exposed to .kpm modules -- the read-only half.
 *
 * Modules use these to find a syscall's handler before deciding whether to hook
 * it, so nothing here writes kernel memory. The table itself is resolved by name
 * (sys_call_table is not exported) and cached on first use.
 *
 * Matching KernelPatch: the compat variant returns 0 unless a compat table was
 * located, because CONFIG_COMPAT is not universal across the supported KMIs and
 * a wrong base would be far worse than "not available".
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/types.h>

#include <asm/ptrace.h>

#include "infra/symbol_resolver.h"
#include "klog.h" // IWYU pragma: keep

/* Sanity bound for a caller-supplied syscall number. */
#define KPM_SYSCALL_MAX 1024

static unsigned long **kpm_syscall_table;
static bool kpm_syscall_probed;

static unsigned long **kpm_sys_call_table(void)
{
    if (kpm_syscall_probed)
        return kpm_syscall_table;

    kpm_syscall_probed = true;
    kpm_syscall_table = (unsigned long **)find_kernel_symbol_exact("sys_call_table");
    if (!kpm_syscall_table)
        pr_warn("kpm: sys_call_table not found, syscall hooks unavailable\n");
    return kpm_syscall_table;
}

unsigned long kpm_kf_syscalln_addr(int nr, int is_compat)
{
    unsigned long **table = kpm_sys_call_table();

    /* No compat table lookup: KernelPatch returns 0 for it as well. */
    if (is_compat || !table)
        return 0;
    if (nr < 0 || nr >= KPM_SYSCALL_MAX)
        return 0;

    return table[nr];
}

unsigned long kpm_kf_syscalln_name_addr(int nr, int is_compat)
{
    return kpm_kf_syscalln_addr(nr, is_compat);
}

struct pt_regs *kpm_kf_task_pt_reg(struct task_struct *task)
{
    unsigned long stack;

    if (!task)
        return NULL;

    stack = (unsigned long)(uintptr_t)task_stack_page(task);
    if (!stack)
        return NULL;

    return (struct pt_regs *)(stack + THREAD_SIZE - sizeof(struct pt_regs));
}
