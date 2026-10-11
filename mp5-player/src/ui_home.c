/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The home screen: a clock and the tiles of the functions of the board */

#include <stdio.h>
#include <zephyr/kernel.h>

#include "mp5.h"

#define TILE_W		220
#define TILE_H		196
#define TILE_GAP	21

struct tile {
	const char *symbol;
	const char *title;
	const char *subtitle;
	uint32_t color_top;
	uint32_t color_bottom;
	enum screen_id target;
	enum files_filter filter;	/**< for the file browser */
};

static const struct tile tiles[] = {
	{LV_SYMBOL_VIDEO, "Videos", "Recently played", 0x6C8CFF, 0x4A5DE8, SCR_RECENT},
	{LV_SYMBOL_DIRECTORY, "Files", "Browse the SD card", 0xFFB547, 0xF2822C, SCR_FILES,
	 FILES_ALL},
	{LV_SYMBOL_IMAGE, "Photos", "JPEG and PNG", 0x3DDC97, 0x1FA97A, SCR_PHOTOS},
	{LV_SYMBOL_USB, "USB Display", "Second screen for a PC", 0xFF6C9B, 0xD443A8, SCR_USB},
	{LV_SYMBOL_KEYBOARD, "NES", "Famicom games", 0xE5484D, 0xB02A37, SCR_FILES, FILES_NES},
	{LV_SYMBOL_PLAY, "Game Boy", "GB, GBC and GBA", 0x9B6CFF, 0x6A3FD0, SCR_FILES, FILES_GB},
	{LV_SYMBOL_SETTINGS, "Settings", "Time and network", 0x8B95A8, 0x5B6478, SCR_SETTINGS},
};

static const char *const day_names[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday",
					"Friday", "Saturday"};
static const char *const month_names[] = {"January", "February", "March", "April", "May", "June",
					  "July", "August", "September", "October", "November",
					  "December"};

static lv_obj_t *time_label, *seconds_label, *date_label, *storage_label;
static lv_obj_t *set_dialog, *hour_roller, *minute_roller;

static void tile_clicked(lv_event_t *e)
{
	const struct tile *t = lv_event_get_user_data(e);

	if (t->target == SCR_FILES) {
		ui_files_open(NULL, t->filter);
	} else {
		ui_goto(t->target);
	}
}

static lv_obj_t *tile_create(lv_obj_t *parent, const struct tile *t)
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

/* ---- the dialog that sets the time ------------------------------------------------------ */

static void roller_style(lv_obj_t *r)
{
	lv_obj_remove_style_all(r);
	lv_obj_set_width(r, 130);
	lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
	lv_obj_set_style_text_font(r, &lv_font_montserrat_48, 0);
	lv_obj_set_style_text_color(r, lv_color_hex(C_DIM), 0);
	lv_obj_set_style_text_color(r, lv_color_white(), LV_PART_SELECTED);
	lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_SELECTED);
	lv_obj_set_style_bg_color(r, lv_color_hex(C_ACCENT), LV_PART_SELECTED);
	lv_obj_set_style_radius(r, 20, LV_PART_SELECTED);
	lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, 0);
}

static void dialog_close(lv_event_t *e)
{
	ARG_UNUSED(e);
	if (set_dialog != NULL) {
		lv_obj_delete(set_dialog);
		set_dialog = NULL;
	}
}

static void dialog_apply(lv_event_t *e)
{
	clock_set(lv_roller_get_selected(hour_roller), lv_roller_get_selected(minute_roller));
	clock_refresh();
	dialog_close(e);
}

static lv_obj_t *dialog_button(lv_obj_t *parent, const char *text, uint32_t color, lv_event_cb_t cb)
{
	lv_obj_t *b = lv_button_create(parent);
	lv_obj_t *l = lv_label_create(b);

	lv_obj_remove_style_all(b);
	lv_obj_set_size(b, 170, 64);
	lv_obj_set_style_radius(b, 32, 0);
	lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
	lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
	lv_obj_set_style_text_color(l, lv_color_white(), 0);
	lv_label_set_text(l, text);
	lv_obj_center(l);
	lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);

	return b;
}

