/*
 * Host-side tests for the KPM module table (kernel/kpm/kpm_core.c).
 *
 * The ELF loader and the feature registry are stubbed, so what is exercised is
 * the lifecycle logic: load/unload, duplicate rejection, list/info formatting,
 * ctl dispatch, event delivery and teardown.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <linux/fs.h>

#include "kpm/kpm.h"
#include "kpm/kpm_provider.h"
#include "policy/feature.h"

/* --- stubs --------------------------------------------------------------- */

static const char *next_module_name = "mod1";
static int loader_failure;
static int init_rc;
static int exit_calls;
static int event_calls;
static int ctl0_calls;
static int ctl1_calls;
static char last_ctl0_args[64];
static char last_event[64];
static char last_event_args[64];

static long stub_init(const char *args, const char *event, void *reserved)
{
    (void)args;
    (void)event;
    (void)reserved;
    return init_rc;
}

static long stub_exit(void *reserved)
{
    (void)reserved;
    exit_calls++;
    return 0;
}

static long stub_ctl0(const char *ctl_args, char __user *out_msg, int outlen)
{
    ctl0_calls++;
    snprintf(last_ctl0_args, sizeof(last_ctl0_args), "%s", ctl_args ? ctl_args : "");
    if (out_msg && outlen > 0)
        snprintf((char *)out_msg, (size_t)outlen, "ctl0:%s", ctl_args ? ctl_args : "");
    return 0;
}

static long stub_ctl1(void *a1, void *a2, void *a3)
{
    ctl1_calls++;
    return (long)(uintptr_t)a1 + (long)(uintptr_t)a2 + (long)(uintptr_t)a3;
}

static long stub_event(const char *event, const char *args, void *reserved)
{
    (void)reserved;
    event_calls++;
    snprintf(last_event, sizeof(last_event), "%s", event ? event : "");
    snprintf(last_event_args, sizeof(last_event_args), "%s", args ? args : "");
    return 0;
}

int kpm_elf_load(struct kpm_module *mod, const void *data, size_t len, const char *args, kpm_name_exists_t name_exists,
                 char *err, size_t err_len)
{
    (void)data;
    (void)len;

    if (loader_failure) {
        if (err && err_len)
            snprintf(err, err_len, "stub loader failure");
        return -ENOEXEC;
    }

    snprintf(mod->name, KPM_NAME_LEN, "%s", next_module_name);
    snprintf(mod->version, KPM_VERSION_LEN, "1.0");
    snprintf(mod->license, KPM_LICENSE_LEN, "GPL");

    if (name_exists && name_exists(mod->name)) {
        if (err && err_len)
            snprintf(err, err_len, "module already loaded");
        return -EEXIST;
    }

    mod->init = stub_init;
    mod->exit = stub_exit;
    mod->ctl0 = stub_ctl0;
    mod->ctl1 = stub_ctl1;
    mod->event = stub_event;
    mod->image = (void *)(uintptr_t)0x1000;
    if (args && args[0])
        mod->args = strdup(args);
    return 0;
}

void kpm_elf_release(struct kpm_module *mod)
{
    mod->image = NULL;
}

static const struct ksu_feature_handler *registered_handler;

int ksu_register_feature_handler(const struct ksu_feature_handler *handler)
{
    registered_handler = handler;
    return 0;
}

int ksu_unregister_feature_handler(u32 feature_id)
{
    (void)feature_id;
    registered_handler = NULL;
    return 0;
}

/* --- the registered owner (kpm.ko) --------------------------------------- */

static int prov_load, prov_unload, prov_list, prov_info, prov_ctl0, prov_ctl1, prov_event;

static int prov_load_fn(const char *path, const char *args, char *err, size_t err_len)
{
    (void)path;
    (void)args;
    prov_load++;
    if (err && err_len)
        snprintf(err, err_len, "served by kpm.ko");
    return -4242;
}

static int prov_unload_fn(const char *name)
{
    (void)name;
    prov_unload++;
    return -4243;
}

static int prov_list_fn(char *out, size_t size)
{
    prov_list++;
    snprintf(out, size, "from-provider\n");
    return 14;
}

static int prov_info_fn(const char *name, char *out, size_t size)
{
    (void)name;
    prov_info++;
    snprintf(out, size, "info-from-provider\n");
    return 19;
}

static long prov_ctl0_fn(const char *name, const char *args, char __user *out, int outlen)
{
    (void)name;
    (void)args;
    (void)out;
    (void)outlen;
    prov_ctl0++;
    return -4244;
}

