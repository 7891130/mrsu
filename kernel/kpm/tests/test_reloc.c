/*
 * Host-side unit tests for the KPM loader's AArch64 relocation code.
 *
 * Compiles the *real* kernel/kpm/kpm_reloc.c against stub kernel headers, then
 * relocates hand-built section tables and decodes the patched instructions with
 * an independent decoder. This catches encoding/sign/mask mistakes without
 * needing a kernel build.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>

#include <linux/elf.h>

int kpm_apply_relocations(Elf64_Shdr *sechdrs, unsigned int shnum, unsigned int symindex);

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

/* --- independent AArch64 decoders ---------------------------------------- */

static long long decode_adrp_imm(u32 insn)
{
    u32 immlo = (insn >> 29) & 0x3u;
    u32 immhi = (insn >> 5) & 0x7ffffu;
    long long imm = ((long long)immhi << 2) | immlo;

    if (imm & (1ll << 20))
        imm -= (1ll << 21);
    return imm;
}

static u32 decode_add_lo12(u32 insn)
{
    return (insn >> 10) & 0xfffu;
}

static long long decode_branch_imm26(u32 insn)
{
    long long imm = insn & 0x3ffffff;

    if (imm & (1 << 25))
        imm -= (1ll << 26);
    return imm << 2;
}

/* --- fixture ------------------------------------------------------------- */

#define SHNUM 6
#define SEC_TEXT 1
#define SEC_DATA 2
#define SEC_RELA_TEXT 3
#define SEC_RELA_DATA 4
#define SEC_SYMTAB 5

static u32 text[4];
static u64 data[2];
static Elf64_Rela relas[4];
static Elf64_Rela drelas[4];
static Elf64_Sym syms[2];
static Elf64_Shdr secs[SHNUM];

static void build_sections(unsigned int rela_count)
{
    memset(secs, 0, sizeof(secs));
    memset(syms, 0, sizeof(syms));
    memset(relas, 0, sizeof(relas));

    secs[SEC_TEXT].sh_type = 1; /* SHT_PROGBITS */
    secs[SEC_TEXT].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    secs[SEC_TEXT].sh_addr = (u64)(uintptr_t)text;
    secs[SEC_TEXT].sh_size = sizeof(text);

    secs[SEC_DATA].sh_type = 1;
    secs[SEC_DATA].sh_flags = SHF_ALLOC | SHF_WRITE;
    secs[SEC_DATA].sh_addr = (u64)(uintptr_t)data;
    secs[SEC_DATA].sh_size = sizeof(data);

    secs[SEC_RELA_TEXT].sh_type = SHT_RELA;
    secs[SEC_RELA_TEXT].sh_addr = (u64)(uintptr_t)relas;
    secs[SEC_RELA_TEXT].sh_size = rela_count * sizeof(Elf64_Rela);
    secs[SEC_RELA_TEXT].sh_info = SEC_TEXT;

    secs[SEC_RELA_DATA].sh_type = SHT_RELA;
    secs[SEC_RELA_DATA].sh_addr = (u64)(uintptr_t)drelas;
    secs[SEC_RELA_DATA].sh_info = SEC_DATA;

    secs[SEC_SYMTAB].sh_type = 2; /* SHT_SYMTAB */
    secs[SEC_SYMTAB].sh_addr = (u64)(uintptr_t)syms;
    secs[SEC_SYMTAB].sh_size = sizeof(syms);
}

static void set_rela(int idx, u64 offset, u32 type, u64 symval, long long addend)
{
    syms[1].st_value = symval;
    relas[idx].r_offset = offset;
    relas[idx].r_info = ((u64)1 << 32) | type;
    relas[idx].r_addend = addend;
}

/* Relocations that patch the .data section. */
static void set_drela(int idx, u64 offset, u32 type, u64 symval, long long addend)
{
    syms[1].st_value = symval;
    drelas[idx].r_offset = offset;
    drelas[idx].r_info = ((u64)1 << 32) | type;
    drelas[idx].r_addend = addend;
    secs[SEC_RELA_DATA].sh_size = (idx + 1) * sizeof(Elf64_Rela);
}

