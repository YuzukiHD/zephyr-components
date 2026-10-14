/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "debug/profile.h"

#define BLOCK_SHIFT	4
#define BUCKETS		1200
#define MAX_THREADS	40

static struct {
	uintptr_t block;
	uint32_t n;
} hist[BUCKETS];
static struct {
	uintptr_t block;
	uint32_t n;
} callers[BUCKETS];
static int ncallers;
static int used;
static uint32_t samples, ready_samples, dropped;
static volatile bool running;
static k_tid_t target;

static struct {
	const struct k_thread *th;
	uint64_t cycles;
} base[MAX_THREADS];
static int nbase;
static uint64_t base_all, base_idle;

static K_THREAD_STACK_DEFINE(sampler_stack, 2048);
static struct k_thread sampler_thread;
static bool created;

static void sample(void)
{
	const struct k_thread *th = target;
	uintptr_t sp = (uintptr_t)th->callee_saved.sp;

	samples++;
	/* a thread that waits for something has no interrupted code to look at */
	if ((th->base.thread_state & (_THREAD_PENDING | _THREAD_SUSPENDED | _THREAD_DEAD | _THREAD_ABORTING)) != 0 ||
	    sp < th->stack_info.start || sp >= th->stack_info.start + th->stack_info.size) {
		return;
	}
	ready_samples++;
	{
		const struct arch_esf *e = (const void *)sp;
		uintptr_t block = e->mepc >> BLOCK_SHIFT;
		uintptr_t from = e->ra >> BLOCK_SHIFT;
		int i;

		/* the caller of a small leaf function such as malloc or memcpy */
		for (i = 0; i < ncallers; i++) {
			if (callers[i].block == from) {
				callers[i].n++;
				break;
			}
		}
		if (i == ncallers && ncallers < BUCKETS) {
			callers[ncallers].block = from;
			callers[ncallers].n = 1;
			ncallers++;
		}

		for (i = 0; i < used; i++) {
			if (hist[i].block == block) {
				hist[i].n++;
				return;
			}
		}
		if (used < BUCKETS) {
			hist[used].block = block;
			hist[used].n = 1;
			used++;
		} else {
			dropped++;
		}
	}
}

static void sampler(void *a, void *b, void *c)
{
	for (;;) {
		k_sleep(K_MSEC(2));
		if (running) {
			sample();
		}
	}
}

static void note_thread(const struct k_thread *th, void *user)
{
	k_thread_runtime_stats_t st = {0};

	if (nbase < MAX_THREADS) {
		k_thread_runtime_stats_get((k_tid_t)th, &st);
		base[nbase].th = th;
		base[nbase].cycles = st.execution_cycles;
		nbase++;
	}
}

void prof_start(void)
{
	k_thread_runtime_stats_t all;

	running = false;
	memset(hist, 0, sizeof(hist));
	used = 0;
	ncallers = 0;
	memset(callers, 0, sizeof(callers));
	samples = ready_samples = dropped = 0;
	target = k_current_get();
	nbase = 0;
	k_thread_foreach(note_thread, NULL);
	k_thread_runtime_stats_all_get(&all);
	base_all = all.execution_cycles;
	base_idle = all.idle_cycles;
	if (!created) {
		created = true;
		k_thread_create(&sampler_thread, sampler_stack, K_THREAD_STACK_SIZEOF(sampler_stack), sampler, NULL,
				NULL, NULL, K_PRIO_COOP(0), 0, K_NO_WAIT);
		k_thread_name_set(&sampler_thread, "profile");
	}
	running = true;
}

static void show_cpu(const struct k_thread *th, void *user)
{
	uint64_t total = *(uint64_t *)user;
	k_thread_runtime_stats_t st = {0};
	uint64_t before = 0;

	k_thread_runtime_stats_get((k_tid_t)th, &st);
	for (int i = 0; i < nbase; i++) {
		if (base[i].th == th) {
			before = base[i].cycles;
		}
	}
	if (st.execution_cycles - before > total / 200) {	/* more than half a percent */
		printk("profile:   %-18s prio %3d  %3u.%u %%\n", k_thread_name_get((k_tid_t)th) ? k_thread_name_get((k_tid_t)th) : "?",
		       k_thread_priority_get((k_tid_t)th), (unsigned)((st.execution_cycles - before) * 100 / total),
		       (unsigned)((st.execution_cycles - before) * 1000 / total % 10));
	}
}

void prof_report(const char *what)
{
	k_thread_runtime_stats_t all;
	uint64_t total, idle;

	running = false;
	k_thread_runtime_stats_all_get(&all);
	total = all.execution_cycles - base_all;
	idle = all.idle_cycles - base_idle;
	if (total == 0) {
		return;
	}
	printk("profile: samples are every 2 ms; %s: idle %u.%u %%, the interface thread was ready in %u of %u samples\n", what,
	       (unsigned)(idle * 100 / total), (unsigned)(idle * 1000 / total % 10), ready_samples, samples);
	k_thread_foreach(show_cpu, &total);
	/* the hottest blocks of code of the interface thread, most samples first */
	for (int rank = 0; rank < 150 && rank < used; rank++) {
		int best = -1;

		for (int i = 0; i < used; i++) {
			if (hist[i].n != 0 && (best < 0 || hist[i].n > hist[best].n)) {
				best = i;
			}
		}
		if (best < 0 || hist[best].n * 1000 < ready_samples) {
			break;
		}
		printk("profile:   %08lx %u\n", (unsigned long)(hist[best].block << BLOCK_SHIFT), hist[best].n);
		hist[best].n = 0;
	}
	/* the return addresses: who called the function that was running */
	for (int rank = 0; rank < 150 && rank < ncallers; rank++) {
		int best = -1;

		for (int i = 0; i < ncallers; i++) {
			if (callers[i].n != 0 && (best < 0 || callers[i].n > callers[best].n)) {
				best = i;
			}
		}
		if (best < 0 || callers[best].n * 1000 < ready_samples) {
			break;
		}
		printk("profile:   ra %08lx %u\n", (unsigned long)(callers[best].block << BLOCK_SHIFT), callers[best].n);
		callers[best].n = 0;
	}
	if (dropped != 0) {
		printk("profile: %u samples did not fit\n", dropped);
	}
}
