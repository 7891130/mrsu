/*
 * Structural validation of the committed sample module
 * (kernel/kpm/samples/ptest/ptest.kpm).
 *
 * The other suites load *synthetic* images through the real loader, because
 * their entries are host functions. A compiler-produced .kpm cannot be loaded
 * here: kpm_elf_load() finishes by calling the module's init, and that init is
 * AArch64 machine code -- the host would execute garbage. (The first version of
 * this test did exactly that and died with SIGSEGV.) Calling the entries is
 * what the device does; see samples/ptest/README.md for the dmesg lines.
 *
 * So this suite validates the image instead, from the loader's point of view:
 *
 *   - an AArch64 ET_REL with sane section-header geometry;
 *   - the five .kpm.* entry sections exist, are SHF_ALLOC, hold exactly one
 *     8-byte, 8-byte-aligned pointer (the loader reads a function pointer out
 *     of each of them);
 *   - .kpm.info really carries name= / version= / license=;
 *   - the sample has no undefined symbols -- that is its whole point, since a
 *     failure to load it can then only be the loader;
 *   - every relocation points at a real section, inside that section, at a real
 *     symbol: a truncated or hand-mangled image fails here rather than on the
 *     device.
 *
 * Relocation *type* coverage is deliberately not asserted: the numbers live in
 * the toolchain's headers, the loader rejects unknown ones with -ENOEXEC and a
 * message naming the type, and suites [1/10]/[2/10] exercise the supported set
 * directly.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ET_REL 1
#define EM_AARCH64 183
#define SHT_SYMTAB 2
#define SHT_RELA 4
#define SHF_ALLOC 0x2

struct ehdr {
    unsigned char ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};

struct shdr {
    uint32_t name, type;
    uint64_t flags, addr, offset, size;
    uint32_t link, info;
    uint64_t addralign, entsize;
};

struct sym {
    uint32_t name;
    unsigned char info, other;
    uint16_t shndx;
    uint64_t value, size;
};

struct rela {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
};

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

static int find_section(const struct shdr *sh, unsigned int shnum, const char *shstr, const char *name)
{
    unsigned int i;

    for (i = 1; i < shnum; i++) {
        if (!strcmp(shstr + sh[i].name, name))
            return (int)i;
    }
    return -1;
}

static int contains(const char *buf, size_t len, const char *needle)
{
    size_t n = strlen(needle), i;

    for (i = 0; i + n <= len; i++) {
        if (!memcmp(buf + i, needle, n))
            return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    unsigned char *buf;
    struct ehdr *eh;
    struct shdr *sh;
    const char *shstr;
    long size;
    FILE *f;
    int init, exit_, ctl0, ctl1, event, info, symtab;
    unsigned int i, symcount = 0;
    int undef = 0, relocs = 0, bad_relocs = 0;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc < 2) {
        fprintf(stderr, "usage: %s <ptest.kpm>\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) < 64 || fseek(f, 0, SEEK_SET)) {
        fprintf(stderr, "cannot size %s\n", argv[1]);
        return 2;
    }
    buf = malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    fclose(f);

    eh = (struct ehdr *)buf;
    puts("ELF header");
    check(!memcmp(eh->ident, "\x7f" "ELF", 4), "ELF magic");
    check(eh->ident[4] == 2, "ELFCLASS64");
    check(eh->type == ET_REL, "ET_REL (a .kpm is a relocatable object, not a module)");
    check(eh->machine == EM_AARCH64, "EM_AARCH64");
    check(eh->shentsize == sizeof(struct shdr), "section header entry size");
    check(eh->shnum > 1 && eh->shnum < 64, "section count is sane");
    if (eh->shoff + (uint64_t)eh->shnum * sizeof(struct shdr) > (uint64_t)size) {
        printf("  FAIL section headers are inside the file\n");
        printf("\n1 check(s) failed\n");
        return 1;
    }
    puts("  section headers fit in the file");

    sh = (struct shdr *)(buf + eh->shoff);
    shstr = (const char *)buf + sh[eh->shstrndx].offset;

    puts("entry sections");
    init = find_section(sh, eh->shnum, shstr, ".kpm.init");
    exit_ = find_section(sh, eh->shnum, shstr, ".kpm.exit");
    ctl0 = find_section(sh, eh->shnum, shstr, ".kpm.ctl0");
    ctl1 = find_section(sh, eh->shnum, shstr, ".kpm.ctl1");
    event = find_section(sh, eh->shnum, shstr, ".kpm.event");
    info = find_section(sh, eh->shnum, shstr, ".kpm.info");
    symtab = find_section(sh, eh->shnum, shstr, ".symtab");
    check(init > 0, ".kpm.init exists");
    check(exit_ > 0, ".kpm.exit exists");
    check(ctl0 > 0, ".kpm.ctl0 exists");
    check(ctl1 > 0, ".kpm.ctl1 exists");
    check(event > 0, ".kpm.event exists");
    check(info > 0, ".kpm.info exists");
    check(symtab > 0, ".symtab exists (the loader needs it)");

    if (init > 0 && exit_ > 0 && ctl0 > 0 && ctl1 > 0 && event > 0) {
        int idx[5];
        static const char *names[5] = { ".kpm.init", ".kpm.exit", ".kpm.ctl0", ".kpm.ctl1", ".kpm.event" };

        idx[0] = init;
        idx[1] = exit_;
        idx[2] = ctl0;
        idx[3] = ctl1;
        idx[4] = event;
        for (i = 0; i < 5; i++) {
            const struct shdr *s = &sh[idx[i]];
            char label[80];

            snprintf(label, sizeof(label), "%s is SHF_ALLOC", names[i]);
            check((s->flags & SHF_ALLOC) != 0, label);
            snprintf(label, sizeof(label), "%s holds one 8-byte pointer", names[i]);
            check(s->size == 8, label);
            snprintf(label, sizeof(label), "%s is 8-byte aligned", names[i]);
            check(s->addralign >= 8, label);
        }
    }

    puts(".kpm.info");
    if (info > 0) {
        const char *base = (const char *)buf + sh[info].offset;
        size_t len = (size_t)sh[info].size;

        check(contains(base, len, "name="), "carries name=");
        check(contains(base, len, "version="), "carries version=");
        check(contains(base, len, "license="), "carries license=");
    }

    puts("symbols");
    if (symtab > 0) {
        symcount = (unsigned int)(sh[symtab].size / sizeof(struct sym));
        const struct sym *syms = (const struct sym *)(buf + sh[symtab].offset);

        for (i = 1; i < symcount; i++) {
            if (syms[i].shndx == 0)
                undef++;
        }
        printf("  (%u symbols)\n", symcount);
    }
    check(symcount > 1, "the symbol table is not empty");
    check(undef == 0, "no undefined symbols (so a load failure can only be the loader)");

    puts("relocations");
    for (i = 1; i < eh->shnum; i++) {
        const struct shdr *s = &sh[i];
        unsigned int n, k;
        const struct rela *rela;

        if (s->type != SHT_RELA)
            continue;
        if (s->info == 0 || s->info >= eh->shnum) {
            printf("  FAIL %s targets an invalid section\n", shstr + s->name);
            failures++;
            continue;
        }
        n = (unsigned int)(s->size / sizeof(struct rela));
        rela = (const struct rela *)(buf + s->offset);
        for (k = 0; k < n; k++) {
            unsigned int symidx = (unsigned int)(rela[k].info >> 32);

            relocs++;
            if (rela[k].offset >= sh[s->info].size) {
                bad_relocs++;
                continue;
            }
            if (symcount && symidx >= symcount)
                bad_relocs++;
        }
    }
    printf("  (%d relocations)\n", relocs);
    check(relocs > 0, "the image carries relocations (entries are bound by them)");
    check(bad_relocs == 0, "every relocation targets a valid section, offset and symbol");

    free(buf);
    if (failures) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
