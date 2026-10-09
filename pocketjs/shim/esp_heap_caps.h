/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* One memory: the PSRAM and the SRAM are the same heap here, the flags are accepted
 * and ignored.
 */
#define MALLOC_CAP_SPIRAM   (1U << 0)
#define MALLOC_CAP_INTERNAL (1U << 1)
#define MALLOC_CAP_8BIT     (1U << 2)
#define MALLOC_CAP_DMA      (1U << 3)

static inline void *heap_caps_malloc(size_t size, uint32_t caps)
{
	(void)caps;
	return malloc(size);
}

static inline void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps)
{
	(void)caps;
	return aligned_alloc(alignment, size);
}

static inline void heap_caps_free(void *ptr)
{
	free(ptr);
}
