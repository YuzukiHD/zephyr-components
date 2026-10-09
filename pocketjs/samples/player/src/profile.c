/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * A poor man's profiler: a thread that preempts the emulation every
 * millisecond reads the program counter the emulation thread was interrupted
 * at, and after a while prints the busiest 64 byte blocks of code.
 */

#include <stdlib.h>
#include <zephyr/arch/exception.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define MAX_SAMPLES	6000
#define CODE_LO		0x40010000UL
#define CODE_HI		0x40200000UL

K_THREAD_STACK_DEFINE(prof_stack, 2048);
static struct k_thread prof_thread;
static uint32_t pcs[MAX_SAMPLES];

static int cmp(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;

	return x < y ? -1 : (x > y);
}

static void prof_main(void *target, void *b, void *c)
{
	struct k_thread *t = target;
	int n = 0, bad = 0;

	k_sleep(K_SECONDS(4));
	while (n < MAX_SAMPLES) {
		const struct arch_esf *esf = (const struct arch_esf *)t->callee_saved.sp;
		unsigned long pc = esf->mepc;

		if (pc >= CODE_LO && pc < CODE_HI) {
			pcs[n++] = pc & ~63UL;
		} else {
			bad++;
		}
		k_msleep(1);
	}
	qsort(pcs, n, sizeof(pcs[0]), cmp);
	printk("profile: %d samples, %d unusable\n", n, bad);
	for (int top = 0; top < 45; top++) {
		int best = -1, best_cnt = 0;

		for (int i = 0; i < n;) {
			int j = i;

			while (j < n && pcs[j] == pcs[i]) {
				j++;
			}
			if (pcs[i] != 0 && j - i > best_cnt) {
				best_cnt = j - i;
				best = i;
			}
			i = j;
		}
		if (best < 0) {
			break;
		}
		uint32_t v = pcs[best];

		printk("prof %08x %d\n", v, best_cnt);
		for (int i = best; i < n && pcs[i] == v; i++) {
			pcs[i] = 0;
		}
	}
}

void profile_start(struct k_thread *target)
{
	k_thread_create(&prof_thread, prof_stack, K_THREAD_STACK_SIZEOF(prof_stack), prof_main,
			target, NULL, NULL, 1, 0, K_NO_WAIT);
}
