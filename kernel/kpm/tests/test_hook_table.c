/*
 * Host-side tests for the hook registry (kernel/kpm/kpm_hook_table.c).
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kpm/kpm_hook_table.h"

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

static struct kpm_hook_entry a = { .target = 0x1000, .replacement = 0x2000, .trampoline_pc = 0x3000, .owner = "modA" };
static struct kpm_hook_entry b = { .target = 0x1100, .replacement = 0x2100, .trampoline_pc = 0x3100, .owner = "modB" };

int main(void)
{
    char text[256];

    setvbuf(stdout, NULL, _IONBF, 0);

    printf("registry\n");
    kpm_hook_table_init();
    check(kpm_hook_table_count() == 0, "starts empty");
    check(kpm_hook_table_add(&a) == 0, "adds an entry");
    check(kpm_hook_table_count() == 1, "count follows");
    check(kpm_hook_table_find(0x1000) == &a, "finds by target");
    check(kpm_hook_table_find(0x9999) == NULL, "unknown target is absent");

    printf("duplicates\n");
    {
        struct kpm_hook_entry dup = { .target = 0x1000, .replacement = 0x5555, .owner = "dup" };

        check(kpm_hook_table_add(&dup) == -EEXIST, "same target rejected");
        check(kpm_hook_table_count() == 1, "count unchanged");
        check(kpm_hook_table_find(0x1000) == &a, "original still registered");
    }

    printf("several entries\n");
    check(kpm_hook_table_add(&b) == 0, "adds a second entry");
    check(kpm_hook_table_count() == 2, "count is two");
    check(kpm_hook_table_list(text, sizeof(text)) > 0, "list produces text");
    check(strstr(text, "0x1000 -> 0x2000 modA") != NULL, "list shows the first hook");
    check(strstr(text, "0x1100 -> 0x2100 modB") != NULL, "list shows the second hook");

    printf("listing bounds\n");
    {
        char small[16];
        int n = kpm_hook_table_list(small, sizeof(small));

        check(n > 0 && (size_t)n < sizeof(small), "truncates instead of overflowing");
        check(small[sizeof(small) - 1] == '\0' || strlen(small) < sizeof(small), "stays terminated");
    }

    printf("removal\n");
    check(kpm_hook_table_remove(0x1000) == &a, "removes by target");
    check(kpm_hook_table_find(0x1000) == NULL, "removed entry is gone");
    check(kpm_hook_table_count() == 1, "count drops");
    check(kpm_hook_table_remove(0x1000) == NULL, "removing twice is harmless");
    check(kpm_hook_table_add(&a) == 0, "the same target can be hooked again");

    printf("exit clears\n");
    kpm_hook_table_exit();
    check(kpm_hook_table_count() == 0, "table is empty after exit");

    if (failures) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
