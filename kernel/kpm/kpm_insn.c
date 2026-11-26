// SPDX-License-Identifier: GPL-2.0
/*
 * AArch64 instruction encoding for the KPM hook engine.
 *
 * Field layouts follow the ARMv8-A ARM and mirror arch/arm64/kernel/insn.c, but
 * are written as plain bit arithmetic so the same code can be unit-tested on a
 * host (see _vendor/kpm-hosttest/test_insn.c).
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>

#include "kpm/kpm_insn.h"

#define INSN_MASK(n) ((u32)((1ull << (n)) - 1))

static s64 sign_extend(u64 value, unsigned int bits)
{
    u64 sign = 1ull << (bits - 1);

    return (s64)((value ^ sign) - sign);
}

u32 kpm_insn_gen_nop(void)
{
    return KPM_INSN_NOP;
}

bool kpm_insn_is_b(u32 insn)
{
    return (insn & 0xfc000000u) == 0x14000000u;
}

bool kpm_insn_is_bl(u32 insn)
{
    return (insn & 0xfc000000u) == 0x94000000u;
}

bool kpm_insn_is_bcond(u32 insn)
{
    return (insn & 0xff000010u) == 0x54000000u;
}

bool kpm_insn_is_cb(u32 insn)
{
    return (insn & 0x7e000000u) == 0x34000000u;
}

/*
 * ADR/ADRP share bits 28..24 (10000); they differ in bit 31 (op) and use
 * bits 30..29 for immlo. Both bits must be part of the mask, otherwise ADRP
 * never matches and ADR matches everything in the family.
 */
#define KPM_ADR_FAMILY_MASK 0x9f000000u

bool kpm_insn_is_adr(u32 insn)
{
    return (insn & KPM_ADR_FAMILY_MASK) == 0x10000000u;
}

bool kpm_insn_is_adrp(u32 insn)
{
    return (insn & KPM_ADR_FAMILY_MASK) == 0x90000000u;
}

bool kpm_insn_is_ldr_literal(u32 insn)
{
    /* LDR (literal), both 32-bit and 64-bit forms. */
    return (insn & 0x3b000000u) == 0x18000000u;
}

bool kpm_insn_is_pc_relative(u32 insn)
{
    return kpm_insn_is_b(insn) || kpm_insn_is_bl(insn) || kpm_insn_is_bcond(insn) || kpm_insn_is_cb(insn) ||
           kpm_insn_is_adr(insn) || kpm_insn_is_adrp(insn) || kpm_insn_is_ldr_literal(insn);
}

/* --- encoders ------------------------------------------------------------ */

u32 kpm_insn_gen_branch_imm(u64 pc, u64 addr, bool link)
{
    s64 offset = (s64)addr - (s64)pc;
    s64 imm = offset >> 2;

    if (offset & 0x3)
        return 0;
    if (imm < -(1ll << 25) || imm >= (1ll << 25))
        return 0;

    return (link ? 0x94000000u : 0x14000000u) | ((u32)imm & INSN_MASK(26));
}

u32 kpm_insn_gen_cond_branch_imm(u64 pc, u64 addr, u8 cond)
{
    s64 offset = (s64)addr - (s64)pc;
    s64 imm = offset >> 2;

    if (offset & 0x3)
        return 0;
    if (imm < -(1ll << 18) || imm >= (1ll << 18))
        return 0;

    return 0x54000000u | (((u32)imm & INSN_MASK(19)) << 5) | (cond & 0xf);
}

u32 kpm_insn_gen_comp_branch_imm(u64 pc, u64 addr, u8 rt, bool is64, bool non_zero)
{
    s64 offset = (s64)addr - (s64)pc;
    s64 imm = offset >> 2;
    u32 base;

    if (offset & 0x3)
        return 0;
    if (imm < -(1ll << 18) || imm >= (1ll << 18))
        return 0;

    if (is64)
        base = non_zero ? 0xb5000000u : 0xb4000000u;
    else
        base = non_zero ? 0x35000000u : 0x34000000u;

    return base | (((u32)imm & INSN_MASK(19)) << 5) | (rt & 0x1f);
}

