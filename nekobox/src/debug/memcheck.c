/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <zephyr/kernel.h>

#include "debug/memcheck.h"

#define STEP		4096U
#define MAX_BLOCKS	64

/* largest block that can be allocated, by bisection on the size */
static size_t largest_block(void)
{
	size_t lo = 0, hi = 16U * 1024 * 1024;

	while (hi - lo > STEP) {
		size_t mid = lo + (hi - lo) / 2;
		void *p = aligned_alloc(64, mid);

		if (p != NULL) {
			free(p);
			lo = mid;
		} else {
			hi = mid;
		}
	}

	return lo;
}

void nekobox_mem_probe(size_t *largest, size_t *total)
{
	void *blk[MAX_BLOCKS];
	size_t sum = 0;
	int n = 0;

	*largest = largest_block();
	/* take the largest block again and again until nothing is left */
	while (n < MAX_BLOCKS) {
		size_t size = largest_block();

		if (size < STEP) {
			break;
		}
		blk[n] = aligned_alloc(64, size);
		if (blk[n] == NULL) {
			break;
		}
		sum += size;
		n++;
	}
	while (n > 0) {
		free(blk[--n]);
	}
	*total = sum;
}
