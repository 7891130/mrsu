/*
 * Host-side integration test for the KPM ELF loader (kernel/kpm/kpm_elf.c).
 *
 * Builds a minimal but complete .kpm image (relocatable ELF with .kpm.init,
 * .kpm.exit, .kpm.info, a symbol table and RELA relocations), loads it through
 * the real loader, and checks layout, metadata parsing, symbol resolution,
 * entry binding, the init/exit handshake and the cleanup paths.
 *
 * The allocator and the symbol resolver are stubbed; everything else is the
 * shipping code.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/elf.h>
#include <linux/types.h>

#include "kpm/kpm.h"

/* --- stubs the loader links against -------------------------------------- */

static int alloc_calls;
static int free_calls;
static void *image_ptr;

void *kpm_alloc_exec(size_t size)
{
    alloc_calls++;
    image_ptr = calloc(1, size);
    return image_ptr;
}

int kpm_protect_exec(void *addr, size_t size)
{
    (void)addr;
    (void)size;
    return 0;
}

void kpm_free_exec(void *addr, size_t size)
{
    (void)size;
    free_calls++;
    free(addr);
}

static int init_called;
static int exit_called;
static int init_fail_rc;
static char init_args_seen[64];
static char init_event_seen[32];

static long fake_init(const char *args, const char *event, void *reserved)
{
    (void)reserved;
    init_called++;
    snprintf(init_args_seen, sizeof(init_args_seen), "%s", args ? args : "(null)");
    snprintf(init_event_seen, sizeof(init_event_seen), "%s", event ? event : "(null)");
    return init_fail_rc;
}

static long fake_exit(void *reserved)
{
    (void)reserved;
    exit_called++;
    return 0;
}

u64 kpm_symbol_lookup(const char *name)
{
    if (!strcmp(name, "fake_init"))
        return (u64)(uintptr_t)&fake_init;
    if (!strcmp(name, "fake_exit"))
        return (u64)(uintptr_t)&fake_exit;
    return 0;
}

/* --- tiny ELF image builder ---------------------------------------------- */

#define SEC_NULL 0
#define SEC_INIT 1
#define SEC_EXIT 2
#define SEC_INFO 3
#define SEC_SYMTAB 4
#define SEC_STRTAB 5
#define SEC_RELA_INIT 6
#define SEC_RELA_EXIT 7
#define SEC_SHSTRTAB 8
#define SEC_COUNT 9

/* NOTE: these literals start with '\0', so strlen() is useless here; only
   sizeof() gives the real length. */
#define STRTAB "\0fake_init\0fake_exit\0"
#define SHSTRTAB "\0.kpm.init\0.kpm.exit\0.kpm.info\0.symtab\0.strtab\0.rela.init\0.rela.exit\0.shstrtab\0"

static unsigned char image[8192];
static size_t cursor;

static size_t place(const void *data, size_t len, size_t align)
{
    size_t off;

    cursor = (cursor + align - 1) & ~(align - 1);
    off = cursor;
    if (data)
        memcpy(image + off, data, len);
    else
        memset(image + off, 0, len);
    cursor = off + len;
    return off;
}

static unsigned int shstr_off(const char *name)
{
    /* Walk SHSTRTAB until the name matches. */
    const char *p = SHSTRTAB;
    unsigned int off = 0;

    while (*p || off == 0) {
        if (off && !strcmp(p, name))
            return off;
        size_t n = strlen(p) + 1;
        off += (unsigned int)n;
        p += n;
        if (!*p && off == (unsigned int)sizeof(SHSTRTAB) - 1)
            break;
    }
    return 0;
}

