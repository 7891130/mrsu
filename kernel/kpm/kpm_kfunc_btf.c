// SPDX-License-Identifier: GPL-2.0
/*
 * BTF helpers exposed to .kpm modules.
 *
 * Modules use these to discover struct layouts at load time instead of being
 * rebuilt for every KMI. The kernel already maintains the type graph, so these
 * are thin wrappers around btf_find_by_name_kind()/btf_type_by_id() -- the same
 * helpers infra/btf_parser.c uses -- rather than a second parser.
 *
 * KernelPatch defines one more entry, kp_btf_raw(), which hands out the raw BTF
 * blob. struct btf does not expose its data pointer to modules, so that one is
 * deliberately absent: modules referencing it fail to load with an unknown
 * symbol, which is safer than returning a pointer we cannot guarantee.
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "klog.h" // IWYU pragma: keep

#if defined(MODULE) && defined(CONFIG_DEBUG_INFO_BTF)

#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/err.h>
#include <linux/string.h>

static const struct btf *kpm_btf(void)
{
    const struct btf *btf = bpf_get_btf_vmlinux();

    if (IS_ERR_OR_NULL(btf))
        return NULL;
    return btf;
}

int kpm_kf_btf_available(void)
{
    return kpm_btf() ? 1 : 0;
}

/* Find a struct or union by name; returns the BTF type id, or a negative error. */
int kpm_kf_btf_find_type(const char *name)
{
    const struct btf *btf = kpm_btf();
    int id;

    if (!btf || !name)
        return -EINVAL;

    id = btf_find_by_name_kind(btf, name, BTF_KIND_STRUCT);
    if (id < 0)
        id = btf_find_by_name_kind(btf, name, BTF_KIND_UNION);
    return id;
}

long kpm_kf_btf_type_size(const char *name)
{
    const struct btf *btf = kpm_btf();
    const struct btf_type *type;
    int id;

    if (!btf || !name)
        return -EINVAL;

    id = kpm_kf_btf_find_type(name);
    if (id < 0)
        return id;

    type = btf_type_by_id(btf, id);
    if (!type)
        return -EINVAL;
    return (long)type->size;
}

int kpm_kf_btf_type_info(int type_id, const char **name_out, int *kind_out, long *size_out)
{
    const struct btf *btf = kpm_btf();
    const struct btf_type *type;
    const char *name;

    if (!btf || type_id <= 0)
        return -EINVAL;

    type = btf_type_by_id(btf, type_id);
    if (!type)
        return -EINVAL;

    name = btf_name_by_offset(btf, type->name_off);
    if (name_out)
        *name_out = name;
    if (kind_out)
        *kind_out = (int)BTF_INFO_KIND(type->info);
    if (size_out)
        *size_out = (long)type->size;
    return 0;
}

int kpm_kf_btf_member(int type_id, int idx, const char **name_out, int *type_id_out, long *bit_offset_out)
{
    const struct btf *btf = kpm_btf();
    const struct btf_type *type;
    const struct btf_member *m;

    if (!btf || type_id <= 0 || idx < 0)
        return -EINVAL;

    type = btf_type_by_id(btf, type_id);
    if (!type)
        return -EINVAL;
    if (BTF_INFO_KIND(type->info) != BTF_KIND_STRUCT && BTF_INFO_KIND(type->info) != BTF_KIND_UNION)
        return -EINVAL;
    if ((u32)idx >= BTF_INFO_VLEN(type->info))
        return -ENOENT;

    /* btf_members() is not available on every target, so index by hand. */
    m = (const struct btf_member *)(type + 1) + idx;
    if (name_out)
        *name_out = btf_name_by_offset(btf, m->name_off);
    if (type_id_out)
        *type_id_out = (int)m->type;

    if (bit_offset_out) {
        u32 bits = m->offset;

        if (BTF_INFO_KFLAG(type->info))
            bits = BTF_MEMBER_BIT_OFFSET(bits);
        *bit_offset_out = (long)bits;
    }
    return 0;
}

long kpm_kf_btf_member_offset(const char *struct_name, const char *member)
{
    const struct btf *btf = kpm_btf();
    const struct btf_type *type;
    const struct btf_member *m;
    u32 i;
    int id;

    if (!btf || !struct_name || !member)
        return -EINVAL;

    id = kpm_kf_btf_find_type(struct_name);
    if (id < 0)
        return id;

    type = btf_type_by_id(btf, id);
    if (!type)
        return -EINVAL;

    for_each_member (i, type, m) {
        const char *name = btf_name_by_offset(btf, m->name_off);
        u32 bits = m->offset;

        if (!name)
            return -EINVAL;
        if (strcmp(name, member))
            continue;
        if (BTF_INFO_KFLAG(type->info))
            bits = BTF_MEMBER_BIT_OFFSET(bits);
        if (bits & 7)
            return -EINVAL; /* a bitfield has no byte offset */
        return (long)(bits / 8);
    }
    return -ENOENT;
}

/* Follow typedef/const/volatile/restrict chains to the underlying type id. */
int kpm_kf_btf_skip_mod(int type_id)
{
    const struct btf *btf = kpm_btf();
    int guard;

    if (!btf || type_id <= 0)
        return -EINVAL;

    for (guard = 0; guard < 32; guard++) {
        const struct btf_type *type = btf_type_by_id(btf, type_id);
        u32 kind;

        if (!type)
            return -EINVAL;
        kind = BTF_INFO_KIND(type->info);
        if (kind != BTF_KIND_TYPEDEF && kind != BTF_KIND_CONST && kind != BTF_KIND_VOLATILE &&
            kind != BTF_KIND_RESTRICT)
            return type_id;
        type_id = (int)type->type;
    }
    return -ELOOP;
}

#else /* !CONFIG_DEBUG_INFO_BTF */

int kpm_kf_btf_available(void)
{
    return 0;
}

int kpm_kf_btf_find_type(const char *name)
{
    (void)name;
    return -EOPNOTSUPP;
}

long kpm_kf_btf_type_size(const char *name)
{
    (void)name;
    return -EOPNOTSUPP;
}

int kpm_kf_btf_type_info(int type_id, const char **name_out, int *kind_out, long *size_out)
{
    (void)type_id;
    (void)name_out;
    (void)kind_out;
    (void)size_out;
    return -EOPNOTSUPP;
}

int kpm_kf_btf_member(int type_id, int idx, const char **name_out, int *type_id_out, long *bit_offset_out)
{
    (void)type_id;
    (void)idx;
    (void)name_out;
    (void)type_id_out;
    (void)bit_offset_out;
    return -EOPNOTSUPP;
}

long kpm_kf_btf_member_offset(const char *struct_name, const char *member)
{
    (void)struct_name;
    (void)member;
    return -EOPNOTSUPP;
}

int kpm_kf_btf_skip_mod(int type_id)
{
    (void)type_id;
    return -EOPNOTSUPP;
}

#endif /* CONFIG_DEBUG_INFO_BTF */
