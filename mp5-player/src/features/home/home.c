/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The home screen: a clock and the tiles of the functions of the board */

#include <stdio.h>
#include <zephyr/kernel.h>

#include "core/clock.h"
#include "core/storage.h"
#include "ui/ui.h"

#define TILE_W		220
#define TILE_H		196
#define TILE_GAP	21

static const char *const day_names[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday",
					"Friday", "Saturday"};
static const char *const month_names[] = {"January", "February", "March", "April", "May", "June",
					  "July", "August", "September", "October", "November",
					  "December"};

static lv_obj_t *time_label, *seconds_label, *date_label, *storage_label;

static int tile_count(void)
{
	int n = 0;

	STRUCT_SECTION_FOREACH(mp5_home_tile, t) {
		n++;
	}

	return n;
}

/* the n-th tile in the order of their order field */
static const struct mp5_home_tile *tile_nth(int n)
{
	const struct mp5_home_tile *prev = NULL;

	for (int k = 0; k <= n; k++) {
		const struct mp5_home_tile *best = NULL;

		STRUCT_SECTION_FOREACH(mp5_home_tile, t) {
			if ((prev == NULL || t->order > prev->order) &&
			    (best == NULL || t->order < best->order)) {
				best = t;
			}
		}
		prev = best;
	}

	return prev;
}

static void tile_clicked(lv_event_t *e)
{
	const struct mp5_home_tile *t = lv_event_get_user_data(e);

	if (t->screen == NULL) {
		ui_files_open(NULL, t->kind);
	} else {
		ui_goto(t->screen);
	}
}

