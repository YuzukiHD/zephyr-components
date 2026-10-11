/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The Wi-Fi screen: the networks around (scan), tapping one asks for the password with an
 * on-screen keyboard and joins it. The network is saved on the card and joined again at the next
 * start.
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "mp5.h"

#define MAX_ROWS	24
#define ROW_H		72
#define LIST_Y		(88 + 56)

static lv_obj_t *status_label, *list, *scan_btn, *scan_label, *empty_label;
static lv_obj_t *dialog, *dialog_title, *pass_area, *keyboard, *eye_label;
static bool pass_shown;
static lv_timer_t *timer;
static struct net_ap shown[MAX_ROWS];
static int shown_count = -1;
static bool shown_scanning;
static char pending_ssid[33];

static void back_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_goto(SCR_SETTINGS);
}

static void scan_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	app_net_scan();
}

static void dialog_close(void)
{
	lv_obj_add_flag(dialog, LV_OBJ_FLAG_HIDDEN);
	lv_textarea_set_text(pass_area, "");
}

static void keyboard_event(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_READY) {
		app_net_connect(pending_ssid, lv_textarea_get_text(pass_area));
		dialog_close();
	} else if (code == LV_EVENT_CANCEL) {
		dialog_close();
	}
}

static void row_clicked(lv_event_t *e)
{
	const struct net_ap *ap = lv_event_get_user_data(e);

	char saved[65];

	snprintf(pending_ssid, sizeof(pending_ssid), "%s", ap->ssid);
	if (!ap->secured) {
		app_net_connect(ap->ssid, "");
		return;
	}
	/* a network that was joined before does not ask again */
	if (app_net_saved_psk(ap->ssid, saved, sizeof(saved))) {
		app_net_connect(ap->ssid, saved);
		return;
	}
	lv_label_set_text_fmt(dialog_title, "Password of %s", ap->ssid);
	lv_textarea_set_text(pass_area, "");
	pass_shown = false;
	lv_textarea_set_password_mode(pass_area, true);
	lv_label_set_text(eye_label, LV_SYMBOL_EYE_CLOSE);
	lv_obj_remove_flag(dialog, LV_OBJ_FLAG_HIDDEN);
}

static void eye_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	pass_shown = !pass_shown;
	lv_textarea_set_password_mode(pass_area, !pass_shown);
	lv_label_set_text(eye_label, pass_shown ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
}

static void disconnect_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	app_net_disconnect();
}

static const char *bars(int rssi)
{
	return rssi >= -55 ? LV_SYMBOL_WIFI "  strong" : rssi >= -70 ? LV_SYMBOL_WIFI "  good" :
		LV_SYMBOL_WIFI "  weak";
}

static void rows_rebuild(int n)
{
	lv_obj_clean(list);
	for (int i = 0; i < n; i++) {
		lv_obj_t *row = lv_button_create(list);
		lv_obj_t *name = lv_label_create(row);
		lv_obj_t *info = lv_label_create(row);

		lv_obj_remove_style_all(row);
		lv_obj_set_size(row, 960, ROW_H - 8);
		lv_obj_set_style_radius(row, 18, 0);
		lv_obj_set_style_bg_color(row, lv_color_hex(C_SURFACE), 0);
		lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(row, lv_color_hex(C_SURFACE2), LV_STATE_PRESSED);
		lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
				      LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_hor(row, 24, 0);
		lv_obj_set_style_pad_column(row, 20, 0);
		lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
		lv_obj_add_event_cb(row, row_clicked, LV_EVENT_CLICKED, &shown[i]);

		lv_obj_set_style_text_font(name, &lv_font_montserrat_24, 0);
		lv_obj_set_style_text_color(name, lv_color_hex(C_TEXT), 0);
		lv_obj_set_flex_grow(name, 1);
		lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
		lv_label_set_text_fmt(name, "%s%s", shown[i].secured ? LV_SYMBOL_EYE_CLOSE "  " : "",
				      shown[i].ssid);
		lv_obj_set_style_text_font(info, &lv_font_montserrat_20, 0);
		lv_obj_set_style_text_color(info, lv_color_hex(C_DIM), 0);
		lv_label_set_text(info, bars(shown[i].rssi));
	}
}

