/*
 * Host-side unit tests for the KPM instruction encoder (kernel/kpm/kpm_insn.c).
 *
 * Two kinds of checks:
 *  - exact instruction words, transcribed from the ARMv8-A encodings, so a
 *    wrong bit position cannot pass;
 *  - round trips through the decoders and the prologue retargeter, which is
 *    what the hook engine relies on.
 */
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

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

static void check_word(u32 got, u32 want, const char *what)
{
    if (got == want) {
        printf("  ok   %s (0x%08x)\n", what, got);
    } else {
        printf("  FAIL %s: got 0x%08x want 0x%08x\n", what, got, want);
        failures++;
    }
}

static void test_index_modes(void)
{
    printf("pre/post-index pairs (words from index_ref.s)\n");
    check_word(kpm_insn_gen_ldstp_mode(29, 30, 31, -16, true, false, KPM_LDST_PRE_INDEX), 0xa9bf7bfdu,
               "stp x29,x30,[sp,#-16]!");
    check_word(kpm_insn_gen_ldstp_mode(29, 30, 31, 16, true, true, KPM_LDST_POST_INDEX), 0xa8c17bfdu,
               "ldp x29,x30,[sp],#16");
    check_word(kpm_insn_gen_ldstp_mode(29, 30, 31, 16, true, false, KPM_LDST_POST_INDEX), 0xa8817bfdu,
               "stp x29,x30,[sp],#16");
    check_word(kpm_insn_gen_ldstp_mode(29, 30, 31, -16, true, true, KPM_LDST_PRE_INDEX), 0xa9ff7bfdu,
               "ldp x29,x30,[sp,#-16]!");

    printf("prologue helpers (same reference)\n");
    check_word(kpm_insn_gen_add_sub_imm(29, 31, 0, true, false), 0x910003fdu, "mov x29, sp");
    check_word(kpm_insn_gen_add_sub_imm(31, 31, 16, true, false), 0x910043ffu, "add sp, sp, #16");
    check_word(kpm_insn_gen_add_sub_imm(31, 31, 32, true, true), 0xd10083ffu, "sub sp, sp, #32");
    check_word(kpm_insn_gen_ldst_imm(19, 31, 16, true, false), 0xf9000bf3u, "str x19, [sp, #16]");
    check_word(kpm_insn_gen_ldst_imm(19, 31, 16, true, true), 0xf9400bf3u, "ldr x19, [sp, #16]");
}

static void test_ldr_literal(void)
{
    printf("literal load (word from lit_ref.s)\n");
    check_word(kpm_insn_gen_ldr_literal(1, 16), 0x58000081u, "ldr x1, [pc, #16]");
    check_word(kpm_insn_gen_ldr_literal(0, 0), 0x58000000u, "ldr x0, [pc, #0]");
    check(!kpm_insn_gen_ldr_literal(1, 3), "unaligned offset refused");
}

