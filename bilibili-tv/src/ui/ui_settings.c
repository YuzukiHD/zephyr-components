/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The settings tab of the home page: the Wi-Fi network (scan, pick one, type the password) on the
 * left, volume and brightness on the right.
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "core/settings.h"
#include "net/wifi.h"
#include "ui/ui_priv.h"

#define KB_H		300
#define AP_SHOWN	10
#define ROW_H		54
#define C_ROW		lv_color_hex(0x26303c)

static lv_obj_t *area, *cur_label, *scan_btn, *scan_label, *ap_box, *pw_box, *pw_title, *pw_ta, *kb;
static lv_obj_t *vol_slider, *vol_value, *bri_slider, *bri_value;
static struct wifi_ap shown[AP_SHOWN];
static int shown_n;
static char selected[33];
static uint32_t ap_sig;
static bool scanned_once, was_scanning, ap_dirty;

static void scan_clicked(lv_event_t *e)
{
	app_cmd(CMD_WIFI_SCAN, 0);
	lv_label_set_text(scan_label, "\xe6\x89\xab\xe6\x8f\x8f\xe4\xb8\xad...");		/* scanning */
}

static void pw_hide(void)
{
	lv_obj_add_flag(pw_box, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

static void pw_connect_clicked(lv_event_t *e)
{
	app_cmd_wifi(selected, lv_textarea_get_text(pw_ta));
	pw_hide();
}

static void pw_cancel_clicked(lv_event_t *e)
{
	pw_hide();
}

static void ap_clicked(lv_event_t *e)
{
	int i = (int)(intptr_t)lv_event_get_user_data(e);

	if (i < 0 || i >= shown_n) {
		return;
	}
	snprintf(selected, sizeof(selected), "%s", shown[i].ssid);
	if (!shown[i].secured) {
		app_cmd_wifi(selected, "");
		return;
	}
	lv_label_set_text(pw_title, selected);
	lv_textarea_set_text(pw_ta, "");
	lv_obj_remove_flag(pw_box, LV_OBJ_FLAG_HIDDEN);
	lv_keyboard_set_textarea(kb, pw_ta);
	lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

static void vol_changed(lv_event_t *e)
{
	int v = lv_slider_get_value(lv_event_get_target(e));
	char t[8];

	snprintf(t, sizeof(t), "%d%%", v);
	lv_label_set_text(vol_value, t);
	settings_set_volume(v);
	if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
		app_cmd(CMD_SAVE_SETTINGS, 0);
	}
}

static void bri_changed(lv_event_t *e)
{
	int v = lv_slider_get_value(lv_event_get_target(e));
	char t[8];

	snprintf(t, sizeof(t), "%d%%", v * 100 / BRIGHTNESS_MAX);
	lv_label_set_text(bri_value, t);
	settings_set_brightness(v);
	if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
		app_cmd(CMD_SAVE_SETTINGS, 0);
	}
}

static lv_obj_t *slider(lv_obj_t *parent, int y, int min, int max, lv_event_cb_t cb)
{
	lv_obj_t *s = lv_slider_create(parent);

	lv_obj_set_size(s, 296, 12);
	lv_obj_set_pos(s, 28, y);
	lv_slider_set_range(s, min, max);
	lv_obj_set_style_bg_color(s, C_ROW, LV_PART_MAIN);
	lv_obj_set_style_bg_color(s, C_ACCENT, LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(s, lv_color_white(), LV_PART_KNOB);
	lv_obj_set_style_pad_all(s, 9, LV_PART_KNOB);
	lv_obj_set_ext_click_area(s, 26);
	lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, NULL);
	lv_obj_add_event_cb(s, cb, LV_EVENT_RELEASED, NULL);

	return s;
}

lv_obj_t *ui_settings_create(lv_obj_t *parent, lv_obj_t *overlay, int top, int height)
{
	lv_obj_t *wifi, *sound, *l, *b;

	area = lv_obj_create(parent);
	lv_obj_set_pos(area, 0, top);
	lv_obj_set_size(area, SCREEN_W, height);
	lv_obj_set_style_bg_opa(area, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(area, 0, 0);
	lv_obj_set_style_pad_all(area, 0, 0);
	lv_obj_remove_flag(area, LV_OBJ_FLAG_SCROLLABLE);

	/* the network */
	wifi = ui_card_panel(area, 24, 12, 620, height - 24);
	ui_text(wifi, ui_font_l, C_TEXT, 28, 22);
	lv_label_set_text(lv_obj_get_child(wifi, -1), "\xe6\x97\xa0\xe7\xba\xbf\xe7\xbd\x91\xe7\xbb\x9c");	/* wireless network */
	cur_label = ui_text(wifi, ui_font_s, C_DIM, 28, 70);
	lv_label_set_long_mode(cur_label, LV_LABEL_LONG_DOT);
	lv_obj_set_width(cur_label, 400);
	scan_btn = ui_button(wifi, "\xe6\x89\xab\xe6\x8f\x8f", scan_clicked, NULL);			/* scan */
	scan_label = lv_obj_get_child(scan_btn, 0);
	ui_button_size(scan_btn, 130, 46);
	lv_obj_set_style_bg_color(scan_btn, C_ACCENT, 0);
	lv_obj_set_pos(scan_btn, 620 - 28 - 130, 24);
	ap_box = lv_obj_create(wifi);
	lv_obj_set_pos(ap_box, 16, 112);
	lv_obj_set_size(ap_box, 588, height - 24 - 112 - 16);
	lv_obj_set_style_bg_opa(ap_box, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(ap_box, 0, 0);
	lv_obj_set_style_pad_all(ap_box, 0, 0);
	lv_obj_set_style_pad_gap(ap_box, 8, 0);
	lv_obj_set_flex_flow(ap_box, LV_FLEX_FLOW_COLUMN);

	/* volume and brightness */
	sound = ui_card_panel(area, 668, 12, 332, height - 24);
	ui_text(sound, ui_font_l, C_TEXT, 28, 22);
	lv_label_set_text(lv_obj_get_child(sound, -1), "\xe5\xa3\xb0\xe9\x9f\xb3\xe4\xb8\x8e\xe6\x98\xbe\xe7\xa4\xba");	/* sound and display */
	l = ui_text(sound, ui_font_m, C_TEXT, 28, 104);
	lv_label_set_text(l, LV_SYMBOL_VOLUME_MAX "  \xe9\x9f\xb3\xe9\x87\x8f");				/* volume */
	vol_value = ui_text(sound, ui_font_m, C_DIM, 240, 104);
	lv_obj_set_width(vol_value, 64);
	lv_obj_set_style_text_align(vol_value, LV_TEXT_ALIGN_RIGHT, 0);
	vol_slider = slider(sound, 160, 0, 100, vol_changed);
	l = ui_text(sound, ui_font_m, C_TEXT, 28, 244);
	lv_label_set_text(l, LV_SYMBOL_EYE_OPEN "  \xe4\xba\xae\xe5\xba\xa6");				/* brightness */
	bri_value = ui_text(sound, ui_font_m, C_DIM, 240, 244);
	lv_obj_set_width(bri_value, 64);
	lv_obj_set_style_text_align(bri_value, LV_TEXT_ALIGN_RIGHT, 0);
	bri_slider = slider(sound, 300, BRIGHTNESS_MIN, BRIGHTNESS_MAX, bri_changed);

	/* the password of a network, over the keyboard */
	pw_box = lv_obj_create(overlay);
	lv_obj_set_size(pw_box, SCREEN_W, 112);
	lv_obj_set_pos(pw_box, 0, SCREEN_H - KB_H - 112);
	lv_obj_set_style_bg_color(pw_box, C_BAR, 0);
	lv_obj_set_style_bg_opa(pw_box, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(pw_box, 0, 0);
	lv_obj_set_style_radius(pw_box, 0, 0);
	lv_obj_set_style_pad_all(pw_box, 0, 0);
	lv_obj_remove_flag(pw_box, LV_OBJ_FLAG_SCROLLABLE);
	pw_title = ui_text(pw_box, ui_font_m, C_ACCENT, MARGIN, 8);
	pw_ta = lv_textarea_create(pw_box);
	lv_textarea_set_one_line(pw_ta, true);
	lv_textarea_set_max_length(pw_ta, 64);
	lv_textarea_set_placeholder_text(pw_ta, "\xe5\xaf\x86\xe7\xa0\x81");			/* password */
	lv_obj_set_style_text_font(pw_ta, ui_font_m, 0);
	lv_obj_set_style_text_color(pw_ta, C_TEXT, 0);
	lv_obj_set_style_bg_color(pw_ta, C_CARD, 0);
	lv_obj_set_style_border_color(pw_ta, C_ACCENT, 0);
	lv_obj_set_size(pw_ta, 640, 52);
	lv_obj_set_pos(pw_ta, MARGIN, 46);
	b = ui_button(pw_box, "\xe8\xbf\x9e\xe6\x8e\xa5", pw_connect_clicked, NULL);		/* connect */
	ui_button_size(b, 140, 48);
	lv_obj_set_style_bg_color(b, C_ACCENT, 0);
	lv_obj_set_pos(b, 680, 48);
	b = ui_button(pw_box, "\xe5\x8f\x96\xe6\xb6\x88", pw_cancel_clicked, NULL);		/* cancel */
	ui_button_size(b, 140, 48);
	lv_obj_set_pos(b, 832, 48);
	lv_obj_add_flag(pw_box, LV_OBJ_FLAG_HIDDEN);

	kb = lv_keyboard_create(overlay);
	lv_obj_set_size(kb, SCREEN_W, KB_H);
	lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
	ui_keyboard_style(kb);
	lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

	return area;
}

/* the buttons of the networks found, the connected one marked */
static void ap_rebuild(void)
{
	static struct wifi_ap aps[AP_SHOWN];
	bool scanning;
	int n = app_wifi_results(aps, AP_SHOWN, &scanning);
	uint32_t sig = n;

	for (int i = 0; i < n; i++) {
		for (const char *p = aps[i].ssid; *p; p++) {
			sig = sig * 31 + (uint8_t)*p;
		}
		sig = sig * 31 + (uint8_t)aps[i].rssi;
	}
	if (sig == ap_sig && !ap_dirty) {
		return;
	}
	ap_sig = sig;
	ap_dirty = false;
	memcpy(shown, aps, sizeof(shown));
	shown_n = n;
	lv_obj_clean(ap_box);
	for (int i = 0; i < n; i++) {
		lv_obj_t *b = lv_button_create(ap_box), *l;
		bool now = strcmp(aps[i].ssid, wifi_current()) == 0;
		char t[48];

		lv_obj_set_size(b, 588, ROW_H);
		lv_obj_set_style_bg_color(b, C_ROW, 0);
		lv_obj_set_style_bg_color(b, C_CARD_PRESS, LV_STATE_PRESSED);
		lv_obj_set_style_shadow_width(b, 0, 0);
		lv_obj_set_style_radius(b, 12, 0);
		l = lv_label_create(b);
		lv_obj_set_style_text_font(l, ui_font_m, 0);
		lv_obj_set_style_text_color(l, now ? C_ACCENT : C_TEXT, 0);
		lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
		lv_obj_set_width(l, 390);
		snprintf(t, sizeof(t), "%s%s", now ? LV_SYMBOL_OK "  " : "", aps[i].ssid);
		lv_label_set_text(l, t);
		lv_obj_align(l, LV_ALIGN_LEFT_MID, 18, 0);
		l = lv_label_create(b);
		lv_obj_set_style_text_font(l, ui_font_s, 0);
		lv_obj_set_style_text_color(l, aps[i].rssi > -60 ? C_ACCENT : C_DIM, 0);
		snprintf(t, sizeof(t), "%s" LV_SYMBOL_WIFI, aps[i].secured ? "\xe5\x8a\xa0\xe5\xaf\x86  " : "");	/* secured */
		lv_label_set_text(l, t);
		lv_obj_align(l, LV_ALIGN_RIGHT_MID, -18, 0);
		lv_obj_add_event_cb(b, ap_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
	}
}

void ui_settings_show(void)
{
	char t[8];

	lv_slider_set_value(vol_slider, settings_volume(), LV_ANIM_OFF);
	snprintf(t, sizeof(t), "%d%%", settings_volume());
	lv_label_set_text(vol_value, t);
	lv_slider_set_value(bri_slider, settings_brightness(), LV_ANIM_OFF);
	snprintf(t, sizeof(t), "%d%%", settings_brightness() * 100 / BRIGHTNESS_MAX);
	lv_label_set_text(bri_value, t);
	if (!scanned_once) {
		scanned_once = true;
		app_cmd(CMD_WIFI_SCAN, 0);
		lv_label_set_text(scan_label, "\xe6\x89\xab\xe6\x8f\x8f\xe4\xb8\xad...");
	}
	ap_dirty = true;
}

void ui_settings_run(bool visible)
{
	bool scanning = false;
	struct wifi_ap none;
	char t[64];

	if (!visible) {
		if (!lv_obj_has_flag(pw_box, LV_OBJ_FLAG_HIDDEN)) {
			pw_hide();	/* the keyboard belongs to this tab */
		}
		return;
	}
	snprintf(t, sizeof(t), "%s%s", "\xe5\xbd\x93\xe5\x89\x8d\xe7\xbd\x91\xe7\xbb\x9c\xef\xbc\x9a",	/* current network: */
		 wifi_current()[0] != '\0' ? wifi_current() : "-");
	if (strcmp(lv_label_get_text(cur_label), t) != 0) {
		lv_label_set_text(cur_label, t);
		ap_dirty = true;
	}
	app_wifi_results(&none, 0, &scanning);
	if (was_scanning && !scanning) {
		lv_label_set_text(scan_label, "\xe6\x89\xab\xe6\x8f\x8f");			/* scan */
		ap_dirty = true;
	}
	was_scanning = scanning;
	ap_rebuild();
}
