// SPDX-License-Identifier: GPL-2.0
/*
 * hook_wrap: run before/after callbacks around a function whose prologue has
 * been replaced.
 *
 * The generated stub stays tiny -- build a frame, call the dispatcher, return
 * the saved result -- because calling the original happens in the dispatcher:
 * the trampoline is an ordinary AArch64 function (displaced prologue plus a
 * jump back), so it can be called through a function pointer with the ABI of
 * the hooked function.
 *
 * Ordering matters and is why kpm_hook_install_with() exists: the stub needs
 * the trampoline address, and the trampoline is created by the installer.
 * Building the replacement only after the trampoline exists means the target is
 * never patched to point at something that cannot yet run.
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include "klog.h" // IWYU pragma: keep
#include "kpm/kpm.h"
#include "kpm/kpm_hook.h"
#include "kpm/kpm_hook_chain.h"
#include "kpm/kpm_hook_fargs.h"
#include "kpm/kpm_hook_table.h"
#include "kpm/kpm_insn.h"

/* Frame layout shared with kpm_hook_fargs.h: chain, skip, local[8], ret, args. */
#define KPM_WRAP_RET_OFF 80
#define KPM_WRAP_ARGS_OFF 88
#define KPM_WRAP_FRAME_SIZE 160 /* 88 + 8*8, rounded up to 16 */

#define KPM_WRAP_MAX_ARGS 8
#define KPM_WRAP_STUB_INSNS 15
#define KPM_WRAP_STUB_SIZE (KPM_WRAP_STUB_INSNS * 4 + 8) /* instructions + ctx literal */

struct kpm_wrap_ctx {
    struct kpm_hook_chain chain;
    u64 target;
    u64 orig;
    unsigned int argno;
    void *stub;
    size_t stub_size;
};

static void __nocfi kpm_wrap_dispatch(struct kpm_hook_fargs *fargs, struct kpm_wrap_ctx *ctx)
{
    const struct kpm_hook_chain_item *item;

    for (item = kpm_hook_chain_first(&ctx->chain); item; item = kpm_hook_chain_next(&ctx->chain, item)) {
        if (item->before)
            ((kpm_hook_before_t)item->before)(fargs, item->udata);
    }

    if (!fargs->skip_origin) {
        switch (ctx->argno) {
        case 0:
            fargs->ret = ((u64 (*)(void))ctx->orig)();
            break;
        case 1:
            fargs->ret = ((u64 (*)(u64))ctx->orig)(fargs->args[0]);
            break;
        case 2:
            fargs->ret = ((u64 (*)(u64, u64))ctx->orig)(fargs->args[0], fargs->args[1]);
            break;
        case 3:
            fargs->ret = ((u64 (*)(u64, u64, u64))ctx->orig)(fargs->args[0], fargs->args[1], fargs->args[2]);
            break;
        case 4:
            fargs->ret = ((u64 (*)(u64, u64, u64, u64))ctx->orig)(fargs->args[0], fargs->args[1], fargs->args[2],
                                                                  fargs->args[3]);
            break;
        case 5:
            fargs->ret = ((u64 (*)(u64, u64, u64, u64, u64))ctx->orig)(fargs->args[0], fargs->args[1], fargs->args[2],
                                                                       fargs->args[3], fargs->args[4]);
            break;
        case 6:
            fargs->ret = ((u64 (*)(u64, u64, u64, u64, u64, u64))ctx->orig)(
                fargs->args[0], fargs->args[1], fargs->args[2], fargs->args[3], fargs->args[4], fargs->args[5]);
            break;
        default:
            fargs->ret = ((u64 (*)(u64, u64, u64, u64, u64, u64, u64, u64))ctx->orig)(fargs->args[0], fargs->args[1],
                                                                                      fargs->args[2], fargs->args[3],
                                                                                      fargs->args[4], fargs->args[5],
                                                                                      fargs->args[6], fargs->args[7]);
            break;
        }
    }

    for (item = kpm_hook_chain_first(&ctx->chain); item; item = kpm_hook_chain_next(&ctx->chain, item)) {
        if (item->after)
            ((kpm_hook_after_t)item->after)(fargs, item->udata);
    }
}