/* --- tests --------------------------------------------------------------- */

static void test_abs64(void)
{
    const u64 want = 0x1122334455667788ull;

    printf("ABS64 into data\n");
    text[0] = 0;
    data[0] = 0;
    build_sections(1);
    set_drela(0, 0, R_AARCH64_ABS64, want, 0);

    check(kpm_apply_relocations(secs, SHNUM, SEC_SYMTAB) == 0, "returns 0");
    check(data[0] == want, "value written");
}

static void test_abs64_addend(void)
{
    printf("ABS64 with addend\n");
    data[1] = 0;
    build_sections(1);
    set_drela(0, sizeof(u64), R_AARCH64_ABS64, 0x1000, 0x234);

    check(kpm_apply_relocations(secs, SHNUM, SEC_SYMTAB) == 0, "returns 0");
    check(data[1] == 0x1234, "value + addend written");
}

static void test_branch26(void)
{
    long long want = 0x1000;
    long long got;

    printf("CALL26 (bl)\n");
    text[2] = 0x94000000u; /* bl #0 */
    build_sections(1);
    set_rela(0, 2 * sizeof(u32), R_AARCH64_CALL26, (u64)(uintptr_t)&text[2] + want, 0);

    check(kpm_apply_relocations(secs, SHNUM, SEC_SYMTAB) == 0, "returns 0");
    got = decode_branch_imm26(text[2]);
    check(got == want, "branch displacement encoded");
}

static void test_adrp_add_pair(void)
{
    uintptr_t page = (uintptr_t)text & ~0xfffull;
    u64 target = page + 0x5000 + 0x678;
    u64 resolved;

    printf("ADRP + ADD pair (kernel call sequence)\n");
    text[0] = 0x90000000u; /* adrp x0, #0 */
    text[1] = 0x91000000u; /* add  x0, x0, #0 */
    build_sections(2);
    set_rela(0, 0, R_AARCH64_ADR_PREL_PG_HI21, target, 0);
    set_rela(1, sizeof(u32), R_AARCH64_ADD_ABS_LO12_NC, target, 0);

    check(kpm_apply_relocations(secs, SHNUM, SEC_SYMTAB) == 0, "returns 0");

    printf("    adrp insn=%08x imm=%lld  add insn=%08x lo12=%03x\n",
           text[0], decode_adrp_imm(text[0]), text[1], decode_add_lo12(text[1]));
    resolved = (u64)(((uintptr_t)text & ~0xfffull) + ((u64)decode_adrp_imm(text[0]) << 12));
    resolved += decode_add_lo12(text[1]);
    check(resolved == target, "adrp+add resolves to the target");
}

static void test_unsupported(void)
{
    printf("unsupported relocation\n");
    build_sections(1);
    set_rela(0, 0, 0xdead, 0, 0);

    check(kpm_apply_relocations(secs, SHNUM, SEC_SYMTAB) == -ENOEXEC,
          "rejected with -ENOEXEC");
}

static void test_abs32_overflow(void)
{
    printf("ABS32 overflow\n");
    data[0] = 0;
    build_sections(1);
    /* 0x1_0000_0000 does not fit into 32 bits */
    set_drela(0, 0, R_AARCH64_ABS32, 0x100000000ull, 0);

    check(kpm_apply_relocations(secs, SHNUM, SEC_SYMTAB) == -ENOEXEC,
          "overflow rejected");
}

static void test_skips_non_alloc_target(void)
{
    printf("relocation against a non-allocated section\n");
    build_sections(1);
    secs[SEC_RELA_TEXT].sh_info = SEC_SYMTAB; /* not SHF_ALLOC */
    set_rela(0, 0, 0xdead, 0, 0);

    check(kpm_apply_relocations(secs, SHNUM, SEC_SYMTAB) == 0,
          "skipped instead of failing");
}

int main(void)
{
    test_abs64();
    test_abs64_addend();
    test_branch26();
    test_adrp_add_pair();
    test_unsupported();
    test_abs32_overflow();
    test_skips_non_alloc_target();

    if (failures) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
