/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The settings screen: the date and time and the board are its own rows, every other row is
 * registered by the feature it belongs to (MP5_SETTINGS_ROW) and listed in the order field.
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "core/clock.h"
#include "features/settings/settings.h"
#include "ui/ui.h"

#define ROW_H	96

static lv_obj_t *time_value;
static lv_timer_t *refresh_timer;

static void back_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_goto("home");
}

/* a row: a title, the value on the right, tapping it does something */
lv_obj_t *settings_row_create(lv_obj_t *parent, const char *symbol, const char *title,
				      lv_obj_t **value, lv_event_cb_t cb)
{
	lv_obj_t *row = lv_obj_create(parent);
	lv_obj_t *icon = lv_label_create(row);
	lv_obj_t *t = lv_label_create(row);
	lv_obj_t *v = lv_label_create(row);

	lv_obj_remove_style_all(row);
	lv_obj_set_size(row, LV_PCT(100), ROW_H - 12);
	lv_obj_set_style_radius(row, 20, 0);
	lv_obj_set_style_bg_color(row, lv_color_hex(C_SURFACE), 0);
	lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(row, lv_color_hex(C_SURFACE2), LV_STATE_PRESSED);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_hor(row, 28, 0);
	lv_obj_set_style_pad_column(row, 22, 0);
	lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
	if (cb != NULL) {
		lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, NULL);
	} else {
		lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
	}

	lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(icon, lv_color_hex(C_ACCENT), 0);
	lv_obj_set_width(icon, 40);
	lv_label_set_text(icon, symbol);
	lv_obj_set_style_text_font(t, &lv_font_montserrat_24, 0);
	lv_obj_set_style_text_color(t, lv_color_hex(C_TEXT), 0);
	lv_obj_set_flex_grow(t, 1);
	lv_label_set_text(t, title);
	lv_obj_set_style_text_font(v, &lv_font_montserrat_20, 0);
	lv_obj_set_style_text_color(v, lv_color_hex(C_DIM), 0);
	lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
	if (value != NULL) {
		*value = v;
	}

	return row;
}

static void time_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_set_time_dialog(NULL);
}

static void time_row(lv_obj_t *list)
{
	settings_row_create(list, LV_SYMBOL_BELL, "Date and time", &time_value, time_clicked);
}

static void time_refresh(void)
{
	struct tm tm;
	char buf[64];

	clock_now(&tm);
	snprintf(buf, sizeof(buf), "%04d-%02d-%02d  %02d:%02d:%02d", tm.tm_year + 1900,
		 tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	lv_label_set_text(time_value, buf);
}

static void about_row(lv_obj_t *list)
{
	lv_obj_t *about;
	char buf[96];

	snprintf(buf, sizeof(buf), "Built %s %s", __DATE__, __TIME__);
	settings_row_create(list, LV_SYMBOL_SETTINGS, "About", &about, NULL);
	lv_label_set_text(about, buf);
}

MP5_SETTINGS_ROW(time, 10, time_row, time_refresh);
MP5_SETTINGS_ROW(about, 90, about_row, NULL);

/* calls @p fn for every row in the order of their order field */
static void rows_each(void (*fn)(const struct mp5_settings_row *r, void *arg), void *arg)
{
	const struct mp5_settings_row *prev = NULL;
	int n = 0;

	STRUCT_SECTION_FOREACH(mp5_settings_row, r) {
		n++;
	}
	for (int k = 0; k < n; k++) {
		const struct mp5_settings_row *best = NULL;

		STRUCT_SECTION_FOREACH(mp5_settings_row, r) {
			if ((prev == NULL || r->order > prev->order) &&
			    (best == NULL || r->order < best->order)) {
				best = r;
			}
		}
		fn(best, arg);
		prev = best;
	}
}

static void row_make(const struct mp5_settings_row *r, void *list)
{
	r->create(list);
}

static void row_refresh(const struct mp5_settings_row *r, void *unused)
{
	ARG_UNUSED(unused);
	if (r->refresh != NULL) {
		r->refresh();
	}
}

static void refresh(lv_timer_t *t)
{
	ARG_UNUSED(t);
	rows_each(row_refresh, NULL);
}

static lv_obj_t *settings_create(void)
{
	lv_obj_t *scr = ui_screen_new(false);
	lv_obj_t *bar = ui_title_bar(scr, "Settings", back_clicked);
	lv_obj_t *list = lv_obj_create(scr);

	ARG_UNUSED(bar);
	lv_obj_remove_style_all(list);
	lv_obj_set_size(list, LV_PCT(100), 768 - 88);
	lv_obj_set_pos(list, 0, 88);
	lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_hor(list, 32, 0);
	lv_obj_set_style_pad_row(list, 12, 0);
	lv_obj_set_scroll_dir(list, LV_DIR_VER);
	rows_each(row_make, list);

	return scr;
}

static void settings_enter(void *arg)
{
	ARG_UNUSED(arg);
	if (refresh_timer == NULL) {
		refresh_timer = lv_timer_create(refresh, 1000, NULL);
	}
	lv_timer_resume(refresh_timer);
	refresh(NULL);
}

static void settings_leave(void)
{
	if (refresh_timer != NULL) {
		lv_timer_pause(refresh_timer);
	}
}

MP5_SCREEN(settings, "settings", settings_create, settings_enter, settings_leave);
MP5_HOME_TILE(settings, .order = 70, .symbol = LV_SYMBOL_SETTINGS, .title = "Settings",
	      .subtitle = "Time and network", .color_top = 0x8B95A8, .color_bottom = 0x5B6478,
	      .screen = "settings");
