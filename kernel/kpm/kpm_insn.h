#ifndef __KSU_H_KPM_INSN
#define __KSU_H_KPM_INSN

#include <linux/types.h>

/*
 * Minimal AArch64 instruction encoding/decoding for the KPM hook engine.
 *
 * Only what inline hooking needs: build branches to a replacement, copy a
 * function prologue into a trampoline and fix up its PC-relative instructions.
 * Encodings follow the ARMv8-A ARM; see arch/arm64/kernel/insn.c for the
 * kernel's equivalent.
 */

#define KPM_INSN_NOP 0xd503201fu
#define KPM_INSN_RET_X30 0xd65f03c0u

/* Condition codes, bits 3..0 of B.cond. */
#define KPM_COND_EQ 0x0
#define KPM_COND_NE 0x1
#define KPM_COND_CS 0x2
#define KPM_COND_CC 0x3
#define KPM_COND_MI 0x4
#define KPM_COND_PL 0x5
#define KPM_COND_VS 0x6
#define KPM_COND_VC 0x7
#define KPM_COND_HI 0x8
#define KPM_COND_LS 0x9
#define KPM_COND_GE 0xa
#define KPM_COND_LT 0xb
#define KPM_COND_GT 0xc
#define KPM_COND_LE 0xd
#define KPM_COND_AL 0xe

/* Encoders. A failed precondition yields 0, which is an invalid instruction. */
u32 kpm_insn_gen_nop(void);
u32 kpm_insn_gen_branch_imm(u64 pc, u64 addr, bool link);
u32 kpm_insn_gen_cond_branch_imm(u64 pc, u64 addr, u8 cond);
u32 kpm_insn_gen_comp_branch_imm(u64 pc, u64 addr, u8 rt, bool is64, bool non_zero);
u32 kpm_insn_gen_branch_reg(u8 reg, bool link);
u32 kpm_insn_gen_ret(u8 reg);
u32 kpm_insn_gen_adr(u64 pc, u64 addr, u8 reg, bool page);
u32 kpm_insn_gen_add_sub_imm(u8 rd, u8 rn, s64 imm, bool is64, bool sub);
u32 kpm_insn_gen_movewide(u8 rd, u16 imm, u8 shift, bool is64, bool movk);
u32 kpm_insn_gen_mov_reg(u8 rd, u8 rm, bool is64);
u32 kpm_insn_gen_ldstp(u8 rt, u8 rt2, u8 rn, s64 offset, bool is64, bool load);
u32 kpm_insn_gen_ldst_imm(u8 rt, u8 rn, u16 offset, bool is64, bool load);

/* Classification. */
bool kpm_insn_is_b(u32 insn);
bool kpm_insn_is_bl(u32 insn);
bool kpm_insn_is_bcond(u32 insn);
bool kpm_insn_is_cb(u32 insn);
bool kpm_insn_is_adr(u32 insn);
bool kpm_insn_is_adrp(u32 insn);
bool kpm_insn_is_ldr_literal(u32 insn);
/* True when the instruction's meaning depends on its own address. */
bool kpm_insn_is_pc_relative(u32 insn);

/* Decoders, returning the displacement the instruction encodes. */
s64 kpm_insn_decode_branch_imm(u32 insn);
s64 kpm_insn_decode_cb_imm(u32 insn);
s64 kpm_insn_decode_bcond_imm(u32 insn);
s64 kpm_insn_decode_adr_imm(u32 insn);

/*
 * Rewrite a PC-relative instruction that was at `old_pc` so it keeps targeting
 * the same address when placed at `new_pc`.
 *
 * Returns 0 on success, -ERANGE when the target is out of reach from the new
 * location, -EINVAL when the instruction is not PC-relative or not supported
 * (LDR literal is deliberately unsupported: the hook engine must fall back).
 */
int kpm_insn_retarget(u32 insn, u64 old_pc, u64 new_pc, u32 *out);

#endif // __KSU_H_KPM_INSN
