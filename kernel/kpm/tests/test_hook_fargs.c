/*
 * Host-side tests for the hook frame ABI and the callback chains.
 *
 * The frame offsets are the important part: .kpm modules are compiled against
 * kpatch's hook.h and address these fields by offset, so a layout mismatch
 * would corrupt arguments or return values at runtime rather than fail to
 * compile.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kpm/kpm_hook_chain.h"
#include "kpm/kpm_hook_fargs.h"

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

static void before_a(void *fargs, void *udata) { (void)fargs; (void)udata; }
static void before_b(void *fargs, void *udata) { (void)fargs; (void)udata; }
static void after_a(void *fargs, void *udata) { (void)fargs; (void)udata; }
static void after_b(void *fargs, void *udata) { (void)fargs; (void)udata; }

static void test_fargs_layout(void)
{
    printf("frame layout matches kpatch\n");
    check(sizeof(kpm_hook_local_t) == 64, "local area is 8 x u64");
    check(KPM_HOOK_LOCAL_DATA_NUM == 8, "local slots");
    check(KPM_HOOK_CHAIN_NUM == 0x10, "chain capacity");

    /* void *chain (8) + int skip_origin (4) + pad (4) + local (64) + ret (8) */
    check(offsetof(struct kpm_hook_fargs0, chain) == 0, "chain at 0");
    check(offsetof(struct kpm_hook_fargs0, skip_origin) == 8, "skip_origin at 8");
    check(offsetof(struct kpm_hook_fargs0, local) == 16, "local at 16");
    check(offsetof(struct kpm_hook_fargs0, ret) == 80, "ret at 80");
    check(sizeof(struct kpm_hook_fargs0) == 88, "frame0 size");

    check(offsetof(struct kpm_hook_fargs4, arg0) == 88, "arg0 right after ret");
    check(offsetof(struct kpm_hook_fargs4, arg3) == 88 + 24, "arg3 offset");
    check(sizeof(struct kpm_hook_fargs4) == 88 + 32, "frame4 size");
    check(offsetof(struct kpm_hook_fargs, args) == 88, "flexible args start");
}

static void test_chain(void)
{
    static struct kpm_hook_chain chain;
    const struct kpm_hook_chain_item *item;
    unsigned int seen = 0;

    printf("chain bookkeeping\n");
    memset(&chain, 0, sizeof(chain));

    check(kpm_hook_chain_add(&chain, 0x1000, before_a, after_a, (void *)0x1) == 0, "adds first pair");
    check(kpm_hook_chain_add(&chain, 0x1000, before_b, NULL, (void *)0x2) == 0, "adds second pair");
    check(chain.count == 2, "count is two");
    check(chain.target == 0x1000, "target recorded");

    check(kpm_hook_chain_add(&chain, 0x1000, before_a, after_a, NULL) == -EEXIST, "duplicate pair rejected");
    check(kpm_hook_chain_add(&chain, 0x2000, before_a, after_b, NULL) == -EINVAL, "different target rejected");
    check(kpm_hook_chain_add(&chain, 0x1000, NULL, NULL, NULL) == -EINVAL, "empty pair rejected");

    printf("chain order\n");
    for (item = kpm_hook_chain_first(&chain); item; item = kpm_hook_chain_next(&chain, item)) {
        if (seen == 0)
            check(item->before == before_a && item->udata == (void *)0x1, "first item in registration order");
        if (seen == 1)
            check(item->before == before_b && item->after == NULL, "second item keeps its NULL after");
        seen++;
    }
    check(seen == 2, "iteration visits both items");

    printf("chain removal\n");
    check(kpm_hook_chain_remove(&chain, before_a, after_a) == 0, "removes the first pair");
    check(chain.count == 1, "count drops");
    check(kpm_hook_chain_remove(&chain, before_a, after_a) == -ENOENT, "removing twice is reported");
    check(kpm_hook_chain_first(&chain)->before == before_b, "remaining item is still reachable");

    printf("chain capacity\n");
    memset(&chain, 0, sizeof(chain));
    {
        unsigned int i;
        int rc = 0;

        /* Fill using distinct before pointers would need many functions; reuse
           capacity checks via a direct fill of the item array instead. */
        for (i = 0; i < KPM_HOOK_CHAIN_NUM; i++) {
            chain.items[i].used = true;
            chain.items[i].before = before_a;
            chain.items[i].after = (kpm_hook_after_t)(uintptr_t)(i + 1);
            chain.items[i].udata = (void *)(uintptr_t)i;
        }
        chain.count = KPM_HOOK_CHAIN_NUM;
        chain.target = 0x1000;
        rc = kpm_hook_chain_add(&chain, 0x1000, before_b, NULL, NULL);
        check(rc == -ENOMEM, "full chain reports -ENOMEM");
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    test_fargs_layout();
    test_chain();

    if (failures) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