static lv_obj_t *tile_create(lv_obj_t *parent, const struct mp5_home_tile *t)
{
	lv_obj_t *card = lv_obj_create(parent);
	lv_obj_t *badge = lv_obj_create(card);
	lv_obj_t *sym = lv_label_create(badge);
	lv_obj_t *title = lv_label_create(card);
	lv_obj_t *sub = lv_label_create(card);

	lv_obj_remove_style_all(card);
	lv_obj_set_size(card, TILE_W, TILE_H);
	lv_obj_set_style_radius(card, 32, 0);
	lv_obj_set_style_bg_color(card, lv_color_hex(C_SURFACE), 0);
	lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(card, 2, 0);
	lv_obj_set_style_border_color(card, lv_color_hex(C_BORDER), 0);
	lv_obj_set_style_bg_color(card, lv_color_hex(C_SURFACE2), LV_STATE_PRESSED);
	lv_obj_set_style_border_color(card, lv_color_hex(t->color_top), LV_STATE_PRESSED);
	lv_obj_set_style_pad_all(card, 22, 0);
	lv_obj_set_style_pad_row(card, 6, 0);
	lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
	lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(card, tile_clicked, LV_EVENT_CLICKED, (void *)t);

	lv_obj_remove_style_all(badge);
	lv_obj_set_size(badge, 64, 64);
	lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(badge, lv_color_hex(t->color_top), 0);
	lv_obj_set_style_bg_grad_color(badge, lv_color_hex(t->color_bottom), 0);
	lv_obj_set_style_bg_grad_dir(badge, LV_GRAD_DIR_VER, 0);
	lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_margin_bottom(badge, 14, 0);
	lv_obj_set_style_text_font(sym, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(sym, lv_color_white(), 0);
	lv_label_set_text(sym, t->symbol);
	lv_obj_center(sym);

	lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
	lv_obj_set_style_text_color(title, lv_color_hex(C_TEXT), 0);
	lv_label_set_text(title, t->title);

	lv_obj_set_width(sub, LV_PCT(100));
	lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(sub, lv_color_hex(C_DIM), 0);
	lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
	lv_label_set_text(sub, t->subtitle);

	return card;
}

static void clock_refresh(void)
{
	struct tm tm;

	clock_now(&tm);
	lv_label_set_text_fmt(time_label, "%02d:%02d", tm.tm_hour, tm.tm_min);
	lv_label_set_text_fmt(seconds_label, "%02d", tm.tm_sec);
	lv_label_set_text_fmt(date_label, "%s\n%d %s %d", day_names[tm.tm_wday], tm.tm_mday,
			      month_names[tm.tm_mon], tm.tm_year + 1900);
}

static lv_timer_t *clock_tmr;

static void clock_timer(lv_timer_t *t)
{
	ARG_UNUSED(t);
	clock_refresh();
}

static void storage_refresh(void)
{
	uint64_t total, avail;

	if (storage_space(&total, &avail)) {
		lv_label_set_text_fmt(storage_label, LV_SYMBOL_SD_CARD "   SD card   %u.%u GB free of %u.%u GB",
				      (unsigned int)(avail * 10 / 1000000000ULL / 10),
				      (unsigned int)(avail * 10 / 1000000000ULL % 10),
				      (unsigned int)(total * 10 / 1000000000ULL / 10),
				      (unsigned int)(total * 10 / 1000000000ULL % 10));
	} else {
		lv_label_set_text(storage_label, LV_SYMBOL_WARNING "   No SD card");
	}
}

static void set_time_open(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_set_time_dialog(clock_refresh);
}

/* ---- the screen ------------------------------------------------------------------------- */

static lv_obj_t *home_create(void)
{
	lv_obj_t *scr = ui_screen_new(false);
	lv_obj_t *card, *left, *brand, *row;

	lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_all(scr, 36, 0);
	lv_obj_set_style_pad_row(scr, 20, 0);

	/* the clock */
	card = lv_obj_create(scr);
	lv_obj_remove_style_all(card);
	lv_obj_set_size(card, LV_PCT(100), 170);
	lv_obj_set_style_radius(card, 36, 0);
	lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(card, lv_color_hex(C_ACCENT), 0);
	lv_obj_set_style_bg_grad_color(card, lv_color_hex(C_ACCENT2), 0);
	lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_HOR, 0);
	lv_obj_set_style_pad_hor(card, 44, 0);
	lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(card, set_time_open, LV_EVENT_CLICKED, NULL);

	left = lv_obj_create(card);
	lv_obj_remove_style_all(left);
	lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
	lv_obj_align(left, LV_ALIGN_LEFT_MID, 0, 0);
	lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
	lv_obj_set_style_pad_column(left, 14, 0);
	lv_obj_remove_flag(left, LV_OBJ_FLAG_CLICKABLE);

	time_label = lv_label_create(left);
	lv_obj_set_style_text_font(time_label, &lv_font_montserrat_48, 0);
	lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
	seconds_label = lv_label_create(left);
	lv_obj_set_style_text_font(seconds_label, &lv_font_montserrat_24, 0);
	lv_obj_set_style_text_color(seconds_label, lv_color_hex(0xD6DDFF), 0);
	lv_obj_set_style_pad_bottom(seconds_label, 6, 0);

	date_label = lv_label_create(card);
	lv_obj_align(date_label, LV_ALIGN_RIGHT_MID, 0, 0);
	lv_obj_set_style_text_font(date_label, &lv_font_montserrat_24, 0);
	lv_obj_set_style_text_color(date_label, lv_color_white(), 0);
	lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_set_style_text_line_space(date_label, 8, 0);

	brand = lv_label_create(card);
	lv_obj_align(brand, LV_ALIGN_TOP_LEFT, 0, 24);
	lv_obj_set_style_text_font(brand, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(brand, lv_color_hex(0xD6DDFF), 0);
	lv_obj_set_style_text_letter_space(brand, 3, 0);
	lv_label_set_text(brand, "YUZUKINEKO  MP5");

	/* the functions, four to a row */
	row = NULL;
	for (int i = 0; i < tile_count(); i++) {
		const struct mp5_home_tile *t = tile_nth(i);

		if (i % 4 == 0) {
			row = lv_obj_create(scr);
			lv_obj_remove_style_all(row);
			lv_obj_set_size(row, LV_PCT(100), TILE_H);
			lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
			lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
					      LV_FLEX_ALIGN_START);
			lv_obj_set_style_pad_column(row, TILE_GAP, 0);
			lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
		}
		tile_create(row, t);
	}

	storage_label = lv_label_create(scr);
	lv_obj_set_width(storage_label, LV_PCT(100));
	lv_obj_set_style_text_font(storage_label, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(storage_label, lv_color_hex(C_DIM), 0);
	lv_obj_set_style_pad_top(storage_label, 6, 0);

	if (clock_tmr == NULL) {
		clock_tmr = lv_timer_create(clock_timer, 1000, NULL);
	}
	clock_refresh();
	storage_refresh();

	return scr;
}

static void home_enter(void *arg)
{
	ARG_UNUSED(arg);
	clock_refresh();
	storage_refresh();
}

MP5_SCREEN(home, "home", home_create, home_enter, NULL);
