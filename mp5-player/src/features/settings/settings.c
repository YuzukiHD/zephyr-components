/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The settings: the date and time, the network and the network time, the board */

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "mp5.h"

#define ROW_H	96

static lv_obj_t *time_value, *net_value, *sync_value, *bt_value;
static lv_timer_t *refresh_timer;

static void back_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_goto(SCR_HOME);
}

/* a row: a title, the value on the right, tapping it does something */
static lv_obj_t *row_create(lv_obj_t *parent, const char *symbol, const char *title,
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
	ui_set_time_dialog();
}

static void refresh(lv_timer_t *t)
{
	struct tm tm;
	char buf[64], line[96];

	ARG_UNUSED(t);
	clock_now(&tm);
	snprintf(buf, sizeof(buf), "%04d-%02d-%02d  %02d:%02d:%02d", tm.tm_year + 1900,
		 tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	lv_label_set_text(time_value, buf);
	app_net_status(line, sizeof(line));
	lv_label_set_text(net_value, line);
	app_net_sync_status(line, sizeof(line));
	lv_label_set_text(sync_value, line);
	bt_status(line, sizeof(line));
	lv_label_set_text(bt_value, line);
}

static void wifi_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_goto(SCR_WIFI);
}

static void sync_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	app_net_sync_time();
	refresh(NULL);
}

static void dark_changed(lv_event_t *e)
{
	lv_obj_t *sw = lv_event_get_target(e);

	/* the screens are made again, the switch with them */
	ui_theme_set(lv_obj_has_state(sw, LV_STATE_CHECKED), ui_theme_accent());
}

static void accent_clicked(lv_event_t *e)
{
	ui_theme_set(ui_theme_dark(), (int)(uintptr_t)lv_event_get_user_data(e));
}

/* a row with a switch for dark or light */
static void dark_row(lv_obj_t *list)
{
	lv_obj_t *value;
	lv_obj_t *row = row_create(list, LV_SYMBOL_EYE_OPEN, "Dark mode", &value, NULL);
	lv_obj_t *sw = lv_switch_create(row);

	lv_obj_add_flag(value, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_style_bg_color(sw, lv_color_hex(C_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
	if (ui_theme_dark()) {
		lv_obj_add_state(sw, LV_STATE_CHECKED);
	}
	lv_obj_add_event_cb(sw, dark_changed, LV_EVENT_VALUE_CHANGED, NULL);
}

/* a row with the accent colours to choose from */
static void accent_row(lv_obj_t *list)
{
	lv_obj_t *value;
	lv_obj_t *row = row_create(list, LV_SYMBOL_IMAGE, "Accent color", &value, NULL);

	lv_obj_add_flag(value, LV_OBJ_FLAG_HIDDEN);
	for (int i = 0; i < UI_ACCENTS; i++) {
		lv_obj_t *dot = lv_button_create(row);

		lv_obj_remove_style_all(dot);
		lv_obj_set_size(dot, 48, 48);
		lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_bg_color(dot, lv_color_hex(ui_theme_accent_color(i)), 0);
		lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
		if (i == ui_theme_accent()) {
			lv_obj_set_style_border_width(dot, 4, 0);
			lv_obj_set_style_border_color(dot, lv_color_hex(C_TEXT), 0);
		}
		lv_obj_set_ext_click_area(dot, 6);
		lv_obj_add_event_cb(dot, accent_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
	}
}

static void brightness_changed(lv_event_t *e)
{
	lv_obj_t *s = lv_event_get_target(e);

	/* the backlight follows the slider, the value is saved when it is let go */
	ui_brightness_set(lv_slider_get_value(s), lv_event_get_code(e) == LV_EVENT_RELEASED);
}

static void brightness_row(lv_obj_t *list)
{
	lv_obj_t *value;
	lv_obj_t *row = row_create(list, LV_SYMBOL_EYE_OPEN, "Brightness", &value, NULL);
	lv_obj_t *s = lv_slider_create(row);

	lv_obj_add_flag(value, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_width(s, 360);
	lv_slider_set_range(s, 10, 255);
	lv_slider_set_value(s, ui_brightness(), LV_ANIM_OFF);
	lv_obj_set_style_bg_color(s, lv_color_hex(C_SURFACE2), LV_PART_MAIN);
	lv_obj_set_style_bg_color(s, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(s, lv_color_hex(C_ACCENT), LV_PART_KNOB);
	lv_obj_set_style_pad_all(s, 8, LV_PART_KNOB);
	lv_obj_set_ext_click_area(s, 20);
	lv_obj_add_event_cb(s, brightness_changed, LV_EVENT_VALUE_CHANGED, NULL);
	lv_obj_add_event_cb(s, brightness_changed, LV_EVENT_RELEASED, NULL);
}

static lv_obj_t *settings_create(void)
{
	lv_obj_t *scr = ui_screen_new(false);
	lv_obj_t *bar = ui_title_bar(scr, "Settings", back_clicked);
	lv_obj_t *list = lv_obj_create(scr);
	lv_obj_t *about;
	char buf[96];

	ARG_UNUSED(bar);
	lv_obj_remove_style_all(list);
	lv_obj_set_size(list, LV_PCT(100), 768 - 88);
	lv_obj_set_pos(list, 0, 88);
	lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_hor(list, 32, 0);
	lv_obj_set_style_pad_row(list, 12, 0);
	lv_obj_set_scroll_dir(list, LV_DIR_VER);

	row_create(list, LV_SYMBOL_BELL, "Date and time", &time_value, time_clicked);
	row_create(list, LV_SYMBOL_WIFI, "Wi-Fi", &net_value, wifi_clicked);
	row_create(list, LV_SYMBOL_BLUETOOTH, "Bluetooth", &bt_value, NULL);
	row_create(list, LV_SYMBOL_REFRESH, "Set the time from the network", &sync_value,
		   sync_clicked);

	brightness_row(list);
	dark_row(list);
	accent_row(list);

	snprintf(buf, sizeof(buf), "Built %s %s", __DATE__, __TIME__);
	row_create(list, LV_SYMBOL_SETTINGS, "About", &about, NULL);
	lv_label_set_text(about, buf);

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

const struct screen_ops settings_ops = {
	.create = settings_create,
	.enter = settings_enter,
	.leave = settings_leave,
};
