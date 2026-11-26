// SPDX-License-Identifier: GPL-2.0
/*
 * Registry of installed inline hooks.
 *
 * Pure bookkeeping: no text patching, no cache maintenance, no allocation.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/types.h>

#include "klog.h" // IWYU pragma: keep
#include "kpm/kpm_hook_table.h"

static LIST_HEAD(kpm_hooks);
static DEFINE_MUTEX(kpm_hooks_lock);

void kpm_hook_table_init(void)
{
    mutex_lock(&kpm_hooks_lock);
    /* Nothing can be installed before init, but be explicit for reloads. */
    INIT_LIST_HEAD(&kpm_hooks);
    mutex_unlock(&kpm_hooks_lock);
}

static struct kpm_hook_entry *find_locked(u64 target)
{
    struct kpm_hook_entry *pos;

    list_for_each_entry (pos, &kpm_hooks, list) {
        if (pos->target == target)
            return pos;
    }
    return NULL;
}

int kpm_hook_table_add(struct kpm_hook_entry *entry)
{
    int ret = 0;

    if (!entry)
        return -EINVAL;

    mutex_lock(&kpm_hooks_lock);
    if (find_locked(entry->target)) {
        ret = -EEXIST;
    } else {
        list_add_tail(&entry->list, &kpm_hooks);
    }
    mutex_unlock(&kpm_hooks_lock);

    return ret;
}

struct kpm_hook_entry *kpm_hook_table_find(u64 target)
{
    struct kpm_hook_entry *entry;

    mutex_lock(&kpm_hooks_lock);
    entry = find_locked(target);
    mutex_unlock(&kpm_hooks_lock);

    return entry;
}

struct kpm_hook_entry *kpm_hook_table_remove(u64 target)
{
    struct kpm_hook_entry *entry;

    mutex_lock(&kpm_hooks_lock);
    entry = find_locked(target);
    if (entry)
        list_del(&entry->list);
    mutex_unlock(&kpm_hooks_lock);

    return entry;
}

unsigned int kpm_hook_table_count(void)
{
    struct kpm_hook_entry *pos;
    unsigned int n = 0;

    mutex_lock(&kpm_hooks_lock);
    list_for_each_entry (pos, &kpm_hooks, list)
        n++;
    mutex_unlock(&kpm_hooks_lock);

    return n;
}

int kpm_hook_table_list(char *out, size_t size)
{
    struct kpm_hook_entry *pos;
    size_t off = 0;

    if (!out || !size)
        return -EINVAL;
    out[0] = '\0';

    mutex_lock(&kpm_hooks_lock);
    list_for_each_entry (pos, &kpm_hooks, list) {
        int n = snprintf(out + off, size - off, "0x%llx -> 0x%llx %s\n", (unsigned long long)pos->target,
                         (unsigned long long)pos->replacement, pos->owner ? pos->owner : "");

        if (n < 0)
            break;
        if ((size_t)n >= size - off) {
            off = size - 1;
            break;
        }
        off += (size_t)n;
    }
    mutex_unlock(&kpm_hooks_lock);

    return (int)off;
}

void kpm_hook_table_exit(void)
{
    /* Entries are owned by whoever installed them; the table just forgets. */
    mutex_lock(&kpm_hooks_lock);
    INIT_LIST_HEAD(&kpm_hooks);
    mutex_unlock(&kpm_hooks_lock);
}