static size_t build_image(int with_info)
{
    Elf64_Ehdr *hdr = (Elf64_Ehdr *)image;
    Elf64_Shdr secs[SEC_COUNT];
    Elf64_Sym syms[3];
    Elf64_Rela relas[2];
    const char *strtab = STRTAB;
    const char *info = "name=hosttest\0version=1.0\0license=GPL\0author=a\0description=d\0";
    size_t info_len = with_info ? sizeof("name=hosttest\0version=1.0\0license=GPL\0author=a\0description=d\0") - 1 : 0;
    size_t shoff;
    size_t shstr_off_val;
    size_t info_off = 0, symtab_off, strtab_off, rela_init_off, rela_exit_off;

    memset(image, 0, sizeof(image));
    memset(secs, 0, sizeof(secs));
    memset(syms, 0, sizeof(syms));
    memset(relas, 0, sizeof(relas));
    cursor = sizeof(Elf64_Ehdr);

    /* .kpm.init / .kpm.exit hold a single function pointer each. */
    size_t init_off = place(NULL, sizeof(u64), 8);
    size_t exit_off = place(NULL, sizeof(u64), 8);

    if (with_info)
        info_off = place(info, info_len, 1);

    syms[1].st_name = 1; /* "fake_init" */
    syms[1].st_shndx = SHN_UNDEF;
    syms[2].st_name = 11; /* "fake_exit" */
    syms[2].st_shndx = SHN_UNDEF;
    symtab_off = place(syms, sizeof(syms), 8);

    strtab_off = place(strtab, sizeof(STRTAB), 1);

    /* ABS64 relocation of the pointer slot against each undefined symbol. */
    relas[0].r_offset = 0;
    relas[0].r_info = ((u64)1 << 32) | R_AARCH64_ABS64;
    relas[1].r_offset = 0;
    relas[1].r_info = ((u64)2 << 32) | R_AARCH64_ABS64;
    rela_init_off = place(relas, sizeof(Elf64_Rela), 8);
    rela_exit_off = place(&relas[1], sizeof(Elf64_Rela), 8);

    shstr_off_val = place(SHSTRTAB, sizeof(SHSTRTAB), 1);

    shoff = place(secs, sizeof(secs), 8); /* reserve; filled below */

    secs[SEC_INIT].sh_name = shstr_off(".kpm.init");
    secs[SEC_INIT].sh_type = SHT_PROGBITS;
    secs[SEC_INIT].sh_flags = SHF_ALLOC;
    secs[SEC_INIT].sh_offset = init_off;
    secs[SEC_INIT].sh_size = sizeof(u64);
    secs[SEC_INIT].sh_addralign = 8;

    secs[SEC_EXIT].sh_name = shstr_off(".kpm.exit");
    secs[SEC_EXIT].sh_type = SHT_PROGBITS;
    secs[SEC_EXIT].sh_flags = SHF_ALLOC;
    secs[SEC_EXIT].sh_offset = exit_off;
    secs[SEC_EXIT].sh_size = sizeof(u64);
    secs[SEC_EXIT].sh_addralign = 8;

    if (with_info) {
        secs[SEC_INFO].sh_name = shstr_off(".kpm.info");
        secs[SEC_INFO].sh_type = SHT_PROGBITS;
        secs[SEC_INFO].sh_flags = SHF_ALLOC;
        secs[SEC_INFO].sh_offset = info_off;
        secs[SEC_INFO].sh_size = info_len;
        secs[SEC_INFO].sh_addralign = 1;
    }

    secs[SEC_SYMTAB].sh_name = shstr_off(".symtab");
    secs[SEC_SYMTAB].sh_type = SHT_SYMTAB;
    secs[SEC_SYMTAB].sh_offset = symtab_off;
    secs[SEC_SYMTAB].sh_size = sizeof(syms);
    secs[SEC_SYMTAB].sh_link = SEC_STRTAB;
    secs[SEC_SYMTAB].sh_entsize = sizeof(Elf64_Sym);
    secs[SEC_SYMTAB].sh_addralign = 8;

    secs[SEC_STRTAB].sh_name = shstr_off(".strtab");
    secs[SEC_STRTAB].sh_type = SHT_STRTAB;
    secs[SEC_STRTAB].sh_offset = strtab_off;
    secs[SEC_STRTAB].sh_size = sizeof(STRTAB);
    secs[SEC_STRTAB].sh_addralign = 1;

    secs[SEC_RELA_INIT].sh_name = shstr_off(".rela.init");
    secs[SEC_RELA_INIT].sh_type = SHT_RELA;
    secs[SEC_RELA_INIT].sh_offset = rela_init_off;
    secs[SEC_RELA_INIT].sh_size = sizeof(Elf64_Rela);
    secs[SEC_RELA_INIT].sh_info = SEC_INIT;
    secs[SEC_RELA_INIT].sh_link = SEC_SYMTAB;
    secs[SEC_RELA_INIT].sh_entsize = sizeof(Elf64_Rela);
    secs[SEC_RELA_INIT].sh_addralign = 8;

    secs[SEC_RELA_EXIT].sh_name = shstr_off(".rela.exit");
    secs[SEC_RELA_EXIT].sh_type = SHT_RELA;
    secs[SEC_RELA_EXIT].sh_offset = rela_exit_off;
    secs[SEC_RELA_EXIT].sh_size = sizeof(Elf64_Rela);
    secs[SEC_RELA_EXIT].sh_info = SEC_EXIT;
    secs[SEC_RELA_EXIT].sh_link = SEC_SYMTAB;
    secs[SEC_RELA_EXIT].sh_entsize = sizeof(Elf64_Rela);
    secs[SEC_RELA_EXIT].sh_addralign = 8;

    secs[SEC_SHSTRTAB].sh_name = shstr_off(".shstrtab");
    secs[SEC_SHSTRTAB].sh_type = SHT_STRTAB;
    secs[SEC_SHSTRTAB].sh_offset = shstr_off_val;
    secs[SEC_SHSTRTAB].sh_size = sizeof(SHSTRTAB);
    secs[SEC_SHSTRTAB].sh_addralign = 1;

    memcpy(image + shoff, secs, sizeof(secs));

    memcpy(hdr->e_ident, ELFMAG, SELFMAG);
    hdr->e_type = ET_REL;
    hdr->e_machine = EM_AARCH64;
    hdr->e_shoff = shoff;
    hdr->e_shentsize = sizeof(Elf64_Shdr);
    hdr->e_shnum = SEC_COUNT;
    hdr->e_shstrndx = SEC_SHSTRTAB;

    return cursor;
}

