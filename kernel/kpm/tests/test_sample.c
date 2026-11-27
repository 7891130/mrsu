/*
 * Host-side test for a *real* .kpm: loads samples/ptest/ptest.kpm through the
 * shipping loader (kernel/kpm/kpm_elf.c).
 *
 * The other suites build their images by hand, which proves the loader against
 * the layout its author had in mind. This one uses an image produced by a
 * compiler (NDK clang, aarch64 ET_REL) from the sample source, so it also
 * covers what a toolchain actually emits: ADRP+ADD into .bss, ABS64 in the
 * entry slots, a NOBITS section, .comment/.note.GNU-stack that must be skipped.
 *
 * The sample defines no external symbols on purpose, so a failure here can only
 * be the loader, never a missing kfunc.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/types.h>

#include "kpm/kpm.h"

/* --- stubs the loader links against -------------------------------------- */

void *kpm_alloc_exec(size_t size)
{
    return calloc(1, size);
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
    free(addr);
}

/* The sample has no undefined symbols; the resolver must never be needed. */
static int resolve_calls;

u64 kpm_symbol_lookup(const char *name)
{
    resolve_calls++;
    fprintf(stderr, "unexpected symbol lookup: %s\n", name ? name : "(null)");
    return 0;
}

/* --- harness ------------------------------------------------------------- */

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

static unsigned char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long size;

    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        fprintf(stderr, "cannot size %s\n", path);
        return NULL;
    }
    buf = malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(buf);
        fprintf(stderr, "cannot read %s\n", path);
        return NULL;
    }
    fclose(f);
    *len = (size_t)size;
    return buf;
}

int main(int argc, char **argv)
{
    struct kpm_module mod;
    char err[200];
    unsigned char *data;
    size_t len = 0;
    long rc;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc < 2) {
        fprintf(stderr, "usage: %s <ptest.kpm>\n", argv[0]);
        return 2;
    }

    data = read_file(argv[1], &len);
    if (!data)
        return 2;

    puts("loading the compiler-built sample");
    memset(&mod, 0, sizeof(mod));
    err[0] = '\0';
    rc = kpm_elf_load(&mod, data, len, "arg=hello", NULL, err, sizeof(err));
    printf("  loader rc=%ld err=\"%s\"\n", rc, err);
    check(rc == 0, "the real loader accepts a clang-built .kpm");

    if (rc != 0) {
        free(data);
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }

    puts("metadata");
    check(strcmp(mod.name, "ptest") == 0, ".kpm.info name");
    check(strcmp(mod.version, "0.1") == 0, ".kpm.info version");
    check(strcmp(mod.license, "GPL") == 0, ".kpm.info license");
    check(strcmp(mod.author, "MRSU") == 0, ".kpm.info author");
    check(strstr(mod.description, "smoke test") != NULL, ".kpm.info description");
    check(mod.args && strcmp(mod.args, "arg=hello") == 0, "module args kept");

    puts("entries");
    check(mod.init != NULL, ".kpm.init slot bound");
    check(mod.exit != NULL, ".kpm.exit slot bound");
    check(mod.ctl0 != NULL, ".kpm.ctl0 slot bound");
    check(mod.ctl1 != NULL, ".kpm.ctl1 slot bound");
    check(mod.event != NULL, ".kpm.event slot bound");

    puts("calling into the loaded image");
    check(mod.init != NULL && mod.init("arg=hello", "load-file", NULL) == 0, "init returns 0");
    check(mod.ctl1 != NULL && mod.ctl1((void *)1, (void *)2, (void *)3) == 6, "ctl1 sums its args");
    check(mod.ctl0 != NULL && mod.ctl0("hello", NULL, 0) == 5, "ctl0 reports the argument length");
    check(mod.event != NULL && mod.event("boot-completed", NULL, NULL) == 0, "event handler returns 0");
    check(mod.exit != NULL && mod.exit(NULL) == 0, "exit returns 0");
    check(resolve_calls == 0, "no symbol resolution was needed");

    puts("release");
    kpm_elf_release(&mod);
    free(data);

    if (failures) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
