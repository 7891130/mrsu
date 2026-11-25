// SPDX-License-Identifier: GPL-2.0
/*
 * Loader for .kpm images (KernelPatch modules).
 *
 * A .kpm is an AArch64 ET_REL ELF object. Entry points are advertised through
 * section names (.kpm.init/.exit/.ctl0/.ctl1/.event) and metadata through
 * .kpm.info, matching KernelPatch so that modules built for kpatch load
 * unchanged.
 *
 * The image is laid out as: executable sections, then read-only data, then
 * writable data, each start page aligned so the text pages can be flipped
 * read-only after relocation.
 */
#include <linux/elf.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#if defined(__aarch64__)
#include <asm/cacheflush.h>
#if KSU_NEW_DCACHE_FLUSH
#define kpm_flush_icache(start, end) caches_clean_inval_pou((start), (end))
#else
#define kpm_flush_icache(start, end) __flush_icache_range((start), (end))
#endif
#else
#define kpm_flush_icache(start, end) \
    do {                             \
    } while (0)
#endif

#include "klog.h" // IWYU pragma: keep
#include "kpm/kpm.h"
#include "kpm/kpm_reloc.h"
#include "kpm/kpm_symbol.h"

#define KPM_MAX_SECTIONS 96
#define KPM_MIN_ALIGN 8UL

struct kpm_load_info {
    const char *data;
    size_t len;
    Elf64_Shdr *sechdrs;
    const char *secstrings;
    const char *strtab;
    unsigned int shnum;
    unsigned int symindex;
    size_t image_size;
    size_t text_size;
    char *err;
    size_t err_len;
};

static void kpm_error(struct kpm_load_info *li, const char *msg)
{
    if (li->err && li->err_len)
        snprintf(li->err, li->err_len, "%s", msg);
    pr_err("kpm: %s\n", msg);
}

static const char *kpm_sec_name(const struct kpm_load_info *li, unsigned int i)
{
    return li->secstrings + li->sechdrs[i].sh_name;
}

static int kpm_find_sec(const struct kpm_load_info *li, const char *name)
{
    unsigned int i;

    for (i = 1; i < li->shnum; i++) {
        if ((li->sechdrs[i].sh_flags & SHF_ALLOC) && !strcmp(kpm_sec_name(li, i), name))
            return (int)i;
    }
    return 0;
}

static int kpm_check_header(const char *data, size_t len, char *err, size_t err_len)
{
    const Elf64_Ehdr *hdr = (const Elf64_Ehdr *)data;
    size_t shdr_bytes;

    if (len < sizeof(*hdr)) {
        if (err && err_len)
            snprintf(err, err_len, "image is smaller than an ELF header");
        return -ENOEXEC;
    }
    if (memcmp(hdr->e_ident, ELFMAG, SELFMAG)) {
        if (err && err_len)
            snprintf(err, err_len, "not an ELF file");
        return -ENOEXEC;
    }
    if (hdr->e_type != ET_REL) {
        if (err && err_len)
            snprintf(err, err_len, "not a relocatable object (ET_REL)");
        return -ENOEXEC;
    }
#if defined(__aarch64__)
    if (hdr->e_machine != EM_AARCH64) {
        if (err && err_len)
            snprintf(err, err_len, "module architecture is not AArch64");
        return -ENOEXEC;
    }
#endif
    if (hdr->e_shentsize != sizeof(Elf64_Shdr)) {
        if (err && err_len)
            snprintf(err, err_len, "unexpected section header size");
        return -ENOEXEC;
    }
    if (!hdr->e_shnum || hdr->e_shnum > KPM_MAX_SECTIONS) {
        if (err && err_len)
            snprintf(err, err_len, "unsupported section count");
        return -ENOEXEC;
    }
    if (hdr->e_shstrndx >= hdr->e_shnum) {
        if (err && err_len)
            snprintf(err, err_len, "invalid section name table index");
        return -ENOEXEC;
    }
    shdr_bytes = (size_t)hdr->e_shnum * sizeof(Elf64_Shdr);
    if (hdr->e_shoff >= len || shdr_bytes > len - hdr->e_shoff) {
        if (err && err_len)
            snprintf(err, err_len, "section headers are out of bounds");
        return -ENOEXEC;
    }

    return 0;
}

