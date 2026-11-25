#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/types.h>

#include "infra/symbol_resolver.h"
#include "klog.h" // IWYU pragma: keep
#include "kpm/kpm.h"

/*
 * Executable memory for loaded .kpm images.
 *
 * We reuse the kernel module allocator so the image lives inside the module
 * mapping (RW while we copy and relocate, then flipped read-only), instead of
 * hand-rolling page tables the way kpatch does from inside the kernel image.
 * The functions are not exported to modules, so they are resolved by name.
 */
typedef void *(*kpm_module_alloc_t)(unsigned long size);
typedef void (*kpm_module_memfree_t)(void *addr);
typedef int (*kpm_set_memory_ro_t)(unsigned long addr, int numpages);

static kpm_module_alloc_t kpm_module_alloc_fn;
static kpm_module_memfree_t kpm_module_memfree_fn;
static kpm_set_memory_ro_t kpm_set_memory_ro_fn;
static bool kpm_alloc_probed;
static bool kpm_alloc_ready;
static DEFINE_MUTEX(kpm_alloc_mutex);

static bool __nocfi kpm_resolve_allocator(void)
{
    bool ready;

    mutex_lock(&kpm_alloc_mutex);
    if (kpm_alloc_probed) {
        ready = kpm_alloc_ready;
        mutex_unlock(&kpm_alloc_mutex);
        return ready;
    }

    kpm_module_alloc_fn = (kpm_module_alloc_t)find_kernel_symbol_exact("module_alloc");
    kpm_module_memfree_fn = (kpm_module_memfree_t)find_kernel_symbol_exact("module_memfree");
    kpm_set_memory_ro_fn = (kpm_set_memory_ro_t)find_kernel_symbol_exact("set_memory_ro");

    kpm_alloc_ready = kpm_module_alloc_fn && kpm_module_memfree_fn;
    kpm_alloc_probed = true;
    ready = kpm_alloc_ready;
    mutex_unlock(&kpm_alloc_mutex);

    if (!ready)
        pr_err("kpm: module_alloc/module_memfree not found, cannot load modules\n");
    else if (!kpm_set_memory_ro_fn)
        pr_warn("kpm: set_memory_ro not found, module text stays writable\n");

    return ready;
}

void *kpm_alloc_exec(size_t size)
{
    if (!size)
        return NULL;
    if (!kpm_resolve_allocator())
        return NULL;
    return kpm_module_alloc_fn(PAGE_ALIGN(size));
}

int __nocfi kpm_protect_exec(void *addr, size_t size)
{
    int ret;

    if (!addr || !size)
        return -EINVAL;
    if (!kpm_set_memory_ro_fn)
        return 0;

    ret = kpm_set_memory_ro_fn((unsigned long)addr & PAGE_MASK, PAGE_ALIGN(size) >> PAGE_SHIFT);
    if (ret)
        pr_warn("kpm: set_memory_ro failed: %d\n", ret);
    return ret;
}

void __nocfi kpm_free_exec(void *addr, size_t size)
{
    (void)size;
    if (!addr || !kpm_module_memfree_fn)
        return;
    kpm_module_memfree_fn(addr);
}
