/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Self check of the theme: on the settings screen it switches between dark and light and through
 * the accent colours, as the switch and the dots of the screen do, and prints the palette and what
 * was saved. It ends with PASS or FAIL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "mp5.h"

static int step;
static int failures;

static void check(bool ok, const char *what)
{
	if (!ok) {
		failures++;
	}
	printk("theme: %-44s %s\n", what, ok ? "ok" : "FAILED");
}

static void test_step(lv_timer_t *t)
{
	const char *mode = cfg_get("theme");
	const char *acc = cfg_get("accent");
	static const bool dark[] = {false, false, true, true, false};
	static const int accent[] = {0, 3, 3, 5, 0};

	/* first what the previous step asked for, then the next request */
	if (step >= 2) {
		int i = step - 2;

		check(ui_theme_dark() == dark[i] && ui_theme_accent() == accent[i],
		      "palette follows the request");
		check(ui_pal.accent == ui_theme_accent_color(accent[i]), "accent color in the palette");
		check(ui_pal.bg == (dark[i] ? 0x0B0F17U : 0xF1F3F9U), "background of the mode");
		check(mode != NULL && strcmp(mode, dark[i] ? "dark" : "light") == 0 &&
		      acc != NULL && atoi(acc) == accent[i], "saved in the settings file");
		check(lv_screen_active() != NULL && lv_obj_get_child_count(lv_screen_active()) > 0,
		      "screen rebuilt and active");
	}
	if (step == 0) {
		ui_goto(SCR_SETTINGS);
	} else if (step <= 5) {
		ui_theme_set(dark[step - 1], accent[step - 1]);
	} else {
		check(failures == 0, "no step failed");
		printk("theme: %s\n", failures == 0 ? "PASS (0 failures)" : "FAIL");
		lv_timer_delete(t);
		return;
	}
	step++;
}

void ui_theme_test_start(void)
{
	lv_timer_create(test_step, 2500, NULL);
}
