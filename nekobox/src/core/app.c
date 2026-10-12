/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "core/app.h"

#define MAX_STEPS 32

int nekobox_run_stage(enum nekobox_stage stage)
{
	const struct nekobox_init *steps[MAX_STEPS];
	int n = 0;

	STRUCT_SECTION_FOREACH(nekobox_init, step) {
		if (step->stage == stage && n < MAX_STEPS) {
			int at = n++;

			/* sorted by order; steps of the same order keep the order of the link */
			while (at > 0 && steps[at - 1]->order > step->order) {
				steps[at] = steps[at - 1];
				at--;
			}
			steps[at] = step;
		}
	}
	for (int i = 0; i < n; i++) {
		int ret = steps[i]->fn();

		printk("nekobox: boot %u ms: %s%s\n", k_uptime_get_32(), steps[i]->name,
		       ret != 0 ? " FAILED" : "");
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

#define MAX_HOOKS 4

static atomic_t held;
static void (*hooks[MAX_HOOKS])(bool hold);

void nekobox_background_register(void (*fn)(bool hold))
{
	for (int i = 0; i < MAX_HOOKS; i++) {
		if (hooks[i] == NULL) {
			hooks[i] = fn;
			return;
		}
	}
}

void nekobox_background_hold(bool hold)
{
	if (atomic_set(&held, hold ? 1 : 0) == (hold ? 1 : 0)) {
		return;
	}
	for (int i = 0; i < MAX_HOOKS && hooks[i] != NULL; i++) {
		hooks[i](hold);
	}
}

bool nekobox_background_held(void)
{
	return atomic_get(&held) != 0;
}

void nekobox_background_wait(void)
{
	while (nekobox_background_held()) {
		k_msleep(200);
	}
}
