/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The dialog that sets the time of day */

#include <stdio.h>

#include "core/clock.h"
#include "ui/ui.h"

static void (*applied)(void);
static lv_obj_t *set_dialog, *hour_roller, *minute_roller;

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
	if (applied != NULL) {
		applied();
	}
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

void ui_set_time_dialog(void (*done)(void))
{
	static char hours[24 * 3 + 1], minutes[60 * 3 + 1];
	lv_obj_t *panel, *title, *row, *colon, *buttons;
	struct tm tm;

	if (set_dialog != NULL) {
		return;
	}
	applied = done;
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
