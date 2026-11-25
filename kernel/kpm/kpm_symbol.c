#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#include "infra/symbol_resolver.h"
#include "klog.h" // IWYU pragma: keep
#include "kpm/kpm_kfunc.h"
#include "kpm/kpm_symbol.h"

struct kpm_kfunc {
    const char *name;
    unsigned long addr;
};

/*
 * kfuncs implemented by the runtime itself: the compatibility surface
 * KernelPatch exposes to .kpm modules, keyed by the name a module imports.
 * Anything absent here falls through to the kernel symbol lookup, so an
 * unimplemented kfunc only fails the modules that actually use it.
 *
 * APatch-specific helpers (su_*, kstorage_*, ap_mod_*, feature_*) are
 * intentionally not provided: importing them fails with "unknown symbol".
 */
static const struct kpm_kfunc kpm_kfuncs[] = {
    { .name = "symbol_lookup_name", .addr = (unsigned long)&kpm_kf_symbol_lookup_name },
    { .name = "kallsyms_lookup_name", .addr = (unsigned long)&kpm_kf_kallsyms_lookup_name },
    { .name = "compat_copy_to_user", .addr = (unsigned long)&kpm_kf_compat_copy_to_user },
    { .name = "compat_strncpy_from_user", .addr = (unsigned long)&kpm_kf_compat_strncpy_from_user },
    { .name = "current_uid", .addr = (unsigned long)&kpm_kf_current_uid },
    { .name = "kp_symbol_count", .addr = (unsigned long)&kpm_kf_symbol_count },
    { .name = "kp_symbol_list", .addr = (unsigned long)&kpm_kf_symbol_list },
    { .name = "kp_feature_list", .addr = (unsigned long)&kpm_kf_feature_list },
    /* data symbol: modules read the encoded kernel version */
    { .name = "kver", .addr = (unsigned long)&kpm_kver },
};

u64 kpm_symbol_lookup(const char *name)
{
    unsigned int i;

    if (!name || !name[0])
        return 0;

    for (i = 0; i < ARRAY_SIZE(kpm_kfuncs); i++) {
        if (!strcmp(kpm_kfuncs[i].name, name))
            return kpm_kfuncs[i].addr;
    }

    return (u64)find_kernel_symbol_exact(name);
}

int kpm_symbol_count(void)
{
    return (int)ARRAY_SIZE(kpm_kfuncs);
}

int kpm_symbol_list(char *out, unsigned int size)
{
    unsigned int i;
    unsigned int len = 0;

    if (!out || !size)
        return -EINVAL;
    out[0] = '\0';

    for (i = 0; i < ARRAY_SIZE(kpm_kfuncs); i++) {
        int n = snprintf(out + len, size - len, "%s\n", kpm_kfuncs[i].name);

        if (n < 0)
            break;
        if ((unsigned int)n >= size - len) {
            len = size - 1;
            break;
        }
        len += (unsigned int)n;
    }

    return (int)len;
}
