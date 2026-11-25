#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#include "infra/symbol_resolver.h"
#include "klog.h" // IWYU pragma: keep
#include "kpm/kpm_symbol.h"

struct kpm_kfunc {
    const char *name;
    unsigned long addr;
};

#define KPM_KFUNC(sym) { .name = #sym, .addr = (unsigned long)&sym }

/*
 * kfuncs implemented by the runtime itself. This is the compatibility surface
 * KernelPatch exposes to .kpm modules; it is filled in as milestones land.
 * Keeping the table here means an unimplemented kfunc falls through to the
 * kernel symbol lookup instead of failing the whole module load.
 */
static const struct kpm_kfunc kpm_kfuncs[] = {
    /* M3+: allocator, hook engine, task/cred helpers */
    { .name = NULL, .addr = 0 },
};

unsigned long kpm_symbol_lookup(const char *name)
{
    unsigned int i;

    if (!name || !name[0])
        return 0;

    for (i = 0; i < ARRAY_SIZE(kpm_kfuncs); i++) {
        if (kpm_kfuncs[i].name && !strcmp(kpm_kfuncs[i].name, name))
            return kpm_kfuncs[i].addr;
    }

    return find_kernel_symbol_exact(name);
}
