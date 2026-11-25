#ifndef __KSU_H_KPM
#define __KSU_H_KPM

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/uaccess.h>

/*
 * KPM (KernelPatch module) runtime.
 *
 * A .kpm is an ET_REL ELF image whose entry points are declared through the
 * section names below, mirroring KernelPatch's ABI so that modules built for
 * kpatch can be loaded unchanged.
 */
#define KPM_SEC_INIT ".kpm.init"
#define KPM_SEC_EXIT ".kpm.exit"
#define KPM_SEC_INFO ".kpm.info"
#define KPM_SEC_CTL0 ".kpm.ctl0"
#define KPM_SEC_CTL1 ".kpm.ctl1"
#define KPM_SEC_EVENT ".kpm.event"

#define KPM_NAME_LEN 32
#define KPM_VERSION_LEN 32
#define KPM_LICENSE_LEN 32
#define KPM_AUTHOR_LEN 32
#define KPM_DESCRIPTION_LEN 512
#define KPM_ARGS_MAX 1024

typedef long (*kpm_initcall_t)(const char *args, const char *event, void *reserved);
typedef long (*kpm_exitcall_t)(void *reserved);
typedef long (*kpm_ctl0call_t)(const char *ctl_args, char __user *out_msg, int outlen);
typedef long (*kpm_ctl1call_t)(void *a1, void *a2, void *a3);
typedef long (*kpm_eventcall_t)(const char *event, const char *args, void *reserved);

struct kpm_module {
    struct list_head list;

    char name[KPM_NAME_LEN];
    char version[KPM_VERSION_LEN];
    char license[KPM_LICENSE_LEN];
    char author[KPM_AUTHOR_LEN];
    char description[KPM_DESCRIPTION_LEN];
    char *args;

    /* executable image */
    void *image;
    size_t image_size;
    unsigned int image_pages;

    kpm_initcall_t init;
    kpm_exitcall_t exit;
    kpm_ctl0call_t ctl0;
    kpm_ctl1call_t ctl1;
    kpm_eventcall_t event;
};

/* lifecycle */
void kpm_runtime_init(void);
void kpm_runtime_exit(void);

/* exec memory */
void *kpm_alloc_exec(size_t size);
int kpm_protect_exec(void *addr, size_t size);
void kpm_free_exec(void *addr, size_t size);

/* ELF loader: fills mod from a validated .kpm image in kernel memory */
int kpm_elf_load(struct kpm_module *mod, const void *data, size_t len, const char *args);
void kpm_elf_release(struct kpm_module *mod);

/* module table operations (called from the supercall dispatcher) */
int kpm_load_path(const char *path, const char *args, char *err, size_t err_len);
int kpm_unload(const char *name);
int kpm_list(char *out, size_t size);
int kpm_info(const char *name, char *out, size_t size);
long kpm_ctl0(const char *name, const char *args, char __user *out, int outlen);
long kpm_ctl1(const char *name, void *a1, void *a2, void *a3);

#endif // __KSU_H_KPM