int main(void)
{
    const u64 pc = 0x1000;

    setvbuf(stdout, NULL, _IONBF, 0);

    printf("exact encodings\n");
    check_word(kpm_insn_gen_nop(), 0xd503201fu, "nop");
    /* b .+8 */
    check_word(kpm_insn_gen_branch_imm(pc, pc + 8, false), 0x14000002u, "b +8");
    /* bl .+8 */
    check_word(kpm_insn_gen_branch_imm(pc, pc + 8, true), 0x94000002u, "bl +8");
    /* b.eq .+8 */
    check_word(kpm_insn_gen_cond_branch_imm(pc, pc + 8, KPM_COND_EQ), 0x54000040u, "b.eq +8");
    /* cbz x0, .+8 → 64-bit form */
    check_word(kpm_insn_gen_comp_branch_imm(pc, pc + 8, 0, true, false), 0xb4000040u, "cbz x0 +8");
    /* cbnz w1, .+8 → 32-bit form */
    check_word(kpm_insn_gen_comp_branch_imm(pc, pc + 8, 1, false, true), 0x35000041u, "cbnz w1 +8");
    check_word(kpm_insn_gen_branch_reg(0, false), 0xd61f0000u, "br x0");
    check_word(kpm_insn_gen_branch_reg(1, true), 0xd63f0020u, "blr x1");
    check_word(kpm_insn_gen_ret(30), 0xd65f03c0u, "ret");
    /* add x0, x0, #1 */
    check_word(kpm_insn_gen_add_sub_imm(0, 0, 1, true, false), 0x91000400u, "add x0,x0,#1");
    /* sub x2, x3, #0x10 */
    check_word(kpm_insn_gen_add_sub_imm(2, 3, 0x10, true, true), 0xd1004062u, "sub x2,x3,#0x10");
    /* movz x0, #0x1234 */
    check_word(kpm_insn_gen_movewide(0, 0x1234, 0, true, false), 0xd2824680u, "movz x0,#0x1234");
    /* movk x0, #0xabcd, lsl #16 */
    check_word(kpm_insn_gen_movewide(0, 0xabcd, 16, true, true), 0xf2b579a0u, "movk x0,#0xabcd,lsl#16");
    /* mov x1, x2 */
    check_word(kpm_insn_gen_mov_reg(1, 2, true), 0xaa0203e1u, "mov x1,x2");
    /* stp x29, x30, [sp, #-16]! is pre-index; plain stp x29,x30,[sp,#-16] */
    check_word(kpm_insn_gen_ldstp(29, 30, 31, -16, true, false), 0xa93f7bfdu, "stp x29,x30,[sp,#-16]");
    check_word(kpm_insn_gen_ldstp(29, 30, 31, 16, true, true), 0xa9417bfdu, "ldp x29,x30,[sp,#16]");
    /* ldr x0, [x1, #8] */
    check_word(kpm_insn_gen_ldst_imm(0, 1, 8, true, true), 0xf9400420u, "ldr x0,[x1,#8]");
    /* adr x5, .+0x1010 / adrp x6, .+0x1000 (verified by linking with ld.lld) */
    check_word(kpm_insn_gen_adr(0, 0x1010, 5, false), 0x10008085u, "adr x5,.+0x1010");
    check_word(kpm_insn_gen_adr(0, 0x1000, 6, true), 0xb0000006u, "adrp x6,.+0x1000");
    /* str w0, [x1, #4] */
    check_word(kpm_insn_gen_ldst_imm(0, 1, 4, false, false), 0xb9000420u, "str w0,[x1,#4]");

    printf("classification\n");
    check(kpm_insn_is_b(0x14000000u) && !kpm_insn_is_b(0x94000000u), "b vs bl");
    check(kpm_insn_is_bl(0x94000000u), "bl detected");
    check(kpm_insn_is_bcond(0x54000040u) && !kpm_insn_is_bcond(0x14000002u), "b.cond detected");
    check(kpm_insn_is_cb(0xb4000040u) && kpm_insn_is_cb(0x35000041u), "cbz/cbnz detected");
    check(kpm_insn_is_adr(0x10000000u) && !kpm_insn_is_adrp(0x10000000u), "adr vs adrp");
    check(kpm_insn_is_adrp(0x90000000u), "adrp detected");
    check(!kpm_insn_is_pc_relative(0xd503201fu), "nop is not pc-relative");
    check(kpm_insn_is_pc_relative(0x14000002u), "b is pc-relative");

    printf("decode round trips\n");
    {
        u32 insn = kpm_insn_gen_branch_imm(pc, pc + 0x100, true);

        check(kpm_insn_decode_branch_imm(insn) == 0x100, "bl displacement decodes");
        insn = kpm_insn_gen_branch_imm(pc, pc - 0x100, false);
        check(kpm_insn_decode_branch_imm(insn) == -0x100, "negative b displacement decodes");
        insn = kpm_insn_gen_comp_branch_imm(pc, pc - 0x40, 3, true, true);
        check(kpm_insn_decode_cb_imm(insn) == -0x40, "negative cbnz displacement decodes");
        insn = kpm_insn_gen_adr(pc, pc + 0x1000, 5, false);
        check(kpm_insn_decode_adr_imm(insn) == 0x1000, "adr byte offset decodes");
        insn = kpm_insn_gen_adr(pc, pc + 0x3000, 5, true);
        check(kpm_insn_decode_adr_imm(insn) == 0x3000, "adrp page offset decodes");
    }

    printf("rejects impossible encodings\n");
    check(kpm_insn_gen_branch_imm(pc, pc + 0x2, false) == 0, "misaligned branch rejected");
    check(kpm_insn_gen_branch_imm(pc, pc + (1ll << 27), false) == 0, "out-of-range branch rejected");
    check(kpm_insn_gen_add_sub_imm(0, 0, 0x1000, true, false) == 0, "imm12 overflow rejected");
    check(kpm_insn_gen_movewide(0, 1, 8, true, false) == 0, "bad movz shift rejected");
    check(kpm_insn_gen_ldstp(0, 1, 31, 4, true, false) == 0, "misaligned stp rejected");

    printf("prologue retarget\n");
    {
        const u64 tramp = 0x20000;
        u32 insn, out;
        u64 target;

        /* b +0x100 moved to a trampoline 0x10000 further away */
        insn = kpm_insn_gen_branch_imm(pc, pc + 0x100, false);
        check(kpm_insn_retarget(insn, pc, tramp, &out) == 0, "b retargeted");
        check(((s64)tramp + kpm_insn_decode_branch_imm(out)) == (s64)(pc + 0x100),
              "b still targets the original address");

        /* adrp must keep pointing at the same page */
        insn = kpm_insn_gen_adr(pc, (pc & ~0xfffull) + 0x5000, 9, true);
        check(kpm_insn_retarget(insn, pc, tramp, &out) == 0, "adrp retargeted");
        check(((tramp & ~0xfffull) + (u64)kpm_insn_decode_adr_imm(out)) == ((pc & ~0xfffull) + 0x5000),
              "adrp still targets the original page");

        /* a branch whose target is out of reach from the trampoline */
        insn = kpm_insn_gen_branch_imm(pc, pc + (1ll << 26), false);
        check(kpm_insn_retarget(insn, pc, 0x8000000000ull, &out) == -ERANGE,
              "unreachable target reports -ERANGE");

        /* non-pc-relative instruction is refused */
        check(kpm_insn_retarget(0xd503201fu, pc, tramp, &out) == -EINVAL, "nop refused");

        /* ldr literal is refused on purpose */
        insn = 0x58000000u; /* ldr x0, .+0 */
        check(kpm_insn_is_ldr_literal(insn), "ldr literal classified");
        check(kpm_insn_retarget(insn, pc, tramp, &out) == -EINVAL, "ldr literal refused");
        (void)target;
    }

    if (failures) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
