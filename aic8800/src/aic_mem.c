/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The memory of the Wi-Fi driver: one block of CONFIG_AIC8800_MEM_POOL_KB in the image, managed
 * as a heap of its own. A request the pool cannot serve fails, it does not fall back to the
 * malloc arena of the application (that is the point of the pool). A pointer that is not from
 * the pool (a string from strdup) is given back to the C library.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/sys_heap.h>

/* the real functions, not the macros of aic_mem_redirect.h */
#undef malloc
#undef calloc
#undef realloc
#undef aligned_alloc

extern void __real_free(void *ptr);
extern void *realloc(void *ptr, size_t size);

#define POOL_BYTES	(CONFIG_AIC8800_MEM_POOL_KB * 1024)
#define DEFAULT_ALIGN	8

static uint8_t pool[POOL_BYTES] __aligned(64) __noinit;
static struct sys_heap heap;
static bool ready;
static bool warned;
static K_MUTEX_DEFINE(lock);

static bool in_pool(const void *p)
{
	return (const uint8_t *)p >= pool && (const uint8_t *)p < pool + sizeof(pool);
}

static void ensure_ready(void)
{
	if (!ready) {
		sys_heap_init(&heap, pool, sizeof(pool));
		ready = true;
	}
}

static void *pool_alloc(size_t align, size_t size)
{
	void *p;

	k_mutex_lock(&lock, K_FOREVER);
	ensure_ready();
	p = sys_heap_aligned_alloc(&heap, align, size ? size : 1);
	k_mutex_unlock(&lock);
	if (p == NULL && !warned) {
		warned = true;
		printk("aic8800: memory pool of %u KiB is full (%u bytes asked)\n",
		       (unsigned int)(POOL_BYTES / 1024), (unsigned int)size);
	}

	return p;
}

void *aic_mem_malloc(size_t size)
{
	return pool_alloc(DEFAULT_ALIGN, size);
}

void *aic_mem_aligned_alloc(size_t align, size_t size)
{
	return pool_alloc(MAX(align, (size_t)DEFAULT_ALIGN), size);
}

void *aic_mem_calloc(size_t n, size_t size)
{
	void *p;

	if (size != 0 && n > SIZE_MAX / size) {
		return NULL;
	}
	p = pool_alloc(DEFAULT_ALIGN, n * size);
	if (p != NULL) {
		memset(p, 0, n * size);
	}

	return p;
}

void *aic_mem_realloc(void *ptr, size_t size)
{
	void *p;

	if (ptr == NULL) {
		return pool_alloc(DEFAULT_ALIGN, size);
	}
	if (!in_pool(ptr)) {
		return realloc(ptr, size);
	}
	k_mutex_lock(&lock, K_FOREVER);
	p = sys_heap_aligned_realloc(&heap, ptr, DEFAULT_ALIGN, size);
	k_mutex_unlock(&lock);

	return p;
}

/* every free() of the image (--wrap=free): a pointer of the pool goes back to the pool */
void __wrap_free(void *ptr)
{
	if (ptr == NULL) {
		return;
	}
	if (!in_pool(ptr)) {
		__real_free(ptr);
		return;
	}
	k_mutex_lock(&lock, K_FOREVER);
	sys_heap_free(&heap, ptr);
	k_mutex_unlock(&lock);
}

#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
/* what the driver has taken and the most it has taken at one time, in bytes */
void aic_mem_stats(size_t *used, size_t *peak)
{
	struct sys_memory_stats st = {0};

	k_mutex_lock(&lock, K_FOREVER);
	ensure_ready();
	sys_heap_runtime_stats_get(&heap, &st);
	k_mutex_unlock(&lock);
	*used = st.allocated_bytes;
	*peak = st.max_allocated_bytes;
}
#endif
