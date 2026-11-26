#ifndef __KSU_H_KPM_KFUNC_BTF
#define __KSU_H_KPM_KFUNC_BTF

#include <linux/types.h>

/* BTF helpers exposed to .kpm modules; see kpm_kfunc_btf.c. */
int kpm_kf_btf_available(void);
int kpm_kf_btf_find_type(const char *name);
long kpm_kf_btf_type_size(const char *name);
int kpm_kf_btf_type_info(int type_id, const char **name_out, int *kind_out, long *size_out);
int kpm_kf_btf_member(int type_id, int idx, const char **name_out, int *type_id_out, long *bit_offset_out);
long kpm_kf_btf_member_offset(const char *struct_name, const char *member);
int kpm_kf_btf_skip_mod(int type_id);

#endif // __KSU_H_KPM_KFUNC_BTF
