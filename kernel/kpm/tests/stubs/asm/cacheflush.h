#ifndef __HOST_STUB_ASM_CACHEFLUSH_H
#define __HOST_STUB_ASM_CACHEFLUSH_H

#include <stdint.h>

static inline void caches_clean_inval_pou(uintptr_t start, uintptr_t end)
{
    (void)start;
    (void)end;
}

static inline void __flush_icache_range(uintptr_t start, uintptr_t end)
{
    (void)start;
    (void)end;
}

#endif