#ifndef __KSU_H_KPM_KFUNC
#define __KSU_H_KPM_KFUNC

#include <linux/types.h>

/*
 * The subset of KernelPatch's kfunc API that this runtime implements.
 *
 * A .kpm imports these by name; kpm_symbol_lookup() maps them onto the
 * functions below, so the signatures must match kpatch's headers exactly.
 */

unsigned long kpm_kf_symbol_lookup_name(const char *name);
unsigned long kpm_kf_kallsyms_lookup_name(const char *name);

/* Returns the number of bytes copied (0 on fault), like kpatch. */
int kpm_kf_compat_copy_to_user(void __user *to, const void *from, int n);
long kpm_kf_compat_strncpy_from_user(char *dest, const char __user *src, long count);

u32 kpm_kf_current_uid(void);

int kpm_kf_symbol_count(void);
int kpm_kf_symbol_list(char *out, int size);
const char *kpm_kf_feature_list(void);

/* Variable exports: kpatch exposes these as data symbols. */
extern u32 kpm_kver;

#endif // __KSU_H_KPM_KFUNC
