#ifndef __KSU_H_KPM_SYMBOL
#define __KSU_H_KPM_SYMBOL

/*
 * Resolve a symbol imported by a .kpm image.
 *
 * Order: kfuncs implemented by this runtime (the kpatch API surface, added in
 * later milestones), then any kernel symbol by name -- including the ones that
 * are not EXPORT_SYMBOL'd, which kpm modules commonly import.
 */
unsigned long kpm_symbol_lookup(const char *name);

#endif // __KSU_H_KPM_SYMBOL
