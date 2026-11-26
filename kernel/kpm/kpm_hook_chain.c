// SPDX-License-Identifier: GPL-2.0
/*
 * before/after callback chains for wrapped functions.
 *
 * Ordering note: callbacks run in registration order and are appended, which is
 * what kpatch does; `after` callbacks run in the same order rather than reversed
 * so that a module can rely on a single sequence.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>

#include "klog.h" // IWYU pragma: keep
#include "kpm/kpm_hook_chain.h"

int kpm_hook_chain_add(struct kpm_hook_chain *chain, u64 target, kpm_hook_before_t before, kpm_hook_after_t after,
                       void *udata)
{
    unsigned int i;
    struct kpm_hook_chain_item *slot = NULL;

    if (!chain || (!before && !after))
        return -EINVAL;
    if (chain->count && chain->target != target)
        return -EINVAL;

    for (i = 0; i < KPM_HOOK_CHAIN_NUM; i++) {
        struct kpm_hook_chain_item *item = &chain->items[i];

        if (item->used) {
            if (item->before == before && item->after == after)
                return -EEXIST;
            continue;
        }
        if (!slot)
            slot = item;
    }

    if (!slot)
        return -ENOMEM;

    slot->before = before;
    slot->after = after;
    slot->udata = udata;
    slot->used = true;
    chain->target = target;
    chain->count++;

    return 0;
}

int kpm_hook_chain_remove(struct kpm_hook_chain *chain, kpm_hook_before_t before, kpm_hook_after_t after)
{
    unsigned int i;

    if (!chain)
        return -EINVAL;

    for (i = 0; i < KPM_HOOK_CHAIN_NUM; i++) {
        struct kpm_hook_chain_item *item = &chain->items[i];

        if (item->used && item->before == before && item->after == after) {
            memset(item, 0, sizeof(*item));
            chain->count--;
            return 0;
        }
    }

    return -ENOENT;
}

const struct kpm_hook_chain_item *kpm_hook_chain_first(const struct kpm_hook_chain *chain)
{
    if (!chain)
        return NULL;
    return kpm_hook_chain_next(chain, NULL);
}

const struct kpm_hook_chain_item *kpm_hook_chain_next(const struct kpm_hook_chain *chain,
                                                      const struct kpm_hook_chain_item *item)
{
    unsigned int i;

    if (!chain)
        return NULL;

    i = item ? (unsigned int)(item - chain->items) + 1 : 0;
    for (; i < KPM_HOOK_CHAIN_NUM; i++) {
        if (chain->items[i].used)
            return &chain->items[i];
    }

    return NULL;
}
