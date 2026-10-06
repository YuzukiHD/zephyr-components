/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief A heap in demand paged memory for pages that do not fit the RAM
 *
 * With CONFIG_LITEHTML_PAGED_HEAP the memory of the page (the text, the tree, the layout) can
 * live in a region that is bigger than the RAM: the pages of the region that are not used go to
 * a swap file on the SD card (CONFIG_BACKING_STORE_SWAP), the ones that were only read since they
 * came back are dropped without a write. While the heap is enabled malloc(), calloc(), realloc()
 * and aligned_alloc() of the calling thread are served from the region, the other threads and
 * free() of memory from elsewhere keep working on the normal heap.
 *
 * The allocator is a pointer that goes up with lists of freed blocks by size, blocks are not
 * merged. Buffers for drivers (DMA, the frame buffers) must be allocated while the heap is disabled.
 */

#ifndef LITEHTML_ZEPHYR_LH_PAGED_H_
#define LITEHTML_ZEPHYR_LH_PAGED_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lh_paged_stats {
	size_t size;       /* bytes of the region */
	size_t allocated;  /* bytes handed out now */
	size_t max_allocated;
	size_t high_water; /* the highest address that was handed out, from the start */
	size_t free;       /* bytes that were never handed out */
};

/**
 * Map the region and create the heap in it. The file system must be mounted.
 *
 * @param swap_path File that holds the pages that are not in RAM
 * @param size Size of the region in bytes
 */
int lh_paged_heap_init(const char *swap_path, size_t size);

/** Serve the allocations of the calling thread from the region (true) or the normal heap */
void lh_paged_heap_enable(bool on);

/**
 * While on, free() of a block of the region does nothing but count: the page is about to be
 * thrown away with lh_paged_heap_reset(), and linking every block into a free list would read
 * the whole tree back from the swap file.
 */
void lh_paged_heap_discard_frees(bool on);

/** Forget everything in the heap: all blocks are gone, the next allocation starts at the bottom */
void lh_paged_heap_reset(void);

void lh_paged_heap_stats(struct lh_paged_stats *stats);

#ifdef __cplusplus
}
#endif

#endif /* LITEHTML_ZEPHYR_LH_PAGED_H_ */
