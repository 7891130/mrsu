/*
 * Host-side tests for the basic kfunc layer (kernel/kpm/kpm_kfunc.c).
 *
 * The point of these tests is the *return convention*, because a .kpm module
 * branches on it: kpatch's compat_copy_to_user() reports the number of bytes
 * copied and uses 0 for failure, not a negative errno. Getting that backwards
 * makes modules behave oddly on a device rather than fail to load.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kpm/kpm_kfunc.h"
#include "kpm/kpm_symbol.h"

/* --- state shared with the stub headers ---------------------------------- */

u32 host_current_uid_value;
int host_uaccess_fault;

/* --- stubs for the symbol layer ----------------------------------------- */

static int symbol_lookups;

u64 kpm_symbol_lookup(const char *name)
{
    symbol_lookups++;
    if (!strcmp(name, "printk"))
        return 0xc0ffeeULL;
    return 0;
}

int kpm_symbol_count(void)
{
    return 13;
}

int kpm_symbol_list(char *out, unsigned int size)
{
    snprintf(out, size, "printk\nkver\n");
    return 10;
}

/* --- test helpers -------------------------------------------------------- */

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

int main(void)
{
    char buf[64];

    setvbuf(stdout, NULL, _IONBF, 0);

    printf("symbol lookup\n");
    check(kpm_kf_symbol_lookup_name("printk") == 0xc0ffeeULL, "resolves through the symbol layer");
    check(kpm_kf_symbol_lookup_name("nope") == 0, "unknown symbol yields 0");
    check(symbol_lookups == 2, "delegates instead of caching");

    printf("copy_to_user convention\n");
    memset(buf, 0, sizeof(buf));
    host_uaccess_fault = 0;
    check(kpm_kf_compat_copy_to_user(buf, "abcd", 4) == 4, "returns the copied length");
    check(memcmp(buf, "abcd", 4) == 0, "bytes actually copied");
    check(kpm_kf_compat_copy_to_user(buf, "z", 0) == 0, "zero length copies nothing");
    check(kpm_kf_compat_copy_to_user(NULL, "z", 1) == 0, "null destination reports failure");
    host_uaccess_fault = 1;
    check(kpm_kf_compat_copy_to_user(buf, "abcd", 4) == 0, "fault reports 0, not errno");
    host_uaccess_fault = 0;

    printf("strncpy_from_user\n");
    memset(buf, 0, sizeof(buf));
    check(kpm_kf_compat_strncpy_from_user(buf, "hello", sizeof(buf)) == 5, "returns the length");
    check(!strcmp(buf, "hello"), "string copied");
    check(kpm_kf_compat_strncpy_from_user(NULL, "x", 4) == -EINVAL, "null destination rejected");
    check(kpm_kf_compat_strncpy_from_user(buf, "x", 0) == -EINVAL, "zero count rejected");
    host_uaccess_fault = 1;
    check(kpm_kf_compat_strncpy_from_user(buf, "x", 4) == -EFAULT, "fault propagated");
    host_uaccess_fault = 0;

    printf("process identity\n");
    host_current_uid_value = 10123;
    check(kpm_kf_current_uid() == 10123, "reflects the current uid");
    host_current_uid_value = 0;
    check(kpm_kf_current_uid() == 0, "root reports 0");

    printf("introspection\n");
    check(kpm_kf_symbol_count() == 13, "count forwarded");
    memset(buf, 0, sizeof(buf));
    check(kpm_kf_symbol_list(buf, sizeof(buf)) == 10, "list forwarded");
    check(strstr(buf, "printk") != NULL, "list content forwarded");
    check(kpm_kf_feature_list() != NULL && kpm_kf_feature_list()[0] != '\0', "feature list is non-empty");

    printf("version symbol\n");
    check(kpm_kver == ((6 << 16) + (1 << 8)), "kver carries LINUX_VERSION_CODE");

    if (failures) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
