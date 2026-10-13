/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Force-included into every file of the module: malloc, calloc, realloc and aligned_alloc of the
 * driver and the supplicant take their memory from a pool of the module (src/aic_mem.c),
 * a block of its own that is never mixed with the memory of the applications. The Wi-Fi code
 * allocates and frees small objects all the time; in the shared arena they split it and a
 * video decoder cannot find a big block any more. free() is not a macro here (the supplicant has
 * structures with a member of that name): the linker wraps it, see __wrap_free().
 */
#ifndef AIC_MEM_REDIRECT_H
#define AIC_MEM_REDIRECT_H

/* the declarations come first, the macros below must not touch them */
#include <stdlib.h>
#include <string.h>

void *aic_mem_malloc(size_t size);
void *aic_mem_calloc(size_t n, size_t size);
void *aic_mem_realloc(void *ptr, size_t size);
void *aic_mem_aligned_alloc(size_t align, size_t size);

#define malloc(s)		aic_mem_malloc(s)
#define calloc(n, s)		aic_mem_calloc(n, s)
#define realloc(p, s)		aic_mem_realloc(p, s)
#define aligned_alloc(a, s)	aic_mem_aligned_alloc(a, s)

#endif /* AIC_MEM_REDIRECT_H */
