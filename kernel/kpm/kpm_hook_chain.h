#ifndef __KSU_H_KPM_HOOK_CHAIN
#define __KSU_H_KPM_HOOK_CHAIN

#include <linux/types.h>

#include "kpm/kpm_hook_fargs.h"

/*
 * Chain of before/after callbacks attached to one hooked function: what
 * KernelPatch's hook_wrap()/hook_unwrap() manage.
 *
 * Kept separate from the dispatcher so the bookkeeping (ordering, capacity,
 * duplicate detection) can be unit-tested on a host.
 */
struct kpm_hook_chain_item {
    kpm_hook_before_t before;
    kpm_hook_after_t after;
    void *udata;
    bool used;
};

struct kpm_hook_chain {
    u64 target;
    struct kpm_hook_chain_item items[KPM_HOOK_CHAIN_NUM];
    unsigned int count;
};

/* Returns 0, -ENOMEM when the chain is full, -EEXIST for a duplicate pair. */
int kpm_hook_chain_add(struct kpm_hook_chain *chain, u64 target, kpm_hook_before_t before, kpm_hook_after_t after,
                       void *udata);
/* Returns 0, or -ENOENT when the pair is not registered. */
int kpm_hook_chain_remove(struct kpm_hook_chain *chain, kpm_hook_before_t before, kpm_hook_after_t after);

/* Iteration helpers: return NULL when there is no next used item. */
const struct kpm_hook_chain_item *kpm_hook_chain_first(const struct kpm_hook_chain *chain);
const struct kpm_hook_chain_item *kpm_hook_chain_next(const struct kpm_hook_chain *chain,
                                                      const struct kpm_hook_chain_item *item);

#endif // __KSU_H_KPM_HOOK_CHAIN