void ui_set_time_dialog(void);

static void set_time_open(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_set_time_dialog();
}

void ui_set_time_dialog(void)
{
	static char hours[24 * 3 + 1], minutes[60 * 3 + 1];
	lv_obj_t *panel, *title, *row, *colon, *buttons;
	struct tm tm;

	if (set_dialog != NULL) {
		return;
	}
	for (int i = 0; i < 24; i++) {
		snprintf(hours + i * 3, 4, "%02d\n", i);
	}
	hours[24 * 3 - 1] = '\0';
	for (int i = 0; i < 60; i++) {
		snprintf(minutes + i * 3, 4, "%02d\n", i);
	}
	minutes[60 * 3 - 1] = '\0';

	set_dialog = lv_obj_create(lv_layer_top());
	lv_obj_remove_style_all(set_dialog);
	lv_obj_set_size(set_dialog, LV_PCT(100), LV_PCT(100));
	lv_obj_set_style_bg_color(set_dialog, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(set_dialog, LV_OPA_70, 0);
	lv_obj_add_flag(set_dialog, LV_OBJ_FLAG_CLICKABLE);

	panel = lv_obj_create(set_dialog);
	lv_obj_remove_style_all(panel);
	lv_obj_set_size(panel, 460, 520);
	lv_obj_center(panel);
	lv_obj_set_style_radius(panel, 36, 0);
	lv_obj_set_style_bg_color(panel, lv_color_hex(C_SURFACE), 0);
	lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(panel, 2, 0);
	lv_obj_set_style_border_color(panel, lv_color_hex(C_BORDER), 0);
	lv_obj_set_style_pad_all(panel, 28, 0);
	lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_row(panel, 22, 0);

	title = lv_label_create(panel);
	lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(title, lv_color_hex(C_TEXT), 0);
	lv_label_set_text(title, "Set the time");

	row = lv_obj_create(panel);
	lv_obj_remove_style_all(row);
	lv_obj_set_size(row, LV_PCT(100), 270);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(row, 12, 0);
	lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

	hour_roller = lv_roller_create(row);
	roller_style(hour_roller);
	lv_roller_set_options(hour_roller, hours, LV_ROLLER_MODE_NORMAL);
	lv_roller_set_visible_row_count(hour_roller, 4);
	colon = lv_label_create(row);
	lv_obj_set_style_text_font(colon, &lv_font_montserrat_48, 0);
	lv_obj_set_style_text_color(colon, lv_color_hex(C_DIM), 0);
	lv_label_set_text(colon, ":");
	minute_roller = lv_roller_create(row);
	roller_style(minute_roller);
	lv_roller_set_options(minute_roller, minutes, LV_ROLLER_MODE_NORMAL);
	lv_roller_set_visible_row_count(minute_roller, 4);

	clock_now(&tm);
	lv_roller_set_selected(hour_roller, tm.tm_hour, LV_ANIM_OFF);
	lv_roller_set_selected(minute_roller, tm.tm_min, LV_ANIM_OFF);

	buttons = lv_obj_create(panel);
	lv_obj_remove_style_all(buttons);
	lv_obj_set_size(buttons, LV_PCT(100), 64);
	lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	dialog_button(buttons, "Cancel", C_SURFACE2, dialog_close);
	dialog_button(buttons, "Set", C_ACCENT, dialog_apply);
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
	for (size_t i = 0; i < ARRAY_SIZE(tiles); i++) {
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
		tile_create(row, &tiles[i]);
	}

	storage_label = lv_label_create(scr);
	lv_obj_set_width(storage_label, LV_PCT(100));
	lv_obj_set_style_text_font(storage_label, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(storage_label, lv_color_hex(C_DIM), 0);
	lv_obj_set_style_pad_top(storage_label, 6, 0);

	lv_timer_create(clock_timer, 1000, NULL);
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

const struct screen_ops home_ops = {
	.create = home_create,
	.enter = home_enter,
};
