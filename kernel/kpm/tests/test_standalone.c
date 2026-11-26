/*
 * Host-side tests for the standalone KPM runtime (kernel/kpm/kpm_standalone.c)
 * and the cross-module control channel it registers with kernelsu.ko.
 *
 * kpm.ko looks ksu_kpm_register_ops()/ksu_kpm_unregister_ops() up by name
 * instead of linking them, so the lookup is stubbed here: with the channel
 * "present" the stub hands out the real functions from kpm_core.c, which makes
 * this an end-to-end test of the handover rather than a mock of it.
 *
 * Note that kpm_table.c is linked once, so the built-in table and kpm.ko's table
 * are the same object in this harness. What is verified is the routing and the
 * handover rules -- which owner serves a command -- not that the two modules
 * have private copies.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <linux/fs.h>

#include "kpm/kpm.h"
#include "kpm/kpm_provider.h"

/* --- the "kernel" the runtime resolves the channel from ------------------ */

static int channel_present = 1;

int ksu_kpm_register_ops(const struct kpm_ops *ops);
void ksu_kpm_unregister_ops(const struct kpm_ops *ops);

unsigned long find_any_symbol_exact(const char *name)
{
    if (!channel_present)
        return 0;
    if (!strcmp(name, "ksu_kpm_register_ops"))
        return (unsigned long)(uintptr_t)&ksu_kpm_register_ops;
    if (!strcmp(name, "ksu_kpm_unregister_ops"))
        return (unsigned long)(uintptr_t)&ksu_kpm_unregister_ops;
    return 0;
}

/* --- stubs for the rest of the table ------------------------------------- */

static int exit_calls;
static int init_rc;

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

int kpm_elf_load(struct kpm_module *mod, const void *data, size_t len, const char *args,
                 kpm_name_exists_t name_exists, char *err, size_t err_len)
{
    (void)data;
    (void)len;
    (void)err;
    (void)err_len;

    snprintf(mod->name, KPM_NAME_LEN, "mod1");
    snprintf(mod->version, KPM_VERSION_LEN, "1.0");
    snprintf(mod->license, KPM_LICENSE_LEN, "GPL");

    if (name_exists && name_exists(mod->name))
        return -EEXIST;

    mod->init = stub_init;
    mod->exit = stub_exit;
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

/* --- entry points from kpm_standalone.c (non-static on purpose) ---------- */

int kpm_standalone_init(void);
void kpm_standalone_exit(void);

/* --- harness ------------------------------------------------------------- */

static int failures;

static void reset_exits(void)
{
    exit_calls = 0;
}

static void check(int cond, const char *what)
{
    if (cond) {
        printf("  ok   %s\n", what);
    } else {
        printf("  FAIL %s\n", what);
        failures++;
    }
}

/*
 * The ops struct is the ABI between two independently built modules: a field
 * inserted or reordered on one side would make kernelsu.ko call a wrong function
 * pointer at a plausible-looking offset. Pin the layout so a careless edit fails
 * here instead of on a device.
 */
_Static_assert(sizeof(struct kpm_ops) == 8 + 8 * sizeof(void *),
               "kpm_ops grew a field: bump KPM_OPS_VERSION and re-check both modules");
_Static_assert(offsetof(struct kpm_ops, load) == 8 + 1 * sizeof(void *), "load moved");
_Static_assert(offsetof(struct kpm_ops, ctl0) == 8 + 5 * sizeof(void *), "ctl0 moved");
_Static_assert(offsetof(struct kpm_ops, notify_event) == 8 + 7 * sizeof(void *), "notify_event moved");

#define MODULE_PATH "/data/adb/kpm/test.kpm"

int main(void)
{
    char err[128];
    char text[256];

    setvbuf(stdout, NULL, _IONBF, 0);

    host_fs_set_blob("PK\x03\x04fake kpm", 12);
    kpm_runtime_init();

    printf("no control channel in the kernel\n");
    channel_present = 0;
    check(kpm_standalone_init() == -ENOENT, "refuses to load without a control channel");
    check(kpm_load_path(MODULE_PATH, NULL, err, sizeof(err)) == 0,
          "the built-in path still works on its own");
    check(kpm_unload("mod1") == 0, "and can unload again");

    printf("the built-in table already owns modules\n");
    channel_present = 1;
    check(kpm_load_path(MODULE_PATH, NULL, err, sizeof(err)) == 0, "load into the built-in table");
    check(kpm_standalone_init() == -EBUSY, "kpm.ko refuses to shadow loaded modules");
    check(kpm_list(text, sizeof(text)) > 0 && strstr(text, "mod1") != NULL,
          "the built-in module is still visible");
    check(kpm_unload("mod1") == 0, "unload it again");

    printf("taking the channel over\n");
    check(kpm_standalone_init() == 0, "kpm.ko takes the control channel");
    check(kpm_standalone_init() == -EBUSY, "and cannot take it twice");

    printf("routing through the registered runtime\n");
    reset_exits();
    check(kpm_load_path(MODULE_PATH, "arg=1", err, sizeof(err)) == 0, "load reaches the runtime");
    check(kpm_list(text, sizeof(text)) > 0, "list reaches the runtime");
    check(strstr(text, "mod1") != NULL, "the module is listed");
    check(kpm_info("mod1", text, sizeof(text)) > 0, "info reaches the runtime");
    check(kpm_notify_event("boot-completed", NULL) >= 0, "boot events reach the runtime");
    check(kpm_unload("mod1") == 0, "unload reaches the runtime");
    check(exit_calls == 1, "the module's exit was called");

    printf("going away\n");
    check(kpm_load_path(MODULE_PATH, NULL, err, sizeof(err)) == 0, "load one more module");
    reset_exits();
    kpm_standalone_exit();
    check(exit_calls == 1, "unloading kpm.ko unloads its modules");
    check(kpm_list(text, sizeof(text)) == 0, "the table is empty afterwards");
    check(kpm_load_path(MODULE_PATH, NULL, err, sizeof(err)) == 0,
          "commands fall back to the built-in table");
    check(kpm_unload("mod1") == 0, "cleanup");

    printf("feature gate\n");
    check(registered_handler != NULL, "the feature handler is registered");
    check(registered_handler->set_handler(0) == 0, "the feature can be disabled");
    check(kpm_load_path(MODULE_PATH, NULL, err, sizeof(err)) == -EPERM, "load is refused");
    check(strstr(err, "disabled") != NULL, "and says why");
    check(registered_handler->set_handler(1) == 0, "the feature can be re-enabled");
    check(kpm_load_path(MODULE_PATH, NULL, err, sizeof(err)) == 0, "load works again");
    check(kpm_unload("mod1") == 0, "cleanup");

    kpm_runtime_exit();
    check(registered_handler == NULL, "feature handler unregistered");

    if (failures) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
