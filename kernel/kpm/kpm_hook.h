#ifndef __KSU_H_KPM_HOOK
#define __KSU_H_KPM_HOOK

#include <linux/types.h>

/*
 * Inline hook building blocks.
 *
 * A hook replaces the first instruction(s) of a target function with a jump to
 * a replacement, and keeps the displaced instructions in a trampoline that
 * ends with a jump back into the original function.
 *
 * Two patch shapes are supported:
 *   - a single `b` when the replacement is within +-128MB: 4 bytes, which the
 *     CPU writes atomically, so no thread can observe a half-written jump;
 *   - adrp/add/br (12 bytes) otherwise, which needs a careful write order.
 *
 * The layout decisions live here as pure functions so they can be unit-tested
 * on a host; allocating the trampoline and writing the patch into kernel text
 * is left to kpm_hook_install().
 */

#define KPM_HOOK_MAX_PATCH_INSNS 3
#define KPM_HOOK_MAX_PATCH_BYTES (KPM_HOOK_MAX_PATCH_INSNS * 4)
#define KPM_HOOK_MAX_JUMP_INSNS 3

struct kpm_hook_plan {
    /* Patch written over the target prologue. */
    u32 patch[KPM_HOOK_MAX_PATCH_INSNS];
    unsigned int patch_count;
    /* Instructions displaced by the patch, in original order. */
    u32 saved[KPM_HOOK_MAX_PATCH_INSNS];
    unsigned int saved_count;
    /* Relocated prologue followed by the jump back. */
    u32 trampoline[(2 * KPM_HOOK_MAX_PATCH_INSNS) + KPM_HOOK_MAX_JUMP_INSNS];
    unsigned int trampoline_count;
    /* Where the trampoline resumes the original function. */
    u64 resume_pc;
};

/* Single `b` if it reaches, else adrp/add/br. Returns 0 or -ERANGE. */
int kpm_hook_encode_jump(u64 from, u64 to, u32 out[KPM_HOOK_MAX_JUMP_INSNS], unsigned int *out_count);

/*
 * Build the patch and trampoline for hooking `target`, whose first
 * instructions are `prologue[0..count)`.
 *
 * `trampoline_pc` is where the trampoline will live; it only affects the
 * relocated PC-relative instructions, so any executable address works.
 *
 * Returns 0, -EINVAL when a displaced instruction cannot be relocated (the
 * caller must not hook that function), or -ERANGE when a jump is out of reach.
 */
int kpm_hook_build(u64 target, const u32 *prologue, unsigned int count, u64 trampoline_pc, u64 replacement,
                   struct kpm_hook_plan *plan);

/*
 * Kernel-only glue (kpm_hook_install.c): allocate a trampoline, build the plan,
 * patch the target and register the hook. `err` receives a diagnostic on
 * failure. Not unit-testable without a kernel: it writes real text.
 */
int kpm_hook_install(u64 target, u64 replacement, const char *owner, char *err, size_t err_len);
int kpm_hook_uninstall(u64 target);

/* Returns the replacement address, or 0 to abort; runs once the trampoline exists. */
typedef u64 (*kpm_replacement_fn)(u64 trampoline_pc, void *arg);

int kpm_hook_install_with(u64 target, kpm_replacement_fn build, void *arg, const char *owner, char *err,
                          size_t err_len);

/* before/after callbacks around the original; see kpm_hook_wrap.c. */
int kpm_hook_wrap(u64 target, unsigned int argno, kpm_hook_before_t before, kpm_hook_after_t after, void *udata,
                  char *err, size_t err_len);
int kpm_hook_unwrap_remove(u64 target, kpm_hook_before_t before, kpm_hook_after_t after, int remove);

/*
 * Error codes kpatch returns from hook(); .kpm modules compare against these
 * exact values, so they must not be renumbered.
 */
#define KPM_HOOK_OK 0
#define KPM_HOOK_ERR_BAD_ADDRESS 4095
#define KPM_HOOK_ERR_DUPLICATED 4094
#define KPM_HOOK_ERR_NO_MEM 4093
#define KPM_HOOK_ERR_BAD_RELO 4092
#define KPM_HOOK_ERR_TRANSIT_NO_MEM 4091
#define KPM_HOOK_ERR_CHAIN_FULL 4090

/* Translate a kernel errno from the installer into kpatch's hook_err_t. */
int kpm_hook_errno_to_kpatch(int errno_val);

/* True when a patch of patch_insns instructions at 	arget stays inside one page. */
bool kpm_hook_patch_fits_page(u64 target, unsigned int patch_insns, unsigned long page_size);

#endif // __KSU_H_KPM_HOOK