/* Copy the section table out of the image so we can rewrite sh_addr/size. */
static int kpm_prepare(struct kpm_load_info *li)
{
    const Elf64_Ehdr *hdr = (const Elf64_Ehdr *)li->data;
    unsigned int i;

    li->shnum = hdr->e_shnum;
    li->sechdrs = kmalloc_array(li->shnum, sizeof(Elf64_Shdr), GFP_KERNEL);
    if (!li->sechdrs)
        return -ENOMEM;
    memcpy(li->sechdrs, li->data + hdr->e_shoff, (size_t)li->shnum * sizeof(Elf64_Shdr));

    if (li->sechdrs[hdr->e_shstrndx].sh_offset >= li->len) {
        kpm_error(li, "section name table is out of bounds");
        return -ENOEXEC;
    }
    li->secstrings = li->data + li->sechdrs[hdr->e_shstrndx].sh_offset;

    for (i = 1; i < li->shnum; i++) {
        Elf64_Shdr *s = &li->sechdrs[i];

        if (s->sh_type != SHT_NOBITS) {
            if (s->sh_offset > li->len || s->sh_size > li->len - s->sh_offset) {
                kpm_error(li, "section data is out of bounds");
                return -ENOEXEC;
            }
            /* Non-allocated sections keep pointing into the source image. */
            s->sh_addr = (unsigned long)(li->data + s->sh_offset);
        } else {
            s->sh_addr = 0;
        }
    }

    if (!kpm_find_sec(li, KPM_SEC_INIT) || !kpm_find_sec(li, KPM_SEC_EXIT)) {
        kpm_error(li, "module has no .kpm.init/.kpm.exit section");
        return -ENOEXEC;
    }
    if (!kpm_find_sec(li, KPM_SEC_INFO)) {
        kpm_error(li, "module has no .kpm.info section");
        return -ENOEXEC;
    }

    for (i = 1; i < li->shnum; i++) {
        if (li->sechdrs[i].sh_type == SHT_SYMTAB) {
            unsigned int link = li->sechdrs[i].sh_link;

            if (link >= li->shnum) {
                kpm_error(li, "symbol table links to an invalid string table");
                return -ENOEXEC;
            }
            li->symindex = i;
            li->strtab = (const char *)li->sechdrs[link].sh_addr;
            break;
        }
    }
    if (!li->symindex) {
        kpm_error(li, "module has no symbol table (stripped?)");
        return -ENOEXEC;
    }

    return 0;
}

/*
 * Assign every SHF_ALLOC section an offset inside the image: executable first,
 * then read-only, then writable, with the boundaries page aligned. sh_entsize
 * is unused for these sections and holds the assigned offset meanwhile.
 */
static void kpm_layout(struct kpm_load_info *li)
{
    unsigned int i;
    int pass;
    size_t off = 0;

    for (i = 1; i < li->shnum; i++)
        li->sechdrs[i].sh_entsize = ~0ULL;

    for (pass = 0; pass < 3; pass++) {
        for (i = 1; i < li->shnum; i++) {
            Elf64_Shdr *s = &li->sechdrs[i];
            bool exec, write, want;

            if (!(s->sh_flags & SHF_ALLOC) || s->sh_entsize != ~0ULL)
                continue;

            exec = (s->sh_flags & SHF_EXECINSTR) != 0;
            write = (s->sh_flags & SHF_WRITE) != 0;
            if (pass == 0)
                want = exec;
            else if (pass == 1)
                want = !exec && !write;
            else
                want = !exec && write;
            if (!want)
                continue;

            off = ALIGN(off, s->sh_addralign ? s->sh_addralign : KPM_MIN_ALIGN);
            s->sh_entsize = off;
            off += s->sh_size;
        }

        if (pass == 0) {
            off = PAGE_ALIGN(off);
            li->text_size = off;
        } else if (pass == 1) {
            off = PAGE_ALIGN(off);
        }
    }

    li->image_size = PAGE_ALIGN(off);
}