u32 kpm_insn_gen_branch_reg(u8 reg, bool link)
{
    return (link ? 0xd63f0000u : 0xd61f0000u) | ((u32)(reg & 0x1f) << 5);
}

u32 kpm_insn_gen_ret(u8 reg)
{
    return 0xd65f0000u | ((u32)(reg & 0x1f) << 5);
}

u32 kpm_insn_gen_adr(u64 pc, u64 addr, u8 reg, bool page)
{
    s64 offset;
    s64 imm;
    u32 immlo, immhi;

    if (page) {
        offset = (s64)(addr & ~0xfffull) - (s64)(pc & ~0xfffull);
        imm = offset >> 12;
    } else {
        offset = (s64)addr - (s64)pc;
        imm = offset;
    }

    if (imm < -(1ll << 20) || imm >= (1ll << 20))
        return 0;

    immlo = (u32)imm & 0x3;
    immhi = ((u32)imm >> 2) & INSN_MASK(19);

    return (page ? 0x90000000u : 0x10000000u) | (immlo << 29) | (immhi << 5) | (reg & 0x1f);
}

u32 kpm_insn_gen_add_sub_imm(u8 rd, u8 rn, s64 imm, bool is64, bool sub)
{
    u32 base;

    if (imm < 0 || imm > 0xfff)
        return 0;

    if (is64)
        base = sub ? 0xd1000000u : 0x91000000u;
    else
        base = sub ? 0x51000000u : 0x11000000u;

    return base | ((u32)imm << 10) | ((u32)(rn & 0x1f) << 5) | (rd & 0x1f);
}

u32 kpm_insn_gen_movewide(u8 rd, u16 imm, u8 shift, bool is64, bool movk)
{
    u32 base;

    if (shift & 0xf)
        return 0;
    if (is64 ? shift >= 64 : shift >= 32)
        return 0;

    if (is64)
        base = movk ? 0xf2800000u : 0xd2800000u;
    else
        base = movk ? 0x72800000u : 0x52800000u;

    return base | ((u32)(shift / 16) << 21) | ((u32)imm << 5) | (rd & 0x1f);
}

u32 kpm_insn_gen_mov_reg(u8 rd, u8 rm, bool is64)
{
    /* MOV Rd, Rm is ORR Rd, XZR, Rm. */
    return (is64 ? 0xaa0003e0u : 0x2a0003e0u) | ((u32)(rm & 0x1f) << 16) | (rd & 0x1f);
}

u32 kpm_insn_gen_ldstp(u8 rt, u8 rt2, u8 rn, s64 offset, bool is64, bool load)
{
    s64 scale = is64 ? 8 : 4;
    u32 base;

    if (offset % scale)
        return 0;
    if (offset / scale < -64 || offset / scale > 63)
        return 0;

    if (is64)
        base = load ? 0xa9400000u : 0xa9000000u;
    else
        base = load ? 0x29400000u : 0x29000000u;

    return base | (((u32)((offset / scale) & 0x7f)) << 15) | ((u32)(rt2 & 0x1f) << 10) | ((u32)(rn & 0x1f) << 5) |
           (rt & 0x1f);
}

u32 kpm_insn_gen_ldst_imm(u8 rt, u8 rn, u16 offset, bool is64, bool load)
{
    u32 scale = is64 ? 8 : 4;
    u32 base;

    if (offset % scale)
        return 0;
    if (offset / scale > 0xfff)
        return 0;

    if (is64)
        base = load ? 0xf9400000u : 0xf9000000u;
    else
        base = load ? 0xb9400000u : 0xb9000000u;

    return base | ((u32)(offset / scale) << 10) | ((u32)(rn & 0x1f) << 5) | (rt & 0x1f);
}

/* --- decoders ------------------------------------------------------------ */

s64 kpm_insn_decode_branch_imm(u32 insn)
{
    return sign_extend(insn & INSN_MASK(26), 26) << 2;
}

s64 kpm_insn_decode_cb_imm(u32 insn)
{
    return sign_extend((insn >> 5) & INSN_MASK(19), 19) << 2;
}

s64 kpm_insn_decode_bcond_imm(u32 insn)
{
    return sign_extend((insn >> 5) & INSN_MASK(19), 19) << 2;
}