/* Called by the installer once the trampoline address is known. */
static u64 __nocfi kpm_wrap_build(u64 trampoline_pc, void *arg)
{
    struct kpm_wrap_ctx *ctx = arg;
    u32 *p;
    u64 stub_addr;

    ctx->orig = trampoline_pc;

    ctx->stub = kpm_alloc_exec(KPM_WRAP_STUB_SIZE);
    if (!ctx->stub)
        return 0;
    ctx->stub_size = KPM_WRAP_STUB_SIZE;
    stub_addr = (u64)(uintptr_t)ctx->stub;

    p = ctx->stub;
    p[0] = kpm_insn_gen_ldstp_mode(29, 30, 31, -16, true, false, KPM_LDST_PRE_INDEX);
    p[1] = kpm_insn_gen_add_sub_imm(29, 31, 0, true, false); /* mov x29, sp */
    p[2] = kpm_insn_gen_add_sub_imm(31, 31, KPM_WRAP_FRAME_SIZE, true, true);
    p[3] = kpm_insn_gen_ldstp(0, 1, 31, KPM_WRAP_ARGS_OFF, true, false);
    p[4] = kpm_insn_gen_ldstp(2, 3, 31, KPM_WRAP_ARGS_OFF + 16, true, false);
    p[5] = kpm_insn_gen_ldstp(4, 5, 31, KPM_WRAP_ARGS_OFF + 32, true, false);
    p[6] = kpm_insn_gen_ldstp(6, 7, 31, KPM_WRAP_ARGS_OFF + 48, true, false);
    p[7] = kpm_insn_gen_ldst_imm(31, 31, KPM_WRAP_RET_OFF, true, false); /* str xzr, ret */
    /* ldr x1, <ctx> ; str x1, [sp] (fargs->chain) ; mov x0, sp ; bl dispatcher */
    p[8] = kpm_insn_gen_ldr_literal(1, KPM_WRAP_STUB_INSNS * 4 - 8 * 4);
    p[9] = kpm_insn_gen_ldst_imm(1, 31, 0, true, false);
    p[10] = kpm_insn_gen_add_sub_imm(0, 31, 0, true, false); /* mov x0, sp */
    p[11] = kpm_insn_gen_branch_imm(stub_addr + 11 * 4, (u64)(uintptr_t)kpm_wrap_dispatch, true);
    p[12] = kpm_insn_gen_ldst_imm(0, 31, KPM_WRAP_RET_OFF, true, true);
    p[13] = kpm_insn_gen_add_sub_imm(31, 31, KPM_WRAP_FRAME_SIZE, true, false);
    p[14] = kpm_insn_gen_ldstp_mode(29, 30, 31, 16, true, true, KPM_LDST_POST_INDEX);

    if (!p[8] || !p[11]) {
        kpm_free_exec(ctx->stub, ctx->stub_size);
        ctx->stub = NULL;
        return 0;
    }

    ((u64 *)(p + KPM_WRAP_STUB_INSNS))[0] = (u64)(uintptr_t)ctx;
    kpm_protect_exec(ctx->stub, ctx->stub_size);

    return stub_addr;
}

int kpm_hook_wrap(u64 target, unsigned int argno, kpm_hook_before_t before, kpm_hook_after_t after, void *udata,
                  char *err, size_t err_len)
{
    struct kpm_wrap_ctx *ctx;
    int ret;

    if (!target || argno > KPM_WRAP_MAX_ARGS) {
        if (err && err_len)
            snprintf(err, err_len, "invalid hook_wrap arguments");
        return -EINVAL;
    }

    ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx)
        return -ENOMEM;

    ctx->target = target;
    ctx->argno = argno;

    ret = kpm_hook_chain_add(&ctx->chain, target, before, after, udata);
    if (ret) {
        kfree(ctx);
        return ret;
    }

    ret = kpm_hook_install_with(target, kpm_wrap_build, ctx, "hook_wrap", err, err_len);
    if (ret) {
        kfree(ctx);
        return ret;
    }

    return 0;
}

int kpm_hook_unwrap_remove(u64 target, kpm_hook_before_t before, kpm_hook_after_t after, int remove)
{
    struct kpm_hook_entry *entry;
    struct kpm_wrap_ctx *ctx;
    void *stub;
    size_t stub_size;
    int ret;

    entry = kpm_hook_table_find(target);
    if (!entry || !entry->priv)
        return -ENOENT;

    ctx = entry->priv;
    ret = kpm_hook_chain_remove(&ctx->chain, before, after);
    if (ret)
        return ret;

    if (!remove && ctx->chain.count)
        return 0;

    stub = ctx->stub;
    stub_size = ctx->stub_size;

    /* Uninstall frees the registry entry, so nothing may touch it afterwards. */
    kpm_hook_uninstall(target);
    if (stub)
        kpm_free_exec(stub, stub_size);
    kfree(ctx);
    return 0;
}