static int kpm_write_image(struct kpm_load_info *li, struct kpm_module *mod)
{
    unsigned int i;

    if (!li->image_size) {
        kpm_error(li, "module has no loadable sections");
        return -ENOEXEC;
    }

    mod->image = kpm_alloc_exec(li->image_size);
    if (!mod->image) {
        kpm_error(li, "cannot allocate executable memory for the module");
        return -ENOMEM;
    }
    mod->image_size = li->image_size;
    mod->image_pages = li->image_size >> PAGE_SHIFT;
    memset(mod->image, 0, li->image_size);

    for (i = 1; i < li->shnum; i++) {
        Elf64_Shdr *s = &li->sechdrs[i];
        char *dest;

        if (!(s->sh_flags & SHF_ALLOC))
            continue;

        dest = (char *)mod->image + s->sh_entsize;
        if (s->sh_type != SHT_NOBITS && s->sh_size)
            memcpy(dest, (void *)s->sh_addr, s->sh_size);
        s->sh_addr = (unsigned long)dest;
    }

    return 0;
}

static int kpm_simplify_symbols(struct kpm_load_info *li, struct kpm_module *mod)
{
    Elf64_Shdr *symsec = &li->sechdrs[li->symindex];
    Elf64_Sym *sym = (Elf64_Sym *)symsec->sh_addr;
    unsigned int i, count = symsec->sh_size / sizeof(Elf64_Sym);
    int ret = 0;

    for (i = 1; i < count; i++) {
        const char *name = li->strtab + sym[i].st_name;

        switch (sym[i].st_shndx) {
        case SHN_UNDEF: {
            unsigned long addr = kpm_symbol_lookup(name);

            if (!addr) {
                if (li->err && li->err_len)
                    snprintf(li->err, li->err_len, "unknown symbol: %.64s", name);
                pr_err("kpm: unknown symbol: %s\n", name);
                ret = -ENOENT;
                break;
            }
            sym[i].st_value = addr;
            break;
        }
        case SHN_ABS:
            break;
        case SHN_COMMON:
            if (li->err && li->err_len)
                snprintf(li->err, li->err_len, "common symbol, rebuild with -fno-common");
            ret = -ENOEXEC;
            break;
        default:
            if (sym[i].st_shndx >= li->shnum) {
                kpm_error(li, "symbol references an invalid section");
                ret = -ENOEXEC;
                break;
            }
            sym[i].st_value += li->sechdrs[sym[i].st_shndx].sh_addr;
            break;
        }

        if (ret)
            return ret;
    }

    (void)mod;
    return 0;
}

