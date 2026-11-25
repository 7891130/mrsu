#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#include "klog.h" // IWYU pragma: keep
#include "kpm/kpm.h"

/*
 * .kpm ELF loader.
 *
 * The real loader lands in the next milestone: it parses the ET_REL image,
 * lays out SHF_ALLOC sections into executable memory, resolves undefined
 * symbols and applies AArch64 relocations, then hands the entry points
 * advertised through the .kpm.* sections to kpm_core.
 */
int kpm_elf_load(struct kpm_module *mod, const void *data, size_t len, const char *args)
{
    (void)data;
    (void)len;
    (void)args;

    if (mod)
        snprintf(mod->name, KPM_NAME_LEN, "%s", "kpm loader not implemented");

    pr_warn("kpm: elf loader is not implemented yet\n");
    return -ENOSYS;
}

void kpm_elf_release(struct kpm_module *mod)
{
    if (!mod)
        return;
    if (mod->image) {
        kpm_free_exec(mod->image, mod->image_size);
        mod->image = NULL;
        mod->image_size = 0;
    }
}