static long prov_ctl1_fn(const char *name, void *a1, void *a2, void *a3)
{
    (void)name;
    (void)a1;
    (void)a2;
    (void)a3;
    prov_ctl1++;
    return -4245;
}

static int prov_event_fn(const char *event, const char *args)
{
    (void)event;
    (void)args;
    prov_event++;
    return -4246;
}

static const struct kpm_ops provider_ops = {
    .magic = KPM_OPS_MAGIC,
    .version = KPM_OPS_VERSION,
    .name = "kpm.ko",
    .load = prov_load_fn,
    .unload = prov_unload_fn,
    .list = prov_list_fn,
    .info = prov_info_fn,
    .ctl0 = prov_ctl0_fn,
    .ctl1 = prov_ctl1_fn,
    .notify_event = prov_event_fn,
};

static const struct kpm_ops provider_without_load = {
    .magic = KPM_OPS_MAGIC,
    .version = KPM_OPS_VERSION,
    .name = "broken.ko",
    .unload = prov_unload_fn,
    .list = prov_list_fn,
    .info = prov_info_fn,
};

static struct kpm_ops provider_bad_magic = {
    .magic = 0xdeadbeef,
    .version = KPM_OPS_VERSION,
    .name = "bad.ko",
    .load = prov_load_fn,
    .unload = prov_unload_fn,
    .list = prov_list_fn,
    .info = prov_info_fn,
};

static struct kpm_ops provider_bad_version = {
    .magic = KPM_OPS_MAGIC,
    .version = KPM_OPS_VERSION + 1,
    .name = "future.ko",
    .load = prov_load_fn,
    .unload = prov_unload_fn,
    .list = prov_list_fn,
    .info = prov_info_fn,
};

/* --- test helpers -------------------------------------------------------- */

static int failures;

static void check(int cond, const char *what)
{
    if (cond) {
        printf("  ok   %s\n", what);
    } else {
        printf("  FAIL %s\n", what);
        failures++;
    }
}

static void reset_state(void)
{
    next_module_name = "mod1";
    loader_failure = 0;
    init_rc = 0;
    exit_calls = event_calls = ctl0_calls = ctl1_calls = 0;
    last_ctl0_args[0] = last_event[0] = last_event_args[0] = '\0';
}

#define MODULE_PATH "/data/adb/kpm/test.kpm"

static int load(const char *name, const char *args, char *err, size_t err_len)
{
    next_module_name = name;
    return kpm_load_path(MODULE_PATH, args, err, err_len);
}

