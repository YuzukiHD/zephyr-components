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

#define MAX_SAMPLES	2500
#define CODE_LO		0x40000000UL
#define CODE_HI		0x40200000UL

K_THREAD_STACK_DEFINE(prof_stack, 2048);
static struct k_thread prof_thread;
static uint32_t pcs[MAX_SAMPLES];
static uint32_t ras[MAX_SAMPLES];

extern volatile int pocketjs_scene_tag;

static int cmp(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;

	return x < y ? -1 : (x > y);
}

/* Collect MAX_SAMPLES program counters, then print the busiest blocks */
static void sample_window(struct k_thread *t, int tag)
{
	int n = 0, bad = 0;

	while (n < MAX_SAMPLES) {
		const struct arch_esf *esf = (const struct arch_esf *)t->callee_saved.sp;
		unsigned long pc = esf->mepc;

		if (pc >= CODE_LO && pc < CODE_HI) {
			ras[n] = esf->ra & ~63UL;
			pcs[n++] = pc & ~63UL;
		} else {
			bad++;
		}
		k_msleep(1);
	}
	/* callers: the return address register of the samples, as a second histogram */
	qsort(ras, n, sizeof(ras[0]), cmp);
	printk("callers: scene %d\n", tag);
	for (int top = 0; top < 12; top++) {
		int best = -1, best_cnt = 0;

		for (int i = 0; i < n;) {
			int j = i;

			while (j < n && ras[j] == ras[i]) {
				j++;
			}
			if (ras[i] != 0 && j - i > best_cnt) {
				best_cnt = j - i;
				best = i;
			}
			i = j;
		}
		if (best < 0) {
			break;
		}
		uint32_t v = ras[best];

		printk("ra %08x %d\n", v, best_cnt);
		for (int i = best; i < n && ras[i] == v; i++) {
			ras[i] = 0;
		}
	}
	qsort(pcs, n, sizeof(pcs[0]), cmp);
	printk("profile: scene %d: %d samples, %d unusable (thread blocked)\n", tag, n, bad);
	for (int top = 0; top < 24; top++) {
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

static void prof_main(void *target, void *b, void *c)
{
	k_sleep(K_SECONDS(4));
	while (true) {
		/* one window per scene change, taken a little after the change */
		const int tag = pocketjs_scene_tag;

		k_sleep(K_SECONDS(2));
		sample_window(target, tag);
		while (pocketjs_scene_tag == tag) {
			k_sleep(K_MSEC(200));
		}
	}
}

void profile_start(struct k_thread *target)
{
	k_thread_create(&prof_thread, prof_stack, K_THREAD_STACK_SIZEOF(prof_stack), prof_main,
			target, NULL, NULL, 1, 0, K_NO_WAIT);
}