/* .kpm.info holds "key=value" NUL-terminated strings. */
static void kpm_parse_info(struct kpm_load_info *li, struct kpm_module *mod)
{
    int idx = kpm_find_sec(li, KPM_SEC_INFO);
    const char *base;
    size_t size, off = 0;

    if (!idx)
        return;

    base = (const char *)li->sechdrs[idx].sh_addr;
    size = li->sechdrs[idx].sh_size;

    while (off < size) {
        const char *s = base + off;
        size_t n = strnlen(s, size - off);
        size_t klen;

        if (!n)
            break;

#define KPM_INFO_KEY(field)                                                        \
    if (!strncmp(s, #field "=", sizeof(#field)) && n > sizeof(#field)) {           \
        klen = n - sizeof(#field);                                                 \
        if (klen >= sizeof(mod->field))                                            \
            klen = sizeof(mod->field) - 1;                                         \
        memcpy(mod->field, s + sizeof(#field), klen);                              \
        mod->field[klen] = 0;                                                      \
        off += n + 1;                                                              \
        continue;                                                                  \
    }

        KPM_INFO_KEY(name)
        KPM_INFO_KEY(version)
        KPM_INFO_KEY(license)
        KPM_INFO_KEY(author)
        KPM_INFO_KEY(description)
#undef KPM_INFO_KEY

        off += n + 1;
    }
}

static void kpm_bind_entries(struct kpm_load_info *li, struct kpm_module *mod)
{
    int idx;

    if ((idx = kpm_find_sec(li, KPM_SEC_INIT)) != 0)
        mod->init = *(kpm_initcall_t *)li->sechdrs[idx].sh_addr;
    if ((idx = kpm_find_sec(li, KPM_SEC_EXIT)) != 0)
        mod->exit = *(kpm_exitcall_t *)li->sechdrs[idx].sh_addr;
    if ((idx = kpm_find_sec(li, KPM_SEC_CTL0)) != 0)
        mod->ctl0 = *(kpm_ctl0call_t *)li->sechdrs[idx].sh_addr;
    if ((idx = kpm_find_sec(li, KPM_SEC_CTL1)) != 0)
        mod->ctl1 = *(kpm_ctl1call_t *)li->sechdrs[idx].sh_addr;
    if ((idx = kpm_find_sec(li, KPM_SEC_EVENT)) != 0)
        mod->event = *(kpm_eventcall_t *)li->sechdrs[idx].sh_addr;
}

void kpm_elf_release(struct kpm_module *mod)
{
    if (!mod)
        return;
    if (mod->image) {
        kpm_free_exec(mod->image, mod->image_size);
        mod->image = NULL;
        mod->image_size = 0;
        mod->image_pages = 0;
    }
    mod->init = NULL;
    mod->exit = NULL;
    mod->ctl0 = NULL;
    mod->ctl1 = NULL;
    mod->event = NULL;
}

int kpm_elf_load(struct kpm_module *mod, const void *data, size_t len, const char *args, char *err, size_t err_len)
{
    struct kpm_load_info li = {
        .data = data,
        .len = len,
        .err = err,
        .err_len = err_len,
    };
    int ret;

    if (err && err_len)
        err[0] = '\0';

    ret = kpm_check_header(data, len, err, err_len);
    if (ret)
        return ret;

    ret = kpm_prepare(&li);
    if (ret)
        goto out_free_headers;

    kpm_parse_info(&li, mod);

    if (!mod->name[0]) {
        kpm_error(&li, "module has no name in .kpm.info");
        ret = -ENOEXEC;
        goto out_free_headers;
    }
    if (!mod->version[0])
        snprintf(mod->version, KPM_VERSION_LEN, "unknown");

    kpm_layout(&li);

    ret = kpm_write_image(&li, mod);
    if (ret)
        goto out_free_headers;

    ret = kpm_simplify_symbols(&li, mod);
    if (ret)
        goto out_release_image;

    ret = kpm_apply_relocations(li.sechdrs, li.shnum, li.symindex);
    if (ret)
        goto out_release_image;

    if (li.text_size)
        kpm_flush_icache((unsigned long)mod->image, (unsigned long)mod->image + li.text_size);

    kpm_bind_entries(&li, mod);

    if (!mod->init || !mod->exit) {
        kpm_error(&li, "module has no init/exit entry");
        ret = -ENOEXEC;
        goto out_release_image;
    }

    if (args && args[0]) {
        mod->args = kstrdup(args, GFP_KERNEL);
        if (!mod->args) {
            ret = -ENOMEM;
            goto out_release_image;
        }
    }

    /* Text is read-only from here on; data pages stay writable. */
    kpm_protect_exec(mod->image, li.text_size);

    ret = mod->init(mod->args, "load-file", NULL);
    if (ret) {
        pr_err("kpm: init of %s failed: %d\n", mod->name, ret);
        if (li.err && li.err_len)
            snprintf(li.err, li.err_len, "module init failed: %d", ret);
        mod->exit(NULL);
        goto out_release_image;
    }

    kfree(li.sechdrs);
    return 0;

out_release_image:
    kpm_elf_release(mod);
    kfree(mod->args);
    mod->args = NULL;
out_free_headers:
    kfree(li.sechdrs);
    return ret;
}
