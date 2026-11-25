// SPDX-License-Identifier: GPL-2.0
/*
 * Inline hook planning for the KPM runtime.
 *
 * Pure logic only: given a target address and its first instructions, produce
 * the patch that jumps to the replacement plus the trampoline that runs the
 * displaced instructions and jumps back. Allocating executable memory and
 * writing the patch into kernel text happens in kpm_hook_install().
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>

#include "klog.h" // IWYU pragma: keep
#include "kpm/kpm_hook.h"
#include "kpm/kpm_insn.h"

/* adrp x16 / add x16, x16, #lo12 / br x16 */
#define KPM_HOOK_TMP_REG 16

int kpm_hook_encode_jump(u64 from, u64 to, u32 out[KPM_HOOK_MAX_JUMP_INSNS], unsigned int *out_count)
{
    u32 branch;

    if (!out || !out_count)
        return -EINVAL;

    /*
     * Prefer a single branch: 4 bytes are written atomically, so a thread
     * running the target cannot observe a partially patched prologue.
     */
    branch = kpm_insn_gen_branch_imm(from, to, false);
    if (branch) {
        out[0] = branch;
        *out_count = 1;
        return 0;
    }

    out[0] = kpm_insn_gen_adr(from, to, KPM_HOOK_TMP_REG, true);
    out[1] = kpm_insn_gen_add_sub_imm(KPM_HOOK_TMP_REG, KPM_HOOK_TMP_REG, (s64)(to & 0xfff), true, false);
    out[2] = kpm_insn_gen_branch_reg(KPM_HOOK_TMP_REG, false);

    /* A zero encoding means the generator rejected the operands. */
    if (!out[0] || !out[1] || !out[2])
        return -ERANGE;

    *out_count = KPM_HOOK_MAX_JUMP_INSNS;
    return 0;
}

int kpm_hook_build(u64 target, const u32 *prologue, unsigned int count, u64 trampoline_pc, u64 replacement,
                   struct kpm_hook_plan *plan)
{
    unsigned int i;
    int ret;

    if (!prologue || !plan || !trampoline_pc)
        return -EINVAL;
    if (count < KPM_HOOK_MAX_PATCH_INSNS)
        return -EINVAL;
    if (target & 0x3)
        return -EINVAL;

    memset(plan, 0, sizeof(*plan));

    /* The patch jumps from the function entry to the replacement. */
    ret = kpm_hook_encode_jump(target, replacement, plan->patch, &plan->patch_count);
    if (ret)
        return ret;

    plan->saved_count = plan->patch_count;
    plan->resume_pc = target + (u64)plan->patch_count * 4;

    for (i = 0; i < plan->saved_count; i++) {
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

    /* Jump from the end of the relocated prologue back into the function. */
    {
        unsigned int jump_count = 0;

        ret = kpm_hook_encode_jump(trampoline_pc + (u64)plan->saved_count * 4, plan->resume_pc,
                                   &plan->trampoline[plan->saved_count], &jump_count);
        if (ret)
            return ret;
        plan->trampoline_count = plan->saved_count + jump_count;
    }

    return 0;
}

/*
 * .kpm modules compare the return value against kpatch's hook_err_t, so keep
 * the mapping explicit instead of leaking kernel errnos.
 */
int kpm_hook_errno_to_kpatch(int errno_val)
{
    switch (errno_val) {
    case 0:
        return KPM_HOOK_OK;
    case -EEXIST:
        return KPM_HOOK_ERR_DUPLICATED;
    case -ENOMEM:
        return KPM_HOOK_ERR_NO_MEM;
    case -ERANGE:
        return KPM_HOOK_ERR_BAD_RELO;
    case -EINVAL:
    case -ENOENT:
    default:
        return KPM_HOOK_ERR_BAD_ADDRESS;
    }
}

/*
 * ksu_patch_text() maps a single page when it writes, so a patch that starts
 * near the end of a page must be refused: the tail would land in an unmapped
 * address. Kept here, as a pure predicate, so it can be unit-tested.
 */
bool kpm_hook_patch_fits_page(u64 target, unsigned int patch_insns, unsigned long page_size)
{
    u64 offset = target & (page_size - 1);
    u64 bytes = (u64)patch_insns * sizeof(u32);

    if (!page_size)
        return false;
    return offset + bytes <= page_size;
}