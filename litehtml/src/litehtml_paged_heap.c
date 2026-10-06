/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The heap of the paged region. A page of the region that is not in RAM costs a read of the
 * swap file, so the allocator keeps what it does simple and local: blocks come from a pointer
 * that only goes up (so the region fills page by page and the swap file grows the same way),
 * freed blocks go to lists by size and are reused, and nothing is merged. A block that is
 * freed is not touched when the heap is told to discard frees (the whole page is dropped
 * with lh_paged_heap_reset() afterwards): writing the list link into every block of a big
 * tree would read the whole tree back from the card first.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel/mm/backing_store_swap.h>
#include <litehtml_zephyr/lh_paged.h>

#define HDR		8U	/* the size of the block, 8 bytes keep the payload aligned */
#define SMALL_STEP	16U
#define SMALL_MAX	1024U
#define N_SMALL		(SMALL_MAX / SMALL_STEP)
#define MED_STEP	256U
#define MED_MAX		65536U
#define N_MED		(MED_MAX / MED_STEP)

struct free_blk {
	struct free_blk *next;
};

static uint8_t *region;
static size_t region_size;
static uint8_t *top;
static size_t live, peak, high_water;
static struct free_blk *small[N_SMALL + 1];
static struct free_blk *med[N_MED + 1];
static struct free_blk *big;
static bool discard_frees;
static k_tid_t owner;
static bool enabled;

void *__real_malloc(size_t n);
void *__real_calloc(size_t a, size_t b);
void *__real_realloc(void *p, size_t n);
void *__real_aligned_alloc(size_t align, size_t n);
void __real_free(void *p);

static bool owns(const void *p)
{
	return region != NULL && (const uint8_t *)p >= region &&
	       (const uint8_t *)p < region + region_size;
}

static uint32_t blk_size(const void *p)
{
	return *(const uint32_t *)((const uint8_t *)p - HDR);
}

/* the size a request is rounded to, which is also its list */
static size_t round_size(size_t n)
{
	if (n == 0) {
		n = 1;
	}
	if (n <= SMALL_MAX) {
		return ROUND_UP(n, SMALL_STEP);
	}
	if (n <= MED_MAX) {
		return ROUND_UP(n, MED_STEP);
	}
	return ROUND_UP(n, 4096U);
}

static struct free_blk **list_for(size_t size)
{
	if (size <= SMALL_MAX) {
		return &small[size / SMALL_STEP];
	}
	if (size <= MED_MAX) {
		return &med[size / MED_STEP];
	}
	return &big;
}

static void *paged_alloc(size_t n, size_t align)
{
	size_t size = round_size(n);
	struct free_blk **l = list_for(size);
	uint8_t *p;

	if (align <= 8U) {
		if (l != &big) {
			if (*l != NULL) {
				struct free_blk *b = *l;

				*l = b->next;
				live += size;
				peak = MAX(peak, live);
				return b;
			}
		} else {
			/* big blocks: the first one that is big enough, not split */
			for (struct free_blk **q = &big; *q != NULL; q = &(*q)->next) {
				if (blk_size(*q) >= size) {
					struct free_blk *b = *q;

					*q = b->next;
					live += blk_size(b);
					peak = MAX(peak, live);
					return b;
				}
			}
		}
	}

	/* the payload is aligned, its header is right before it */
	p = (uint8_t *)ROUND_UP((uintptr_t)top + HDR, MAX(align, 8U));
	if (p + size > region + region_size) {
		return NULL;
	}
	*(uint32_t *)(p - HDR) = (uint32_t)size;
	top = p + size;
	high_water = MAX(high_water, (size_t)(top - region));
	live += size;
	peak = MAX(peak, live);
	return p;
}

static void paged_free(void *p)
{
	uint32_t size = blk_size(p);
	struct free_blk **l;

	live -= size;
	if (discard_frees) {
		return;
	}
	l = list_for(size);
	((struct free_blk *)p)->next = *l;
	*l = p;
}

/* the allocations of the thread that enabled the heap */
static bool use_paged(void)
{
	return enabled && owner == k_current_get();
}

int lh_paged_heap_init(const char *swap_path, size_t size)
{
	if (region != NULL) {
		return -EALREADY;
	}
	region = k_mem_paging_swap_map(swap_path, size);
	if (region == NULL) {
		return -ENOMEM;
	}
	region_size = size;
	top = region;

	return 0;
}

void lh_paged_heap_enable(bool on)
{
	enabled = on && region != NULL;
	owner = enabled ? k_current_get() : NULL;
}

void lh_paged_heap_discard_frees(bool on)
{
	discard_frees = on;
}

void lh_paged_heap_reset(void)
{
	memset(small, 0, sizeof(small));
	memset(med, 0, sizeof(med));
	big = NULL;
	top = region;
	live = 0;
}

void lh_paged_heap_stats(struct lh_paged_stats *s)
{
	s->size = region_size;
	s->allocated = live;
	s->max_allocated = peak;
	s->high_water = high_water;
	s->free = region_size - (size_t)(top - region);
}

void *__wrap_malloc(size_t n)
{
	if (use_paged()) {
		void *p = paged_alloc(n, 8U);

		if (p != NULL) {
			return p;
		}
	}
	return __real_malloc(n);
}

void *__wrap_calloc(size_t a, size_t b)
{
	if (use_paged()) {
		size_t n;
		void *p;

		if (!__builtin_mul_overflow(a, b, &n) && (p = paged_alloc(n, 8U)) != NULL) {
			memset(p, 0, n);
			return p;
		}
	}
	return __real_calloc(a, b);
}

void *__wrap_aligned_alloc(size_t align, size_t n)
{
	if (use_paged()) {
		void *p = paged_alloc(n, align);

		if (p != NULL) {
			return p;
		}
	}
	return __real_aligned_alloc(align, n);
}

void *__wrap_realloc(void *p, size_t n)
{
	if (owns(p)) {
		uint32_t old = blk_size(p);
		void *q;

		if (n <= old) {
			return p;
		}
		q = paged_alloc(n, 8U);
		if (q == NULL) {
			return NULL;
		}
		memcpy(q, p, old);
		paged_free(p);
		return q;
	}
	if (p == NULL && use_paged()) {
		return __wrap_malloc(n);
	}
	return __real_realloc(p, n);
}

void __wrap_free(void *p)
{
	if (owns(p)) {
		paged_free(p);
	} else {
		__real_free(p);
	}
}
