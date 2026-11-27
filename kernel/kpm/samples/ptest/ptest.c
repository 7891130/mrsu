/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ptest -- the smallest useful KPM.
 *
 * It exists to smoke-test the runtime end to end without depending on
 * anything: no external symbols at all, so a failed load can only mean the
 * loader (ELF parsing, section layout, relocations, entry binding) is broken,
 * not that a kfunc or a kernel symbol is missing. Once this one loads, plug
 * `hook()` or `kallsyms_lookup_name` in and the failure is out of that set.
 *
 * What a successful run proves, in order:
 *   kpm load   -> .kpm.info parsed (name/version show up in `kpm list`/`kpm info`)
 *                 + init called + its args recorded
 *   kpm ctl0   -> ctl0 dispatched (returns the length of the argument string)
 *   kpm ctl1   -> ctl1 dispatched (returns a1 + a2 + a3)
 *   boot event -> .kpm.event delivered (the counters below are bumped)
 *   kpm unload -> exit called
 *
 * The counters are only useful through `kpm info`-style tooling that can read
 * module memory; they are here to keep the code from being optimised away and
 * to be inspectable with a debugger or a second module.
 *
 * Build: see build.sh. Entry points are *pointer slots*: the loader reads an
 * 8-byte function pointer out of each `.kpm.*` section, so the sections hold
 * `&function`, not the function body.
 */
typedef unsigned long u64;
typedef unsigned int u32;

/* --- state, deliberately non-const and volatile so nothing is folded away --- */
static volatile u64 ptest_init_calls;
static volatile u64 ptest_exit_calls;
static volatile u64 ptest_ctl0_calls;
static volatile u64 ptest_ctl1_calls;
static volatile u64 ptest_event_calls;
static volatile u64 ptest_last_arg_len;

static unsigned long ptest_strlen(const char *s)
{
    unsigned long len = 0;

    if (!s)
        return 0;
    while (s[len])
        len++;
    return len;
}

/* long init(const char *args, const char *event, void *reserved) */
static long ptest_init(const char *args, const char *event, void *reserved)
{
    (void)event;
    (void)reserved;

    ptest_init_calls++;
    ptest_last_arg_len = ptest_strlen(args);
    /* A non-zero return would make the loader call exit and drop the image. */
    return 0;
}

/* long exit(void *reserved) */
static long ptest_exit(void *reserved)
{
    (void)reserved;

    ptest_exit_calls++;
    return 0;
}

/*
 * long ctl0(const char *ctl_args, char __user *out_msg, int outlen)
 *
 * Writing into out_msg would need copy_to_user, i.e. an external symbol, which
 * is exactly what this module avoids. It returns the argument length instead:
 * `ksud kpm ctl0 ptest hello` prints nothing but succeeds, and a non-zero
 * result is visible in the manager's "last result".
 */
static long ptest_ctl0(const char *ctl_args, char *out_msg, int outlen)
{
    (void)out_msg;
    (void)outlen;

    ptest_ctl0_calls++;
    return (long)ptest_strlen(ctl_args);
}

/* long ctl1(void *a1, void *a2, void *a3) -- returns the sum, echo-friendly. */
static long ptest_ctl1(void *a1, void *a2, void *a3)
{
    ptest_ctl1_calls++;
    return (long)a1 + (long)a2 + (long)a3;
}

/* long event(const char *event, const char *args, void *reserved) */
static long ptest_event(const char *event, const char *args, void *reserved)
{
    (void)event;
    (void)args;
    (void)reserved;

    ptest_event_calls++;
    return 0;
}

/* `.kpm.info` is a run of NUL-terminated key=value strings. */
static const char ptest_info[] __attribute__((section(".kpm.info"), used, aligned(8))) =
    "name=ptest\0"
    "version=0.1\0"
    "license=GPL\0"
    "author=MRSU\0"
    "description=KPM smoke test: no external symbols\0";

/*
 * One slot per entry: an 8-byte function pointer in a named section. A typed
 * slot keeps the compiler from needing a function-pointer-to-object cast.
 */
typedef long (*ptest_init_t)(const char *, const char *, void *);
typedef long (*ptest_exit_t)(void *);
typedef long (*ptest_ctl0_t)(const char *, char *, int);
typedef long (*ptest_ctl1_t)(void *, void *, void *);
typedef long (*ptest_event_t)(const char *, const char *, void *);

#define KPM_SLOT(sec, type, fn)                                                                    \
    static const type fn##_slot __attribute__((section(sec), used, aligned(8))) = fn

KPM_SLOT(".kpm.init", ptest_init_t, ptest_init);
KPM_SLOT(".kpm.exit", ptest_exit_t, ptest_exit);
KPM_SLOT(".kpm.ctl0", ptest_ctl0_t, ptest_ctl0);
KPM_SLOT(".kpm.ctl1", ptest_ctl1_t, ptest_ctl1);
KPM_SLOT(".kpm.event", ptest_event_t, ptest_event);
