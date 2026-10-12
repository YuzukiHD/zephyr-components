/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The screen manager: screens register with NEKOBOX_SCREEN and are found by name. A screen is made
 * the first time it is shown and kept; a change of theme makes all of them again.
 */

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "core/app.h"
#include "core/storage.h"
#include "debug/stats.h"
#include "ui/ui.h"

#define MAX_SCREENS	16

static lv_obj_t *screens[MAX_SCREENS];
static const struct nekobox_screen *current;

static int screen_index(const struct nekobox_screen *s)
{
	const struct nekobox_screen *first;

	STRUCT_SECTION_GET(nekobox_screen, 0, &first);

	return s - first;
}

static const struct nekobox_screen *screen_find(const char *name)
{
	STRUCT_SECTION_FOREACH(nekobox_screen, s) {
		if (strcmp(s->name, name) == 0) {
			return s;
		}
	}

	return NULL;
}

/*
 * A new screen is drawn band by band into the frame buffer the display engine is scanning out,
 * which shows as a wipe from the top. The plane is kept off until the whole screen is drawn.
 */
static void screen_drawn(lv_event_t *e)
{
	lv_display_remove_event_cb_with_user_data(lv_event_get_target(e), screen_drawn, NULL);
	ui_overlay_set(true);
}

void ui_goto_arg(const char *name, void *arg)
{
	const struct nekobox_screen *s = screen_find(name);
	int i;

	if (s == NULL) {
		return;
	}
	i = screen_index(s);
	if (screens[i] == NULL) {
		screens[i] = s->create();
	}
	if (current != NULL && current->leave != NULL) {
		current->leave();
	}
	current = s;
	ui_stats_page_begin(s->name);
	{
		uint32_t c0 = k_cycle_get_32();

		if (s->enter != NULL) {
			s->enter(arg);
		}
		ui_stats_page_set_up(k_cyc_to_us_floor32(k_cycle_get_32() - c0));
	}
	ui_overlay_set(false);
	lv_display_add_event_cb(lv_display_get_default(), screen_drawn, LV_EVENT_REFR_READY, NULL);
	lv_screen_load(screens[i]);
}

void ui_goto(const char *name)
{
	ui_goto_arg(name, NULL);
}

/* every screen is made again with the new colours */
static void rebuild(void *unused)
{
	const struct nekobox_screen *cur = current;
	bool made[MAX_SCREENS] = {0};
	lv_obj_t *tmp = lv_obj_create(NULL);

	ARG_UNUSED(unused);
	if (cur != NULL && cur->leave != NULL) {
		cur->leave();
	}
	lv_screen_load(tmp);
	STRUCT_SECTION_FOREACH(nekobox_screen, s) {
		int i = screen_index(s);

		made[i] = screens[i] != NULL;
		if (made[i]) {
			lv_obj_delete(screens[i]);
			screens[i] = NULL;
		}
	}
	/* the screens that have timers of their own must have their objects again at once */
	STRUCT_SECTION_FOREACH(nekobox_screen, s) {
		int i = screen_index(s);

		if (made[i]) {
			screens[i] = s->create();
		}
	}
	current = NULL;
	ui_goto(cur != NULL ? cur->name : "home");
	lv_obj_delete_async(tmp);
}

void ui_rebuild_all(void)
{
	/* the screen that asks is one of those to be deleted: do it after its event */
	lv_async_call(rebuild, NULL);
}

static int ui_step(void)
{
	ui_stats_init();
	ui_goto("home");

	return 0;
}
NEKOBOX_INIT(NEKOBOX_STAGE_UI, 20, "interface", ui_step);

/* the first picture: drawn, then the panel is switched on */
void ui_present(void)
{
	static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	lv_timer_handler();
	display_blanking_off(disp);
	printk("nekobox: boot %u ms: first picture\n", k_uptime_get_32());
}

/* the user interface thread: how long a redraw takes shows in the log when it is slow */
void ui_loop(void)
{
	printk("nekobox: %d x %d\n", (int)lv_display_get_horizontal_resolution(NULL),
	       (int)lv_display_get_vertical_resolution(NULL));
	for (;;) {
		static uint32_t slow, worst_us, report_ms;
		static unsigned int card_seen;
		uint32_t t0 = k_cycle_get_32(), next, us;

		/* a card that came later: the saved theme and brightness are in the settings now */
		if (card_seen != storage_generation()) {
			card_seen = storage_generation();
			if (storage_ready()) {
				ui_theme_reload();
			}
		}

		next = lv_timer_handler();
		us = k_cyc_to_us_floor32(k_cycle_get_32() - t0);
		if (us > 30000U) {
			slow++;
		}
		worst_us = MAX(worst_us, us);
		if (k_uptime_get_32() - report_ms >= 5000U) {
			if (worst_us > 8000U) {
				printk("ui: worst redraw %u ms, %u slower than 30 ms\n", worst_us / 1000,
				       slow);
			}
			slow = worst_us = 0;
			report_ms = k_uptime_get_32();
		}
		k_msleep(MIN(next, 20U));
	}
}
