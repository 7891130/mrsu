#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "infra/symbol_resolver.h"
#include "klog.h" // IWYU pragma: keep
#include "kpm/kpm.h"

/*
 * Executable memory for loaded .kpm images and hook trampolines.
 *
 * Two generations of kernel API are needed, because the module allocator was
 * replaced in 6.10 and both sides are in the KMI matrix:
 *
 *   5.10 .. 6.9 : module_alloc(size) / module_memfree(addr)
 *   6.10 ..     : execmem_alloc(EXECMEM_MODULE_TEXT, size) / execmem_free(addr)
 *
 * Neither is exported to modules, so both are resolved by name at first use.
 * Resolving at runtime means a wrong guess here would not fail the build, only
 * the load on an affected kernel -- hence the explicit fallback.
 */
typedef void *(*kpm_module_alloc_t)(unsigned long size);
typedef void (*kpm_module_memfree_t)(void *addr);
typedef void *(*kpm_execmem_alloc_t)(int type, size_t size);
typedef void (*kpm_execmem_free_t)(void *ptr);
typedef int (*kpm_set_memory_ro_t)(unsigned long addr, int numpages);

/* enum execmem_type: EXECMEM_DEFAULT is 0, EXECMEM_MODULE_TEXT is 1. */
#define KPM_EXECMEM_MODULE_TEXT 1

static kpm_module_alloc_t kpm_module_alloc_fn;
static kpm_module_memfree_t kpm_module_memfree_fn;
static kpm_execmem_alloc_t kpm_execmem_alloc_fn;
static kpm_execmem_free_t kpm_execmem_free_fn;
static kpm_set_memory_ro_t kpm_set_memory_ro_fn;
static bool kpm_use_execmem;
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

    if (kpm_module_alloc_fn && kpm_module_memfree_fn) {
        kpm_use_execmem = false;
        ready = true;
    } else {
        kpm_execmem_alloc_fn = (kpm_execmem_alloc_t)find_kernel_symbol_exact("execmem_alloc");
        kpm_execmem_free_fn = (kpm_execmem_free_t)find_kernel_symbol_exact("execmem_free");
        kpm_use_execmem = kpm_execmem_alloc_fn && kpm_execmem_free_fn;
        ready = kpm_use_execmem;
    }

    kpm_set_memory_ro_fn = (kpm_set_memory_ro_t)find_kernel_symbol_exact("set_memory_ro");

    kpm_alloc_ready = ready;
    kpm_alloc_probed = true;
    mutex_unlock(&kpm_alloc_mutex);

    if (!ready)
        pr_err("kpm: neither module_alloc nor execmem_alloc is available, cannot load modules\n");
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

    if (kpm_use_execmem)
        return kpm_execmem_alloc_fn(KPM_EXECMEM_MODULE_TEXT, PAGE_ALIGN(size));

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
    if (!addr)
        return;

    if (kpm_use_execmem) {
        if (kpm_execmem_free_fn)
            kpm_execmem_free_fn(addr);
        return;
    }

    if (kpm_module_memfree_fn)
        kpm_module_memfree_fn(addr);
}
