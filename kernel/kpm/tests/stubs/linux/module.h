#ifndef __HOST_STUB_LINUX_MODULE_H
#define __HOST_STUB_LINUX_MODULE_H

/*
 * Enough of <linux/module.h> to compile a module entry file on the host.
 *
 * The kernel turns module_init()/module_exit() into initcalls; the host harness
 * calls the entry points itself, so they expand to nothing here. That is why
 * kpm_standalone.c keeps its entry points non-static.
 */
#include <linux/init.h>
#include <linux/types.h>

struct module;

#define THIS_MODULE ((struct module *)0)

#define MODULE_LICENSE(x) static const char __host_module_license[] __attribute__((unused)) = x
#define MODULE_DESCRIPTION(x) static const char __host_module_description[] __attribute__((unused)) = x
#define MODULE_AUTHOR(x) static const char __host_module_author[] __attribute__((unused)) = x

#define module_init(fn)
#define module_exit(fn)

#endif