static void refresh(lv_timer_t *t)
{
	struct net_ap now[MAX_ROWS];
	char buf[96];
	int n;
	bool scanning = app_net_scanning();

	ARG_UNUSED(t);
	app_net_status(buf, sizeof(buf));
	lv_label_set_text(status_label, buf);
	n = app_net_scan_results(now, MAX_ROWS);
	/* the list is made again when the networks changed, not at every tick */
	if (n != shown_count || (n > 0 && (strcmp(now[0].ssid, shown[0].ssid) != 0 ||
					   strcmp(now[n - 1].ssid, shown[n - 1].ssid) != 0))) {
		memcpy(shown, now, n * sizeof(*now));
		shown_count = n;
		rows_rebuild(n);
	}
	if (scanning != shown_scanning) {
		shown_scanning = scanning;
		lv_label_set_text(scan_label, scanning ? "Scanning" : LV_SYMBOL_REFRESH "  Scan");
	}
	if (n == 0 && !scanning) {
		lv_label_set_text(empty_label, app_net_available() ? "No networks found, tap Scan" :
							      "No Wi-Fi module found");
		lv_obj_remove_flag(empty_label, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(empty_label, LV_OBJ_FLAG_HIDDEN);
	}
}

static lv_obj_t *pill_button(lv_obj_t *parent, const char *text, lv_obj_t **label, lv_event_cb_t cb)
{
	lv_obj_t *b = lv_button_create(parent);
	lv_obj_t *l = lv_label_create(b);

	lv_obj_remove_style_all(b);
	lv_obj_set_size(b, 170, 56);
	lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(b, lv_color_hex(C_ACCENT), 0);
	lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(b, lv_color_hex(C_ACCENT2), LV_STATE_PRESSED);
	lv_obj_set_style_text_color(l, lv_color_white(), 0);
	lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
	lv_label_set_text(l, text);
	lv_obj_center(l);
	lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
	if (label != NULL) {
		*label = l;
	}

	return b;
}

static lv_obj_t *wifi_create(void)
{
	lv_obj_t *scr = ui_screen_new(false);
	lv_obj_t *bar = ui_title_bar(scr, "Wi-Fi", back_clicked);

	pill_button(bar, LV_SYMBOL_CLOSE "  Forget", NULL, disconnect_clicked);
	scan_btn = pill_button(bar, LV_SYMBOL_REFRESH "  Scan", &scan_label, scan_clicked);
	lv_obj_set_flex_grow(lv_obj_get_child(bar, 1), 1);

	status_label = lv_label_create(scr);
	lv_obj_set_pos(status_label, 40, 92);
	lv_obj_set_width(status_label, 944);
	lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);
	lv_obj_set_style_text_font(status_label, &lv_font_montserrat_20, 0);
	lv_obj_set_style_text_color(status_label, lv_color_hex(C_ACCENT), 0);

	list = lv_obj_create(scr);
	lv_obj_remove_style_all(list);
	lv_obj_set_size(list, LV_PCT(100), 768 - LIST_Y);
	lv_obj_set_pos(list, 0, LIST_Y);
	lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_hor(list, 32, 0);
	lv_obj_set_scroll_dir(list, LV_DIR_VER);

	empty_label = lv_label_create(scr);
	lv_obj_set_style_text_font(empty_label, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(empty_label, lv_color_hex(C_DIM), 0);
	lv_obj_align(empty_label, LV_ALIGN_CENTER, 0, -40);

	/* the password dialog covers the screen */
	dialog = lv_obj_create(scr);
	lv_obj_remove_style_all(dialog);
	lv_obj_set_size(dialog, LV_PCT(100), LV_PCT(100));
	lv_obj_set_style_bg_color(dialog, lv_color_hex(C_BG), 0);
	lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, 0);
	lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
	dialog_title = lv_label_create(dialog);
	lv_obj_set_style_text_font(dialog_title, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(dialog_title, lv_color_hex(C_TEXT), 0);
	lv_obj_set_pos(dialog_title, 48, 40);
	pass_area = lv_textarea_create(dialog);
	lv_obj_set_size(pass_area, 840, 72);
	lv_obj_set_pos(pass_area, 48, 110);
	lv_textarea_set_one_line(pass_area, true);
	/* shown as dots until the eye is tapped */
	lv_textarea_set_password_mode(pass_area, true);
	lv_textarea_set_placeholder_text(pass_area, "Password");
	lv_obj_set_style_text_font(pass_area, &lv_font_montserrat_24, 0);
	lv_obj_set_style_bg_color(pass_area, lv_color_hex(C_SURFACE), 0);
	lv_obj_set_style_bg_opa(pass_area, LV_OPA_COVER, 0);
	lv_obj_set_style_text_color(pass_area, lv_color_hex(C_TEXT), 0);
	lv_obj_set_style_text_color(pass_area, lv_color_hex(C_DIM), LV_PART_TEXTAREA_PLACEHOLDER);
	lv_obj_set_style_border_color(pass_area, lv_color_hex(C_BORDER), 0);
	lv_obj_set_style_border_color(pass_area, lv_color_hex(C_ACCENT), LV_STATE_FOCUSED);
	lv_obj_set_style_border_width(pass_area, 2, 0);
	lv_obj_set_style_radius(pass_area, 16, 0);
	lv_obj_set_style_bg_color(pass_area, lv_color_hex(C_ACCENT), LV_PART_CURSOR);
	lv_obj_set_style_bg_opa(pass_area, LV_OPA_COVER, LV_PART_CURSOR);
	{
		lv_obj_t *eye = ui_icon_button(dialog, LV_SYMBOL_EYE_CLOSE, 72);

		lv_obj_set_pos(eye, 904, 110);
		eye_label = lv_obj_get_child(eye, 0);
		lv_obj_add_event_cb(eye, eye_clicked, LV_EVENT_CLICKED, NULL);
	}
	keyboard = lv_keyboard_create(dialog);
	lv_obj_set_size(keyboard, LV_PCT(100), 400);
	lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_keyboard_set_textarea(keyboard, pass_area);
	lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_24, 0);
	lv_obj_add_event_cb(keyboard, keyboard_event, LV_EVENT_ALL, NULL);
	lv_obj_add_flag(dialog, LV_OBJ_FLAG_HIDDEN);

	return scr;
}

static void wifi_enter(void *arg)
{
	ARG_UNUSED(arg);
	shown_count = -1;
	shown_scanning = false;
	if (timer == NULL) {
		timer = lv_timer_create(refresh, 500, NULL);
	}
	lv_timer_resume(timer);
	dialog_close();
	refresh(NULL);
	if (!app_net_connected()) {
		app_net_scan();
	}
}

static void wifi_leave(void)
{
	if (timer != NULL) {
		lv_timer_pause(timer);
	}
}

const struct screen_ops wifi_ops = {
	.create = wifi_create,
	.enter = wifi_enter,
	.leave = wifi_leave,
};
