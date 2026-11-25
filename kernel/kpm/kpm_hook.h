#ifndef __KSU_H_KPM_HOOK
#define __KSU_H_KPM_HOOK

#include <linux/types.h>

/*
 * Inline hook building blocks.
 *
 * A hook replaces the first few instructions of a target function with a jump
 * to a replacement, and keeps the displaced instructions in a trampoline that
 * ends with a jump back into the original function.
 *
 * The layout decisions live here as pure functions so they can be unit-tested
 * on a host; allocating the trampoline and writing the patch into kernel text
 * is left to the caller.
 */

/* Jump sequences: adrp + add + br, 3 instructions = 12 bytes. */
#define KPM_HOOK_JUMP_INSNS 3
#define KPM_HOOK_JUMP_BYTES (KPM_HOOK_JUMP_INSNS * 4)

/* Number of target instructions a patch displaces. */
#define KPM_HOOK_PATCH_INSNS KPM_HOOK_JUMP_INSNS

struct kpm_hook_plan {
    /* Instructions displaced from the target, in original order. */
    u32 saved[KPM_HOOK_PATCH_INSNS];
    unsigned int saved_count;
    /* Jump that overwrites the target prologue. */
    u32 patch[KPM_HOOK_JUMP_INSNS];
    /* Trampoline contents: relocated prologue + jump back. */
    u32 trampoline[(2 * KPM_HOOK_PATCH_INSNS) + KPM_HOOK_JUMP_INSNS];
    unsigned int trampoline_count;
    /* Where the trampoline resumes the original function. */
    u64 resume_pc;
};

/* adrp/add/br to `to`, executed at `from`. Returns 0 or -ERANGE. */
int kpm_hook_encode_jump(u64 from, u64 to, u32 out[KPM_HOOK_JUMP_INSNS]);

/*
 * Build the patch and the trampoline for hooking `target`, whose first
 * instructions are `prologue[0..count)`.
 *
 * `trampoline_pc` is where the trampoline will be placed; it only affects the
 * relocated PC-relative instructions, so any executable address works.
 *
 * Returns 0, -EINVAL when a displaced instruction cannot be relocated (the
 * caller must not hook that function), or -ERANGE when a jump is out of reach.
 */
int kpm_hook_build(u64 target, const u32 *prologue, unsigned int count, u64 trampoline_pc, u64 replacement,
                   struct kpm_hook_plan *plan);

#endif // __KSU_H_KPM_HOOK
