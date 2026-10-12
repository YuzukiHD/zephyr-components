/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The Videos screen: the videos that were played last, with the position to go on from */

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "history.h"
#include "debug/stats.h"
#include "features/video/ui_player.h"
#include "ui/ui.h"

#define ROW_H	96

static lv_obj_t *list, *empty_box;

static void time_text(char *buf, size_t n, uint32_t ms)
{
	uint32_t s = ms / 1000;

	if (s >= 3600) {
		snprintf(buf, n, "%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
	} else {
		snprintf(buf, n, "%u:%02u", s / 60, s % 60);
	}
}

static void back_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_goto("home");
}

static void browse_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_files_open(NULL, "video");
}

static void entry_clicked(lv_event_t *e)
{
	const struct history_entry *h = history_get((int)(uintptr_t)lv_event_get_user_data(e));
	char path[HISTORY_PATH_MAX];

	if (h != NULL) {
		strcpy(path, h->path);
		ui_player_open(path, "recent");
	}
}

static void entry_create(lv_obj_t *parent, int index, const struct history_entry *h)
{
	lv_obj_t *row = lv_button_create(parent);
	lv_obj_t *icon = lv_label_create(row);
	lv_obj_t *col = lv_obj_create(row);
	lv_obj_t *name = lv_label_create(col);
	lv_obj_t *bar = lv_bar_create(col);
	lv_obj_t *when = lv_label_create(row);
	const char *base = strrchr(h->path, '/');
	char a[16], b[16], text[40];

	base = base != NULL ? base + 1 : h->path;
	lv_obj_remove_style_all(row);
	lv_obj_set_size(row, LV_PCT(100), ROW_H - 12);
	lv_obj_set_style_radius(row, 20, 0);
	lv_obj_set_style_bg_color(row, lv_color_hex(C_SURFACE), 0);
	lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(row, lv_color_hex(C_SURFACE2), LV_STATE_PRESSED);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_hor(row, 24, 0);
	lv_obj_set_style_pad_column(row, 20, 0);
	lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(row, entry_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)index);

	lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(icon, lv_color_hex(C_ACCENT), 0);
	lv_label_set_text(icon, LV_SYMBOL_VIDEO);
	lv_obj_set_width(icon, 40);

	lv_obj_remove_style_all(col);
	lv_obj_set_height(col, LV_SIZE_CONTENT);
	lv_obj_set_flex_grow(col, 1);
	lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_row(col, 10, 0);
	lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_text_color(name, lv_color_hex(C_TEXT), 0);
	lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
	lv_obj_set_width(name, LV_PCT(100));
	lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
	lv_label_set_text(name, base);

	lv_obj_set_size(bar, LV_PCT(100), 6);
	lv_bar_set_range(bar, 0, 1000);
	lv_bar_set_value(bar, h->dur_ms > 0 ? (int32_t)((uint64_t)h->pos_ms * 1000 / h->dur_ms) : 0,
			 LV_ANIM_OFF);
	lv_obj_set_style_bg_color(bar, lv_color_hex(C_BORDER), LV_PART_MAIN);
	lv_obj_set_style_bg_color(bar, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
	lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);
	lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);

	time_text(a, sizeof(a), h->pos_ms);
	time_text(b, sizeof(b), h->dur_ms);
	snprintf(text, sizeof(text), "%s / %s", a, b);
	lv_obj_set_style_text_color(when, lv_color_hex(C_DIM), 0);
	lv_obj_set_style_text_font(when, &lv_font_montserrat_20, 0);
	lv_label_set_text(when, text);
}

static void scroll_begin(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_stats_scroll_begin("videos");
}

static void scroll_end(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_stats_scroll_end();
}

static lv_obj_t *recent_create(void)
{
	lv_obj_t *scr = ui_screen_new(false);
	lv_obj_t *bar = ui_title_bar(scr, "Videos", back_clicked);
	lv_obj_t *filler = lv_obj_create(bar);
	lv_obj_t *browse = lv_button_create(bar);
	lv_obj_t *bl = lv_label_create(browse);
	lv_obj_t *hint;

	lv_obj_remove_style_all(filler);
	lv_obj_set_size(filler, 1, 1);
	lv_obj_set_flex_grow(filler, 1);
	lv_obj_remove_style_all(browse);
	lv_obj_set_size(browse, 190, 56);
	lv_obj_set_style_radius(browse, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(browse, lv_color_hex(C_ACCENT), 0);
	lv_obj_set_style_bg_opa(browse, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(browse, lv_color_hex(C_ACCENT2), LV_STATE_PRESSED);
	lv_obj_set_style_text_color(bl, lv_color_hex(C_TEXT), 0);
	lv_obj_set_style_text_font(bl, &lv_font_montserrat_20, 0);
	lv_label_set_text(bl, LV_SYMBOL_DIRECTORY "  Browse");
	lv_obj_center(bl);
	lv_obj_add_event_cb(browse, browse_clicked, LV_EVENT_CLICKED, NULL);

	list = lv_obj_create(scr);
	lv_obj_remove_style_all(list);
	lv_obj_set_size(list, LV_PCT(100), 768 - 88);
	lv_obj_set_pos(list, 0, 88);
	lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_hor(list, 32, 0);
	lv_obj_set_style_pad_row(list, 12, 0);
	lv_obj_set_style_pad_bottom(list, 24, 0);
	lv_obj_set_scroll_dir(list, LV_DIR_VER);
	lv_obj_add_event_cb(list, scroll_begin, LV_EVENT_SCROLL_BEGIN, NULL);
	lv_obj_add_event_cb(list, scroll_end, LV_EVENT_SCROLL_END, NULL);

	empty_box = lv_obj_create(scr);
	hint = lv_label_create(empty_box);
	lv_obj_remove_style_all(empty_box);
	lv_obj_set_size(empty_box, 700, LV_SIZE_CONTENT);
	lv_obj_align(empty_box, LV_ALIGN_CENTER, 0, -40);
	lv_obj_set_style_text_color(hint, lv_color_hex(C_DIM), 0);
	lv_obj_set_style_text_font(hint, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(hint, 700);
	lv_label_set_text(hint, "No video played yet\nOpen one with Browse");

	return scr;
}

static void recent_enter(void *arg)
{
	int n = history_count();

	ARG_UNUSED(arg);
	lv_obj_clean(list);
	for (int i = 0; i < n; i++) {
		entry_create(list, i, history_get(i));
	}
	if (n == 0) {
		lv_obj_remove_flag(empty_box, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(empty_box, LV_OBJ_FLAG_HIDDEN);
	}
	lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
}

NEKOBOX_SCREEN(recent, "recent", recent_create, recent_enter, NULL);
