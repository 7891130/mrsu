#ifndef __KSU_H_KPM_RELOC
#define __KSU_H_KPM_RELOC

#include <linux/elf.h>
#include <linux/types.h>

/*
 * Apply every SHT_RELA section of a loaded .kpm image.
 *
 * Section headers must already point at their final addresses (SHF_ALLOC
 * sections inside the image, non-allocated ones inside the source buffer), and
 * undefined symbols must already be resolved to absolute addresses.
 */
int kpm_apply_relocations(Elf64_Shdr *sechdrs, unsigned int shnum, unsigned int symindex);

#endif // __KSU_H_KPM_RELOC
