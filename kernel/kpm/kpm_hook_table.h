#ifndef __KSU_H_KPM_HOOK_TABLE
#define __KSU_H_KPM_HOOK_TABLE

#include <linux/list.h>
#include <linux/types.h>

#include "kpm/kpm_hook.h"

/*
 * Registry of installed inline hooks, keyed by the target address.
 *
 * Kept separate from the install glue so the bookkeeping can be unit-tested on
 * a host: the glue (trampoline allocation, text patching, cache maintenance)
 * only fills the entry in and hands it over.
 */
struct kpm_hook_entry {
    struct list_head list;
    u64 target;
    u64 replacement;
    /* Executable trampoline holding the displaced prologue and the jump back. */
    void *trampoline;
    size_t trampoline_size;
    /* Address the replacement calls to run the original function. */
    u64 trampoline_pc;
    struct kpm_hook_plan plan;
    const char *owner;
};

void kpm_hook_table_init(void);
void kpm_hook_table_exit(void);

/* Returns 0, -EEXIST when the target is already hooked. */
int kpm_hook_table_add(struct kpm_hook_entry *entry);
struct kpm_hook_entry *kpm_hook_table_find(u64 target);
/* Removes and returns the entry, or NULL when the target is not hooked. */
struct kpm_hook_entry *kpm_hook_table_remove(u64 target);

unsigned int kpm_hook_table_count(void);
/* Writes "0x<target> -> 0x<replacement> <owner>" lines; returns bytes written. */
int kpm_hook_table_list(char *out, size_t size);

#endif // __KSU_H_KPM_HOOK_TABLE