int main(void)
{
    char err[128];
    char text[512];

    setvbuf(stdout, NULL, _IONBF, 0);

    host_fs_set_blob("PK\x03\x04"
                     "fake kpm",
                     12);
    kpm_runtime_init();

    printf("loading\n");
    reset_state();
    check(load("mod1", "arg=1", err, sizeof(err)) == 0, "loads a module");
    check(strstr(err, "") != NULL, "err buffer usable");
    check(kpm_list(text, sizeof(text)) > 0, "list reports the module");
    check(strstr(text, "mod1") != NULL && strstr(text, "1.0") != NULL, "list shows name and version");
    check(strstr(text, "arg=1") != NULL, "list shows module args");

    printf("duplicates\n");
    check(load("mod1", NULL, err, sizeof(err)) == -EEXIST, "same name rejected");
    check(strstr(err, "already loaded") != NULL, "duplicate reports a message");

    printf("second module\n");
    check(load("mod2", NULL, err, sizeof(err)) == 0, "loads a second module");
    kpm_list(text, sizeof(text));
    check(strstr(text, "mod1") != NULL && strstr(text, "mod2") != NULL, "both listed");

    printf("info\n");
    check(kpm_info("mod1", text, sizeof(text)) > 0, "info returns text");
    check(strstr(text, "name=mod1") != NULL, "info has the name");
    check(strstr(text, "has_ctl0=1") != NULL, "info reports ctl0");
    check(strstr(text, "has_event=1") != NULL, "info reports the event handler");
    check(kpm_info("nope", text, sizeof(text)) == -ENOENT, "unknown module reported");

    printf("control commands\n");
    memset(text, 0, sizeof(text));
    check(kpm_ctl0("mod1", "hello", (char __user *)text, (int)sizeof(text)) == 0, "ctl0 dispatched");
    check(strcmp(last_ctl0_args, "hello") == 0, "ctl0 saw its arguments");
    check(strcmp(text, "ctl0:hello") == 0, "ctl0 reply reached the buffer");
    check(kpm_ctl1("mod1", (void *)1, (void *)2, (void *)3) == 6, "ctl1 returned the module's value");
    check(kpm_ctl0("nope", "x", (char __user *)text, (int)sizeof(text)) == -ENOENT, "ctl0 on unknown module");

    printf("events\n");
    check(kpm_notify_event("boot-completed", "x") == 2, "delivers to both modules");
    check(event_calls == 2, "each module called once");
    check(strcmp(last_event, "boot-completed") == 0, "event name delivered");
    check(strcmp(last_event_args, "x") == 0, "event args delivered");
    check(kpm_notify_event("", NULL) == -EINVAL, "empty event rejected");

    printf("unloading\n");
    check(kpm_unload("mod1") == 0, "unloads");
    check(exit_calls == 1, "module exit was called");
    kpm_list(text, sizeof(text));
    check(strstr(text, "mod1") == NULL, "unloaded module is gone");
    check(kpm_unload("mod1") == -ENOENT, "unloading twice is reported");

    printf("failure paths\n");
    reset_state();
    loader_failure = -ENOEXEC;
    check(load("bad", NULL, err, sizeof(err)) == -ENOEXEC, "loader failure propagated");
    check(strstr(err, "stub loader failure") != NULL, "loader message surfaced");
    kpm_list(text, sizeof(text));
    check(strstr(text, "bad") == NULL, "failed module is not registered");

    printf("control channel owner\n");
    check(ksu_kpm_register_ops(NULL) == -EINVAL, "a null ops struct is refused");
    check(ksu_kpm_register_ops(&provider_without_load) == -EINVAL, "ops without load is refused");
    check(ksu_kpm_register_ops(&provider_bad_magic) == -EINVAL, "a foreign magic is refused");
    check(ksu_kpm_register_ops(&provider_bad_version) == -EINVAL, "a future ABI version is refused");
    check(ksu_kpm_register_ops(&provider_ops) == -EBUSY, "cannot take over while the built-in table has modules");

    check(kpm_unload("mod2") == 0, "unload the remaining built-in module");
    check(ksu_kpm_register_ops(&provider_ops) == 0, "kpm.ko can take the channel over");
    check(ksu_kpm_register_ops(&provider_ops) == -EBUSY, "a second owner is refused");

    check(kpm_load_path(MODULE_PATH, NULL, err, sizeof(err)) == -4242, "load is routed to the owner");
    check(strcmp(err, "served by kpm.ko") == 0, "the owner's diagnostics come through");
    check(kpm_unload("x") == -4243, "unload is routed to the owner");
    check(kpm_list(text, sizeof(text)) == 14 && strstr(text, "from-provider") != NULL, "list is routed to the owner");
    check(kpm_info("x", text, sizeof(text)) == 19, "info is routed to the owner");
    memset(text, 0, sizeof(text));
    check(kpm_ctl0("x", "y", (char __user *)text, (int)sizeof(text)) == -4244, "ctl0 is routed");
    check(kpm_ctl1("x", NULL, NULL, NULL) == -4245, "ctl1 is routed");
    check(kpm_notify_event("boot-completed", NULL) == -4246, "events are routed to the owner");
    check(prov_load == 1 && prov_unload == 1 && prov_list == 1 && prov_info == 1 && prov_ctl0 == 1 && prov_ctl1 == 1 &&
              prov_event == 1,
          "each op was called exactly once");

    printf("the feature gate covers the owner\n");
    check(registered_handler->set_handler(0) == 0, "disable the feature");
    check(kpm_load_path(MODULE_PATH, NULL, err, sizeof(err)) == -EPERM, "load is refused before routing");
    check(strstr(err, "disabled") != NULL, "and says why");
    check(prov_load == 1, "the owner never saw the command");
    check(registered_handler->set_handler(1) == 0, "re-enable the feature");

    printf("releasing the channel\n");
    ksu_kpm_unregister_ops(&provider_bad_magic);
    check(kpm_list(text, sizeof(text)) == 14, "an unrelated unregister is ignored");
    ksu_kpm_unregister_ops(&provider_ops);
    check(kpm_list(text, sizeof(text)) == 0, "the built-in table serves commands again");

    printf("teardown\n");
    kpm_runtime_exit();
    check(registered_handler == NULL, "feature handler unregistered");

    if (failures) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
