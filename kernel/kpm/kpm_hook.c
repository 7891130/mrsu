// SPDX-License-Identifier: GPL-2.0
/*
 * Inline hook planning for the KPM runtime.
 *
 * Pure logic only: given a target address and its first instructions, produce
 * the patch that jumps to the replacement plus the trampoline that runs the
 * displaced instructions and jumps back. Allocating executable memory and
 * writing the patch into kernel text happens in kpm_hook_install(), which is
 * kernel-only code.
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#include "klog.h" // IWYU pragma: keep
#include "kpm/kpm_hook.h"
#include "kpm/kpm_insn.h"

/* adrp x16 / add x16, x16, #lo12 / br x16 */
#define KPM_HOOK_TMP_REG 16

int kpm_hook_encode_jump(u64 from, u64 to, u32 out[KPM_HOOK_JUMP_INSNS])
{
    u32 adrp, add, br;

    if (!out)
        return -EINVAL;

    adrp = kpm_insn_gen_adr(from, to, KPM_HOOK_TMP_REG, true);
    add = kpm_insn_gen_add_sub_imm(KPM_HOOK_TMP_REG, KPM_HOOK_TMP_REG, (s64)(to & 0xfff), true, false);
    br = kpm_insn_gen_branch_reg(KPM_HOOK_TMP_REG, false);

    /* A zero encoding means the generator rejected the operands. */
    if (!adrp || !add || !br)
        return -ERANGE;

    out[0] = adrp;
    out[1] = add;
    out[2] = br;
    return 0;
}

int kpm_hook_build(u64 target, const u32 *prologue, unsigned int count, u64 trampoline_pc, u64 replacement,
                   struct kpm_hook_plan *plan)
{
    unsigned int i;
    int ret;

    if (!prologue || !plan || !trampoline_pc)
        return -EINVAL;
    if (count < KPM_HOOK_PATCH_INSNS)
        return -EINVAL;
    if (target & 0x3)
        return -EINVAL;

    memset(plan, 0, sizeof(*plan));
    plan->saved_count = KPM_HOOK_PATCH_INSNS;
    plan->resume_pc = target + KPM_HOOK_PATCH_INSNS * 4;

    /* The patch itself jumps from the function entry to the replacement. */
    ret = kpm_hook_encode_jump(target, replacement, plan->patch);
    if (ret)
        return ret;

    for (i = 0; i < KPM_HOOK_PATCH_INSNS; i++) {
        u32 insn = prologue[i];
        u32 relocated = insn;

        plan->saved[i] = insn;

        if (kpm_insn_is_pc_relative(insn)) {
            u64 old_pc = target + (u64)i * 4;
            u64 new_pc = trampoline_pc + (u64)i * 4;

            ret = kpm_insn_retarget(insn, old_pc, new_pc, &relocated);
            if (ret) {
                /* Refuse the whole hook: a half-relocated prologue is a crash. */
                pr_warn("kpm: prologue of 0x%llx cannot be relocated (%d)\n", (unsigned long long)target, ret);
                return ret;
            }
        }

        plan->trampoline[i] = relocated;
    }

    /* Jump back into the function right after the displaced instructions. */
    ret = kpm_hook_encode_jump(trampoline_pc + (u64)KPM_HOOK_PATCH_INSNS * 4, plan->resume_pc,
                               &plan->trampoline[KPM_HOOK_PATCH_INSNS]);
    if (ret)
        return ret;

    plan->trampoline_count = KPM_HOOK_PATCH_INSNS + KPM_HOOK_JUMP_INSNS;
    return 0;
}
