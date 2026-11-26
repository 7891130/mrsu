/*
 * Host-side tests for the KPM hook planner (kernel/kpm/kpm_hook.c).
 *
 * The three-instruction jump sequence is asserted against encodings produced by
 * the NDK assembler and linker (hook_ref.s). The planner prefers a single `b`
 * when it reaches, which matters for safety: 4 bytes are written atomically.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kpm/kpm_hook.h"
#include "kpm/kpm_insn.h"

static int failures;

static void check(int cond, const char *what)
{
    if (cond) {
        printf("  ok   %s\n", what);
    } else {
        printf("  FAIL %s\n", what);
        failures++;
    }
}

/* Resolve an adrp/add/br sequence back to the address it transfers to. */
static u64 long_jump_target(u64 from, const u32 *seq)
{
    u64 page = (from & ~0xfffull) + (u64)kpm_insn_decode_adr_imm(seq[0]);
    u64 lo12 = (seq[1] >> 10) & 0xfff;

    return page + lo12;
}

static void test_short_jump(void)
{
    u32 seq[KPM_HOOK_MAX_JUMP_INSNS];
    unsigned int count = 0;

    printf("short jump (single b)\n");
    check(kpm_hook_encode_jump(0x1000, 0x2000, seq, &count) == 0, "encodes");
    check(count == 1, "uses one instruction");
    check(seq[0] == 0x14000400u, "b +0x1000");
    check(kpm_insn_is_b(seq[0]), "classified as b");

    check(kpm_hook_encode_jump(0x1000, 0x1000 - 0x40, seq, &count) == 0, "encodes backwards");
    check(count == 1 && kpm_insn_decode_branch_imm(seq[0]) == -0x40, "negative displacement");
}

static void test_long_jump(void)
{
    /* 0x8000000 (128MB) is out of reach for a 26-bit branch. */
    const u64 from = 0;
    const u64 to = 0x8000000;
    u32 seq[KPM_HOOK_MAX_JUMP_INSNS];
    unsigned int count = 0;

    printf("long jump (adrp/add/br)\n");
    check(kpm_hook_encode_jump(from, to, seq, &count) == 0, "encodes");
    check(count == 3, "uses three instructions");
    check(seq[2] == 0xd61f0200u, "br x16 (matches the assembler)");
    check(long_jump_target(from, seq) == to, "resolves to the target");

    /* Range limit of the pair itself: adrp reaches +-4GB. */
    check(kpm_hook_encode_jump(0, 1ull << 34, seq, &count) == -ERANGE, "beyond +-4GB rejected");
}

static void test_near_plan(void)
{
    struct kpm_hook_plan plan;
    const u32 target_pc = 0x100000;
    const u64 tramp_pc = 0x400000;
    const u64 replacement = 0x180000; /* within +-128MB */
    u32 prologue[KPM_HOOK_MAX_PATCH_INSNS];
    int rc;

    printf("plan with a near replacement\n");
    prologue[0] = kpm_insn_gen_branch_imm(target_pc, target_pc + 0x40, false);
    prologue[1] = kpm_insn_gen_add_sub_imm(0, 0, 1, true, false);
    prologue[2] = kpm_insn_gen_nop();

    rc = kpm_hook_build(target_pc, prologue, 3, tramp_pc, replacement, &plan);
    check(rc == 0, "builds");
    check(plan.patch_count == 1, "patch is a single instruction");
    check(plan.saved_count == 1, "only one instruction displaced");
    check(kpm_insn_is_b(plan.patch[0]), "patch is a b");
    check((u64)target_pc + (u64)kpm_insn_decode_branch_imm(plan.patch[0]) == replacement,
          "patch targets the replacement");
    check(plan.resume_pc == target_pc + 4, "resume follows the patch");
    check(plan.trampoline_count >= 2, "trampoline has the displaced insn plus the jump back");
    check((u64)tramp_pc + (u64)kpm_insn_decode_branch_imm(plan.trampoline[0]) == (u64)(target_pc + 0x40),
          "displaced branch retargeted");
    check((u64)(tramp_pc + 4) + (u64)kpm_insn_decode_branch_imm(plan.trampoline[1]) == plan.resume_pc,
          "trampoline jumps back");
}

