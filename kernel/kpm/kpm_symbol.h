#ifndef __KSU_H_KPM_SYMBOL
#define __KSU_H_KPM_SYMBOL

#include <linux/types.h>

/*
 * Resolve a symbol imported by a .kpm image.
 *
 * Order: kfuncs implemented by this runtime (the kpatch API surface), then any
 * kernel symbol by name -- including the ones that are not EXPORT_SYMBOL'd,
 * which kpm modules commonly import.
 */
unsigned long kpm_symbol_lookup(const char *name);

/* Introspection for the kp_symbol_count/kp_symbol_list kfuncs. */
int kpm_symbol_count(void);
int kpm_symbol_list(char *out, unsigned int size);

#endif // __KSU_H_KPM_SYMBOL