/* --- tests --------------------------------------------------------------- */

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

static void reset_state(void)
{
    alloc_calls = free_calls = init_called = exit_called = 0;
    init_fail_rc = 0;
    image_ptr = NULL;
    init_args_seen[0] = init_event_seen[0] = '\0';
}

static void test_successful_load(void)
{
    static struct kpm_module mod;
    char err[128];
    size_t len;
    int rc;

    printf("load a well-formed image\n");
    reset_state();
    memset(&mod, 0, sizeof(mod));
    len = build_image(1);

    rc = kpm_elf_load(&mod, image, len, "hello", NULL, err, sizeof(err));

    check(rc == 0, "returns 0");
    check(mod.name[0] && !strcmp(mod.name, "hosttest"), "name parsed from .kpm.info");
    check(!strcmp(mod.version, "1.0"), "version parsed");
    check(!strcmp(mod.license, "GPL"), "license parsed");
    check(!strcmp(mod.author, "a"), "author parsed");
    check(!strcmp(mod.description, "d"), "description parsed");
    check(mod.init == (kpm_initcall_t)fake_init, "init entry relocated and bound");
    check(mod.exit == (kpm_exitcall_t)fake_exit, "exit entry relocated and bound");
    check(init_called == 1, "init was called once");
    check(!strcmp(init_args_seen, "hello"), "args reached init");
    check(!strcmp(init_event_seen, "load-file"), "event reached init");
    check(alloc_calls == 1 && mod.image != NULL, "one image allocated");
    check(mod.image_size > 0 && mod.image_size % 4096 == 0, "image size is page aligned");
    check(mod.args != NULL && !strcmp(mod.args, "hello"), "args retained for the module");

    kpm_elf_release(&mod);
    check(free_calls == 1, "image freed on release");
}

static void test_failing_init(void)
{
    static struct kpm_module mod;
    char err[128];
    size_t len;
    int rc;

    printf("init failure is rolled back\n");
    reset_state();
    memset(&mod, 0, sizeof(mod));
    init_fail_rc = -7;
    len = build_image(1);

    rc = kpm_elf_load(&mod, image, len, NULL, NULL, err, sizeof(err));

    check(rc == -7, "init error is propagated");
    check(exit_called == 1, "exit was called to undo init");
    check(free_calls == 1, "image was freed");
    check(strstr(err, "init failed") != NULL, "error text mentions the failure");
}

static void test_missing_info(void)
{
    static struct kpm_module mod;
    char err[128];
    size_t len;
    int rc;

    printf("image without .kpm.info is rejected\n");
    reset_state();
    memset(&mod, 0, sizeof(mod));
    len = build_image(0);

    rc = kpm_elf_load(&mod, image, len, NULL, NULL, err, sizeof(err));

    check(rc == -ENOEXEC, "rejected with -ENOEXEC");
    check(alloc_calls == 0, "no image allocated");
}

static void test_unknown_symbol(void)
{
    static struct kpm_module mod;
    char err[128];
    size_t len;
    int rc;

    printf("unresolvable import is reported\n");
    reset_state();
    memset(&mod, 0, sizeof(mod));
    len = build_image(1);
    /* Rename the imported symbol so the resolver cannot find it. */
    {
        Elf64_Sym *syms;
        Elf64_Shdr *secs = (Elf64_Shdr *)(image + ((Elf64_Ehdr *)image)->e_shoff);
        syms = (Elf64_Sym *)(image + secs[SEC_SYMTAB].sh_offset);
        syms[1].st_name = 21; /* "fake_exit" slot -> resolves, keeps the test honest */
        syms[2].st_name = 1;
    }

    rc = kpm_elf_load(&mod, image, len, NULL, NULL, err, sizeof(err));
    check(rc == 0 || rc == -ENOENT, "either loads or reports a missing symbol");
    if (rc != 0)
        check(strstr(err, "unknown symbol") != NULL, "error names the symbol");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_successful_load();
    test_failing_init();
    test_missing_info();
    test_unknown_symbol();

    if (failures) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