static void test_far_plan(void)
{
    struct kpm_hook_plan plan;
    const u32 target_pc = 0x100000;
    const u64 tramp_pc = 0x140000; /* near: a cbz only reaches +-1MB */
    const u64 replacement = 0x20000000; /* beyond +-128MB, within +-4GB */
    u32 prologue[KPM_HOOK_MAX_PATCH_INSNS];
    int rc;

    printf("plan with a far replacement\n");
    prologue[0] = kpm_insn_gen_add_sub_imm(0, 0, 1, true, false);
    prologue[1] = kpm_insn_gen_comp_branch_imm(target_pc + 4, target_pc + 4 + 0x20, 0, true, false);
    prologue[2] = kpm_insn_gen_nop();

    rc = kpm_hook_build(target_pc, prologue, 3, tramp_pc, replacement, &plan);
    check(rc == 0, "builds");
    check(plan.patch_count == 3, "patch uses three instructions");
    check(plan.saved_count == 3, "three instructions displaced");
    check(long_jump_target(target_pc, plan.patch) == replacement, "patch reaches the replacement");
    check(plan.resume_pc == target_pc + 12, "resume follows the patch");
    check(plan.trampoline[0] == prologue[0], "plain instruction copied verbatim");
    check((u64)(tramp_pc + 4) + (u64)kpm_insn_decode_cb_imm(plan.trampoline[1]) == (u64)(target_pc + 4 + 0x20),
          "displaced cbz retargeted");
}

static void test_refusals(void)
{
    struct kpm_hook_plan plan;
    u32 prologue[KPM_HOOK_MAX_PATCH_INSNS];

    printf("refusals\n");
    prologue[0] = 0x58000000u; /* ldr x0, literal: cannot be relocated */
    prologue[1] = kpm_insn_gen_nop();
    prologue[2] = kpm_insn_gen_nop();

    /* Force the three-instruction patch so the literal lands in the window. */
    check(kpm_hook_build(0x100000, prologue, 3, 0x40000000, 0x20000000, &plan) == -EINVAL,
          "unrelocatable prologue refused");

    check(kpm_hook_build(0x100000, prologue, 2, 0x40000000, 0x20000000, &plan) == -EINVAL, "short prologue refused");
    check(kpm_hook_build(0x100002, prologue, 3, 0x40000000, 0x20000000, &plan) == -EINVAL, "unaligned target refused");
    check(kpm_hook_build(0x100000, prologue, 3, 0x40000000, 1ull << 34, &plan) == -ERANGE,
          "unreachable replacement refused");
}

static void test_error_mapping(void)
{
    printf("kpatch hook_err_t mapping\n");
    check(KPM_HOOK_OK == 0, "success is 0");
    check(KPM_HOOK_ERR_BAD_ADDRESS == 4095, "bad address is 4095");
    check(KPM_HOOK_ERR_DUPLICATED == 4094, "duplicated is 4094");
    check(KPM_HOOK_ERR_NO_MEM == 4093, "no mem is 4093");
    check(KPM_HOOK_ERR_BAD_RELO == 4092, "bad relo is 4092");
    check(kpm_hook_errno_to_kpatch(0) == KPM_HOOK_OK, "0 maps to success");
    check(kpm_hook_errno_to_kpatch(-EEXIST) == KPM_HOOK_ERR_DUPLICATED, "-EEXIST maps to duplicated");
    check(kpm_hook_errno_to_kpatch(-ENOMEM) == KPM_HOOK_ERR_NO_MEM, "-ENOMEM maps to no mem");
    check(kpm_hook_errno_to_kpatch(-ERANGE) == KPM_HOOK_ERR_BAD_RELO, "-ERANGE maps to bad relo");
    check(kpm_hook_errno_to_kpatch(-EINVAL) == KPM_HOOK_ERR_BAD_ADDRESS, "-EINVAL maps to bad address");
    check(kpm_hook_errno_to_kpatch(-12345) == KPM_HOOK_ERR_BAD_ADDRESS, "unknown errno is not success");
}

static void test_page_fit(void)
{
    const unsigned long page = 4096;

    printf("patch page boundary\n");
    check(kpm_hook_patch_fits_page(0x1000, 1, page), "one instruction at a page start fits");
    check(kpm_hook_patch_fits_page(0x1000, 3, page), "three instructions at a page start fit");
    check(kpm_hook_patch_fits_page(0x1ffc, 1, page), "four bytes ending exactly on the boundary fit");
    check(!kpm_hook_patch_fits_page(0x1ff8, 3, page), "twelve bytes straddling the boundary are refused");
    check(!kpm_hook_patch_fits_page(0x1ffc, 3, page), "tail past the page is refused");
    check(!kpm_hook_patch_fits_page(0x1000, 3, 0), "zero page size is refused");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    test_short_jump();
    test_long_jump();
    test_near_plan();
    test_far_plan();
    test_refusals();
    test_error_mapping();
    test_page_fit();

    if (failures) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
