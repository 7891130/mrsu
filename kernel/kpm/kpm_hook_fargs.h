#ifndef __KSU_H_KPM_HOOK_FARGS
#define __KSU_H_KPM_HOOK_FARGS

#include <linux/types.h>

/*
 * Argument/return frame handed to a .kpm hook's before/after callbacks.
 *
 * The layout must match KernelPatch's hook.h exactly: modules are compiled
 * against those headers and read these fields by offset, so a mismatch is
 * undefined behaviour rather than a compile error. The offsets are asserted in
 * _vendor/kpm-hosttest/test_hook_fargs.c.
 *
 *   struct { void *chain; int skip_origin; hook_local_t local; uint64_t ret;
 *            union { struct {...}; uint64_t args[N]; }; }
 */

#define KPM_HOOK_LOCAL_DATA_NUM 8
#define KPM_HOOK_CHAIN_NUM 0x10

typedef union {
    struct {
        u64 data0;
        u64 data1;
        u64 data2;
        u64 data3;
        u64 data4;
        u64 data5;
        u64 data6;
        u64 data7;
    };
    u64 data[KPM_HOOK_LOCAL_DATA_NUM];
} kpm_hook_local_t;

/* Common prefix of every frame; args follow it. */
struct kpm_hook_fargs {
    void *chain;
    int skip_origin;
    u64 local[KPM_HOOK_LOCAL_DATA_NUM];
    u64 ret;
    u64 args[];
};

/* Convenience views for the arities kpatch defines. */
struct kpm_hook_fargs0 {
    void *chain;
    int skip_origin;
    kpm_hook_local_t local;
    u64 ret;
};

struct kpm_hook_fargs4 {
    void *chain;
    int skip_origin;
    kpm_hook_local_t local;
    u64 ret;
    union {
        struct {
            u64 arg0;
            u64 arg1;
            u64 arg2;
            u64 arg3;
        };
        u64 args[4];
    };
};

typedef void (*kpm_hook_before_t)(void *fargs, void *udata);
typedef void (*kpm_hook_after_t)(void *fargs, void *udata);

#endif // __KSU_H_KPM_HOOK_FARGS