s64 kpm_insn_decode_adr_imm(u32 insn)
{
    u32 immlo = (insn >> 29) & 0x3;
    u32 immhi = (insn >> 5) & INSN_MASK(19);
    s64 imm = sign_extend(((s64)immhi << 2) | immlo, 21);

    return kpm_insn_is_adrp(insn) ? (imm << 12) : imm;
}

/* --- relocation ---------------------------------------------------------- */

int kpm_insn_retarget(u32 insn, u64 old_pc, u64 new_pc, u32 *out)
{
    u32 rewritten;

    if (!out)
        return -EINVAL;

    if (kpm_insn_is_b(insn) || kpm_insn_is_bl(insn)) {
        u64 target = (u64)((s64)old_pc + kpm_insn_decode_branch_imm(insn));

        rewritten = kpm_insn_gen_branch_imm(new_pc, target, kpm_insn_is_bl(insn));
    } else if (kpm_insn_is_bcond(insn)) {
        u64 target = (u64)((s64)old_pc + kpm_insn_decode_bcond_imm(insn));

        rewritten = kpm_insn_gen_cond_branch_imm(new_pc, target, (u8)(insn & 0xf));
    } else if (kpm_insn_is_cb(insn)) {
        u64 target = (u64)((s64)old_pc + kpm_insn_decode_cb_imm(insn));
        bool is64 = (insn & 0x80000000u) != 0;
        bool non_zero = (insn & 0x01000000u) != 0;

        rewritten = kpm_insn_gen_comp_branch_imm(new_pc, target, (u8)(insn & 0x1f), is64, non_zero);
    } else if (kpm_insn_is_adr(insn) || kpm_insn_is_adrp(insn)) {
        u64 target = kpm_insn_is_adrp(insn) ? (old_pc & ~0xfffull) + (u64)kpm_insn_decode_adr_imm(insn) :
                                              (u64)((s64)old_pc + kpm_insn_decode_adr_imm(insn));

        rewritten = kpm_insn_gen_adr(new_pc, target, (u8)(insn & 0x1f), kpm_insn_is_adrp(insn));
    } else {
        /* Not PC-relative, or a form we refuse to rewrite (LDR literal). */
        return -EINVAL;
    }

    if (!rewritten)
        return -ERANGE;

    *out = rewritten;
    return 0;
}

u32 kpm_insn_gen_ldstp_mode(u8 rt, u8 rt2, u8 rn, s64 offset, bool is64, bool load, enum kpm_ldst_mode mode)
{
    s64 scale = is64 ? 8 : 4;
    u32 base;

    if (offset % scale)
        return 0;
    if (offset / scale < -64 || offset / scale > 63)
        return 0;

    /* Bits 24..23 select the addressing mode; the pair encodings differ. */
    if (is64) {
        switch (mode) {
        case KPM_LDST_OFFSET:
            base = load ? 0xa9400000u : 0xa9000000u;
            break;
        case KPM_LDST_POST_INDEX:
            base = load ? 0xa8c00000u : 0xa8800000u;
            break;
        case KPM_LDST_PRE_INDEX:
            base = load ? 0xa9c00000u : 0xa9800000u;
            break;
        default:
            return 0;
        }
    } else {
        switch (mode) {
        case KPM_LDST_OFFSET:
            base = load ? 0x29400000u : 0x29000000u;
            break;
        case KPM_LDST_POST_INDEX:
            base = load ? 0x28c00000u : 0x28800000u;
            break;
        case KPM_LDST_PRE_INDEX:
            base = load ? 0x29c00000u : 0x29800000u;
            break;
        default:
            return 0;
        }
    }

    return base | (((u32)((offset / scale) & 0x7f)) << 15) | ((u32)(rt2 & 0x1f) << 10) | ((u32)(rn & 0x1f) << 5) |
           (rt & 0x1f);
}

u32 kpm_insn_gen_ldr_literal(u8 rt, s64 byte_offset)
{
    s64 imm19 = byte_offset / 4;

    if (byte_offset % 4)
        return 0;
    if (imm19 < -(1 << 18) || imm19 >= (1 << 18))
        return 0;

    return 0x58000000u | (((u32)(imm19 & 0x7ffff)) << 5) | (rt & 0x1f);
}