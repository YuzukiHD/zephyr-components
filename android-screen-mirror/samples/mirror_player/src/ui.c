/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The status text and three navigation keys (back, home, recent apps) as LVGL objects on the ARGB
 * plane over the video, and the relay of touches: the fingers are read from the input events, one
 * per slot, and sent to the phone as touch events at the position inside the picture. A finger
 * that starts on a navigation key presses the key and is not sent as touch.
 */

#include <string.h>
#include <lvgl.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "app.h"

#define SLOTS		5
#define NKEYS		3
#define KEY_SIZE	70
#define KEY_GAP		10
/* the controller sometimes leaves out a frame while the finger stays on the glass */
#define RELEASE_HOLD_MS	50

static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *const touch_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_touch));

static struct {
	int x, y;
	bool down;
} fingers[SLOTS];
static int cur_slot;
static K_SEM_DEFINE(touch_wake, 0, 1);

static struct scrcpy *volatile session;
static volatile uint16_t video_w, video_h;
static volatile uint8_t keys_pressed;
static volatile bool menu_open;
static volatile enum ui_action pending_action;

static char status_text[64];
static volatile bool status_dirty;

static char qr_payload[160];
static volatile int qr_request;	/* 1 show, 2 hide */

static void touch_event(struct input_event *evt, void *user_data)
{
	switch (evt->code) {
	case INPUT_ABS_MT_SLOT:
		cur_slot = CLAMP(evt->value, 0, SLOTS - 1);
		break;
	case INPUT_ABS_X:
		fingers[cur_slot].x = evt->value;
		break;
	case INPUT_ABS_Y:
		fingers[cur_slot].y = evt->value;
		break;
	case INPUT_BTN_TOUCH:
		fingers[cur_slot].down = evt->value != 0;
		k_sem_give(&touch_wake);
		break;
	}
	if (evt->sync) {
		k_sem_give(&touch_wake);
	}
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_CHOSEN(zephyr_touch)), touch_event, NULL);

void ui_set_status(const char *text)
{
	strncpy(status_text, text, sizeof(status_text) - 1U);
	status_dirty = true;
}

void ui_show_qr(const char *payload)
{
	strncpy(qr_payload, payload, sizeof(qr_payload) - 1);
	qr_request = 1;
}

void ui_hide_qr(void)
{
	qr_request = 2;
}

void ui_set_session(struct scrcpy *s)
{
	session = s;
}

void ui_set_video_size(uint16_t w, uint16_t h)
{
	video_w = w;
	video_h = h;
}

/* ---- look ------------------------------------------------------------------------------- */

/*
 * Text is drawn on the ARGB plane over the video, and LVGL draws text on a transparent
 * background as solid boxes: every object that carries text has an opaque background.
 */
#define C_BG		lv_color_make(238, 242, 247)
#define C_TEXT		lv_color_make(31, 42, 60)
#define C_MUTED		lv_color_make(107, 122, 144)
#define C_ACCENT	lv_color_make(47, 107, 255)
#define C_ACCENT_SOFT	lv_color_make(225, 234, 255)
#define C_LINE		lv_color_make(208, 216, 228)

#define FONT_S		(&lv_font_montserrat_16)
#define FONT_M		(&lv_font_montserrat_20)
#define FONT_L		(&lv_font_montserrat_28)

static lv_obj_t *label_make(lv_obj_t *parent, const char *text, const lv_font_t *font,
			    lv_color_t color, int x, int y)
{
	lv_obj_t *l = lv_label_create(parent);

	lv_obj_set_style_text_font(l, font, 0);
	lv_obj_set_style_text_color(l, color, 0);
	lv_label_set_text(l, text);
	lv_obj_set_pos(l, x, y);

	return l;
}

/* A rounded button with a centered label; the label is returned in @p out_label */
static lv_obj_t *pill_make(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
			   lv_color_t bg, lv_color_t fg, lv_obj_t **out_label)
{
	lv_obj_t *b = lv_button_create(parent);
	lv_obj_t *l = lv_label_create(b);

	lv_obj_set_size(b, w, h);
	lv_obj_set_pos(b, x, y);
	lv_obj_set_style_radius(b, h / 2, 0);
	lv_obj_set_style_bg_color(b, bg, 0);
	lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
	lv_obj_set_style_shadow_width(b, 0, 0);
	lv_obj_set_style_border_width(b, 0, 0);
	lv_obj_set_style_text_font(l, FONT_M, 0);
	lv_obj_set_style_text_color(l, fg, 0);
	lv_label_set_text(l, text);
	lv_obj_center(l);
	if (out_label != NULL) {
		*out_label = l;
	}

	return b;
}

/* ---- WiFi setup form -------------------------------------------------------------------- */

static char form_ssid[33], form_psk[65], form_msg[96];
static volatile int wifi_request;
static K_SEM_DEFINE(wifi_done, 0, 1);

int ui_wifi_prompt(char *ssid, size_t ssid_size, char *psk, size_t psk_size, const char *message)
{
	strncpy(form_ssid, ssid, sizeof(form_ssid) - 1);
	strncpy(form_psk, psk, sizeof(form_psk) - 1);
	strncpy(form_msg, message, sizeof(form_msg) - 1);
	wifi_request = 1;
	k_sem_take(&wifi_done, K_FOREVER);
	strncpy(ssid, form_ssid, ssid_size - 1);
	ssid[ssid_size - 1] = '\0';
	strncpy(psk, form_psk, psk_size - 1);
	psk[psk_size - 1] = '\0';

	return 0;
}

static lv_obj_t *form, *form_note, *dd_net, *lbl_scan, *ta_ssid, *ta_psk, *kb;

#define NET_OTHER	"Other network..."
#define NET_HINT	"Select a network"

static void (*scan_start)(void);
static char net_options[1024];
static volatile bool net_dirty;

void ui_wifi_set_scanner(void (*start)(void))
{
	scan_start = start;
}

void ui_wifi_networks(const char *options)
{
	strncpy(net_options, options, sizeof(net_options) - 1);
	net_dirty = true;
}

static void form_focus(lv_obj_t *ta)
{
	lv_obj_remove_state(ta_ssid, LV_STATE_FOCUSED);
	lv_obj_remove_state(ta_psk, LV_STATE_FOCUSED);
	lv_obj_add_state(ta, LV_STATE_FOCUSED);
	lv_keyboard_set_textarea(kb, ta);
}

/* The name comes from the list, or is typed in place of the list ("Other network...") */
static void form_name_typed(bool typed)
{
	if (typed) {
		lv_obj_add_flag(dd_net, LV_OBJ_FLAG_HIDDEN);
		lv_obj_remove_flag(ta_ssid, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_remove_flag(dd_net, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(ta_ssid, LV_OBJ_FLAG_HIDDEN);
	}
}

static void form_ta_event(lv_event_t *e)
{
	form_focus(lv_event_get_target(e));
}

static void form_submit(void)
{
	const char *ssid = lv_textarea_get_text(ta_ssid);

	if (ssid[0] == '\0') {
		lv_label_set_text(form_note, "Choose a network first");
		return;
	}
	strncpy(form_ssid, ssid, sizeof(form_ssid) - 1);
	strncpy(form_psk, lv_textarea_get_text(ta_psk), sizeof(form_psk) - 1);
	lv_obj_add_flag(form, LV_OBJ_FLAG_HIDDEN);
	k_sem_give(&wifi_done);
}

static void form_connect_event(lv_event_t *e)
{
	form_submit();
}

static void form_kb_event(lv_event_t *e)
{
	if (lv_event_get_code(e) == LV_EVENT_READY) {
		form_submit();
	}
}

static void form_show_event(lv_event_t *e)
{
	bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);

	lv_textarea_set_password_mode(ta_psk, !on);
}

static void form_scan_event(lv_event_t *e)
{
	form_name_typed(false);
	if (scan_start != NULL) {
		lv_label_set_text(lbl_scan, LV_SYMBOL_REFRESH "  Scanning");
		scan_start();
	}
}

static void form_dd_event(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_READY) {
		/* the list is made when it opens */
		lv_obj_t *list = lv_dropdown_get_list(dd_net);

		if (list != NULL) {
			lv_obj_set_style_text_font(list, FONT_M, 0);
			lv_obj_set_style_text_color(list, C_TEXT, 0);
			lv_obj_set_style_bg_color(list, lv_color_white(), 0);
			lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
			lv_obj_set_style_radius(list, 14, 0);
			lv_obj_set_style_border_width(list, 2, 0);
			lv_obj_set_style_border_color(list, C_LINE, 0);
			lv_obj_set_style_max_height(list, 240, 0);
			lv_obj_set_style_bg_color(list, C_ACCENT_SOFT,
						  LV_PART_SELECTED | LV_STATE_CHECKED);
			lv_obj_set_style_text_color(list, C_ACCENT, LV_PART_SELECTED | LV_STATE_CHECKED);
		}
	} else if (code == LV_EVENT_VALUE_CHANGED) {
		char sel[64];

		/* show what was chosen */
		lv_dropdown_set_text(dd_net, NULL);
		lv_dropdown_get_selected_str(dd_net, sel, sizeof(sel));
		if (strcmp(sel, NET_OTHER) == 0) {
			form_name_typed(true);
			lv_textarea_set_text(ta_ssid, "");
			form_focus(ta_ssid);
		} else {
			lv_textarea_set_text(ta_ssid, sel);
			form_focus(ta_psk);
		}
	}
}

static lv_obj_t *form_textarea(lv_obj_t *parent, int x, int y, int w, int max_len, bool password)
{
	lv_obj_t *ta = lv_textarea_create(parent);

	lv_obj_set_size(ta, w, 52);
	lv_obj_set_pos(ta, x, y);
	lv_textarea_set_one_line(ta, true);
	lv_textarea_set_max_length(ta, max_len);
	lv_textarea_set_password_mode(ta, password);
	lv_obj_set_style_text_font(ta, FONT_M, 0);
	lv_obj_set_style_text_color(ta, C_TEXT, 0);
	lv_obj_set_style_bg_color(ta, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(ta, 14, 0);
	lv_obj_set_style_border_width(ta, 2, 0);
	lv_obj_set_style_border_color(ta, C_LINE, 0);
	lv_obj_set_style_border_color(ta, C_ACCENT, LV_STATE_FOCUSED);
	lv_obj_set_style_pad_left(ta, 16, 0);
	lv_obj_set_style_pad_top(ta, 12, 0);
	lv_obj_set_style_shadow_width(ta, 0, 0);
	lv_obj_add_event_cb(ta, form_ta_event, LV_EVENT_CLICKED, NULL);

	return ta;
}

static void form_build(int pw, int ph)
{
	const int kb_h = 320, pad = 28;
	const int card_w = MIN(pw - 48, 720), card_h = 360;
	const int iw = card_w - 2 * pad;
	lv_obj_t *card, *sw;

	form = lv_obj_create(lv_screen_active());
	lv_obj_set_size(form, pw, ph);
	lv_obj_set_pos(form, 0, 0);
	lv_obj_set_style_bg_color(form, C_BG, 0);
	lv_obj_set_style_bg_opa(form, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(form, 0, 0);
	lv_obj_set_style_border_width(form, 0, 0);
	lv_obj_set_style_pad_all(form, 0, 0);
	lv_obj_remove_flag(form, LV_OBJ_FLAG_SCROLLABLE);

	card = lv_obj_create(form);
	lv_obj_set_size(card, card_w, card_h);
	lv_obj_set_pos(card, (pw - card_w) / 2, (ph - kb_h - card_h) / 2);
	lv_obj_set_style_bg_color(card, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(card, 24, 0);
	lv_obj_set_style_border_width(card, 0, 0);
	lv_obj_set_style_pad_all(card, 0, 0);
	lv_obj_set_style_shadow_width(card, 30, 0);
	lv_obj_set_style_shadow_color(card, lv_color_make(120, 140, 170), 0);
	lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
	lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

	label_make(card, LV_SYMBOL_WIFI "  Connect to WiFi", FONT_L, C_TEXT, pad, 24);
	pill_make(card, "Connect", card_w - pad - 170, 20, 170, 52, C_ACCENT, lv_color_white(), NULL);
	lv_obj_add_event_cb(lv_obj_get_child(card, -1), form_connect_event, LV_EVENT_CLICKED, NULL);
	form_note = label_make(card, "", FONT_S, C_MUTED, pad, 84);
	lv_obj_set_width(form_note, iw);

	label_make(card, "Network", FONT_S, C_MUTED, pad, 122);
	dd_net = lv_dropdown_create(card);
	lv_obj_set_size(dd_net, iw - 176, 52);
	lv_obj_set_pos(dd_net, pad, 146);
	lv_obj_set_style_text_font(dd_net, FONT_M, 0);
	lv_obj_set_style_text_color(dd_net, C_TEXT, 0);
	lv_obj_set_style_bg_color(dd_net, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(dd_net, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(dd_net, 14, 0);
	lv_obj_set_style_border_width(dd_net, 2, 0);
	lv_obj_set_style_border_color(dd_net, C_LINE, 0);
	lv_obj_set_style_pad_left(dd_net, 16, 0);
	lv_obj_set_style_pad_top(dd_net, 12, 0);
	lv_obj_set_style_shadow_width(dd_net, 0, 0);
	lv_dropdown_set_options(dd_net, NET_OTHER);
	lv_dropdown_set_text(dd_net, NET_HINT);
	lv_obj_add_event_cb(dd_net, form_dd_event, LV_EVENT_ALL, NULL);

	/* typed name: takes the place of the list */
	ta_ssid = form_textarea(card, pad, 146, iw - 176, 32, false);
	lv_obj_add_flag(ta_ssid, LV_OBJ_FLAG_HIDDEN);

	pill_make(card, LV_SYMBOL_REFRESH "  Scan", card_w - pad - 160, 146, 160, 52, C_ACCENT_SOFT,
		  C_ACCENT, &lbl_scan);
	lv_obj_add_event_cb(lv_obj_get_child(card, -1), form_scan_event, LV_EVENT_CLICKED, NULL);

	label_make(card, "Password", FONT_S, C_MUTED, pad, 222);
	ta_psk = form_textarea(card, pad, 246, iw - 176, 64, true);

	sw = lv_switch_create(card);
	lv_obj_set_pos(sw, card_w - pad - 150, 256);
	lv_obj_set_style_bg_color(sw, C_LINE, 0);
	lv_obj_set_style_bg_color(sw, C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
	lv_obj_add_event_cb(sw, form_show_event, LV_EVENT_VALUE_CHANGED, NULL);
	label_make(card, "Show", FONT_M, C_TEXT, card_w - pad - 70, 258);

	kb = lv_keyboard_create(form);
	lv_obj_set_size(kb, pw, kb_h);
	lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_obj_set_style_bg_color(kb, lv_color_make(214, 220, 232), 0);
	lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(kb, 0, 0);
	lv_obj_set_style_border_width(kb, 0, 0);
	lv_obj_set_style_pad_all(kb, 10, 0);
	lv_obj_set_style_pad_gap(kb, 8, 0);
	lv_obj_set_style_text_font(kb, FONT_M, LV_PART_ITEMS);
	lv_obj_set_style_text_color(kb, C_TEXT, LV_PART_ITEMS);
	lv_obj_set_style_bg_color(kb, lv_color_white(), LV_PART_ITEMS);
	lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
	lv_obj_set_style_radius(kb, 10, LV_PART_ITEMS);
	lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
	lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
	lv_obj_set_style_bg_color(kb, C_ACCENT_SOFT, LV_PART_ITEMS | LV_STATE_PRESSED);
	lv_obj_set_style_bg_color(kb, C_ACCENT_SOFT, LV_PART_ITEMS | LV_STATE_CHECKED);
	lv_obj_add_event_cb(kb, form_kb_event, LV_EVENT_ALL, NULL);
	lv_keyboard_set_textarea(kb, ta_psk);
}

static void form_open(void)
{
	if (form == NULL) {
		form_build(lv_display_get_horizontal_resolution(NULL),
			   lv_display_get_vertical_resolution(NULL));
	}
	lv_textarea_set_text(ta_ssid, form_ssid);
	lv_textarea_set_text(ta_psk, form_psk);
	lv_dropdown_set_text(dd_net, form_ssid[0] != '\0' ? form_ssid : NET_HINT);
	form_name_typed(false);
	if (scan_start != NULL) {
		lv_label_set_text(lbl_scan, LV_SYMBOL_REFRESH "  Scanning");
		scan_start();
	}
	lv_label_set_text(form_note, form_msg);
	lv_obj_remove_flag(form, LV_OBJ_FLAG_HIDDEN);
	form_focus(ta_psk);
}

/* ---- settings menu ---------------------------------------------------------------------- */

static lv_obj_t *menu;

enum ui_action ui_take_action(void)
{
	enum ui_action a = pending_action;

	pending_action = UI_ACT_NONE;

	return a;
}

bool ui_action_pending(void)
{
	return pending_action != UI_ACT_NONE;
}

static void menu_event(lv_event_t *e)
{
	enum ui_action a = (enum ui_action)(uintptr_t)lv_event_get_user_data(e);
	struct scrcpy *s = session;

	lv_obj_add_flag(menu, LV_OBJ_FLAG_HIDDEN);
	menu_open = false;
	if (a != UI_ACT_NONE) {
		pending_action = a;
		if (s != NULL) {
			/* the reader of the video returns and the mirror ends */
			scrcpy_abort(s);
		}
	}
}

static void menu_build(int pw, int ph)
{
	static const struct {
		const char *text;
		enum ui_action action;
		bool primary;
	} items[] = {
		{LV_SYMBOL_WIFI "  Change WiFi network", UI_ACT_CHANGE_WIFI, true},
		{LV_SYMBOL_PLUS "  Pair a new phone", UI_ACT_PAIR, false},
		{"Close", UI_ACT_NONE, false},
	};
	const int w = 460, h = 380;
	lv_obj_t *l;

	menu = lv_obj_create(lv_screen_active());
	lv_obj_set_size(menu, w, h);
	lv_obj_set_pos(menu, (pw - w) / 2, (ph - h) / 2);
	lv_obj_set_style_bg_color(menu, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(menu, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(menu, 28, 0);
	lv_obj_set_style_border_width(menu, 2, 0);
	lv_obj_set_style_border_color(menu, C_LINE, 0);
	lv_obj_set_style_pad_all(menu, 0, 0);
	lv_obj_remove_flag(menu, LV_OBJ_FLAG_SCROLLABLE);

	l = label_make(menu, "Settings", FONT_L, C_TEXT, 0, 28);
	lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 28);
	for (int i = 0; i < 3; i++) {
		lv_obj_t *b = pill_make(menu, items[i].text, (w - 380) / 2, 100 + i * 80, 380, 60,
					items[i].primary ? C_ACCENT : (items[i].action == UI_ACT_NONE
									   ? lv_color_make(232, 236, 243)
									   : C_ACCENT_SOFT),
					items[i].primary ? lv_color_white()
							 : (items[i].action == UI_ACT_NONE ? C_MUTED
											    : C_ACCENT),
					NULL);

		lv_obj_add_event_cb(b, menu_event, LV_EVENT_CLICKED,
				    (void *)(uintptr_t)items[i].action);
	}
	lv_obj_add_flag(menu, LV_OBJ_FLAG_HIDDEN);
}

static void gear_event(lv_event_t *e)
{
	if (menu == NULL) {
		menu_build(lv_display_get_horizontal_resolution(NULL),
			   lv_display_get_vertical_resolution(NULL));
	}
	lv_obj_remove_flag(menu, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(menu);
	menu_open = true;
}

/* ---- geometry ------------------------------------------------------------------------- */

struct geom {
	int panel_w, panel_h;
	/* the picture on the screen */
	int x, y, w, h;
};

static void key_rect(const struct geom *g, int k, int *x, int *y)
{
	*x = g->panel_w - KEY_SIZE - KEY_GAP;
	*y = g->panel_h / 2 - (NKEYS * (KEY_SIZE + KEY_GAP)) / 2 + k * (KEY_SIZE + KEY_GAP);
}

/* The settings key sits in the top right corner; it is handled by LVGL, the relay only keeps the
 * touch from reaching the phone */
static void gear_rect(const struct geom *g, int *x, int *y)
{
	*x = g->panel_w - KEY_SIZE - KEY_GAP;
	*y = KEY_GAP;
}

static int key_hit(const struct geom *g, int px, int py)
{
	int gx, gy;

	gear_rect(g, &gx, &gy);
	if (px >= gx - KEY_GAP / 2 && px < gx + KEY_SIZE + KEY_GAP / 2 && py >= gy - KEY_GAP / 2 &&
	    py < gy + KEY_SIZE + KEY_GAP / 2) {
		return NKEYS;
	}
	for (int k = 0; k < NKEYS; k++) {
		int x, y;

		key_rect(g, k, &x, &y);
		if (px >= x - KEY_GAP / 2 && px < x + KEY_SIZE + KEY_GAP / 2 &&
		    py >= y - KEY_GAP / 2 && py < y + KEY_SIZE + KEY_GAP / 2) {
			return k;
		}
	}

	return -1;
}

/* the same fit as the display driver: the largest size inside the panel with the aspect kept */
static void picture_rect(struct geom *g, uint16_t vw, uint16_t vh)
{
	if ((uint64_t)g->panel_w * vh <= (uint64_t)g->panel_h * vw) {
		g->w = g->panel_w;
		g->h = (uint64_t)vh * g->panel_w / vw;
	} else {
		g->h = g->panel_h;
		g->w = (uint64_t)vw * g->panel_h / vh;
	}
	g->w &= ~1;
	g->h &= ~1;
	g->x = (g->panel_w - g->w) / 2;
	g->y = (g->panel_h - g->h) / 2;
}

/* ---- touch relay ---------------------------------------------------------------------- */

enum owner { OWNER_NONE, OWNER_TOUCH, OWNER_KEY };

static const uint32_t key_codes[NKEYS] = {SCRCPY_KEY_BACK, SCRCPY_KEY_HOME, SCRCPY_KEY_APP_SWITCH};

K_THREAD_STACK_DEFINE(relay_stack, 4096);
static struct k_thread relay_thread;

static void relay_main(void *a, void *b, void *c)
{
	enum owner owner[SLOTS] = {0};
	int key_of[SLOTS] = {0};
	uint32_t last_seen[SLOTS] = {0};
	int last_x[SLOTS] = {0}, last_y[SLOTS] = {0};
	int sent_x[SLOTS] = {0}, sent_y[SLOTS] = {0};
	struct display_capabilities caps;
	struct geom g;

	display_get_capabilities(disp, &caps);
	g.panel_w = caps.x_resolution;
	g.panel_h = caps.y_resolution;

	while (true) {
		struct scrcpy *s;
		uint16_t vw, vh;
		struct scrcpy_touch ev[SLOTS];
		unsigned int n = 0;
		bool active = false;
		uint32_t now;

		for (int i = 0; i < SLOTS; i++) {
			active |= owner[i] != OWNER_NONE;
		}
		k_sem_take(&touch_wake, active ? K_MSEC(8) : K_FOREVER);

		s = session;
		vw = video_w;
		vh = video_h;
		if (s == NULL || vw == 0U || vh == 0U) {
			memset(owner, 0, sizeof(owner));
			keys_pressed = 0;
			continue;
		}
		picture_rect(&g, vw, vh);
		now = k_uptime_get_32();

		for (int i = 0; i < SLOTS; i++) {
			int fx = fingers[i].x, fy = fingers[i].y;
			bool down = fingers[i].down;
			bool held;

			if (down) {
				last_seen[i] = now;
				last_x[i] = fx;
				last_y[i] = fy;
			}
			held = down || (int32_t)(now - last_seen[i]) < RELEASE_HOLD_MS;

			switch (owner[i]) {
			case OWNER_NONE:
				if (down && !menu_open) {
					int k = key_hit(&g, fx, fy);

					if (k >= 0) {
						owner[i] = OWNER_KEY;
						key_of[i] = k;
						if (k < NKEYS) {
							keys_pressed |= BIT(k);
							scrcpy_send_key(s, true, key_codes[k]);
						}
					} else if (fx >= g.x && fx < g.x + g.w && fy >= g.y &&
						   fy < g.y + g.h) {
						owner[i] = OWNER_TOUCH;
						sent_x[i] = (fx - g.x) * vw / g.w;
						sent_y[i] = (fy - g.y) * vh / g.h;
						ev[n++] = (struct scrcpy_touch){
							SCRCPY_TOUCH_DOWN, i, sent_x[i], sent_y[i]};
					}
				}
				break;
			case OWNER_TOUCH:
				if (held) {
					int x = CLAMP(last_x[i] - g.x, 0, g.w - 1) * vw / g.w;
					int y = CLAMP(last_y[i] - g.y, 0, g.h - 1) * vh / g.h;

					if (x != sent_x[i] || y != sent_y[i]) {
						sent_x[i] = x;
						sent_y[i] = y;
						ev[n++] = (struct scrcpy_touch){
							SCRCPY_TOUCH_MOVE, i, x, y};
					}
				} else {
					owner[i] = OWNER_NONE;
					ev[n++] = (struct scrcpy_touch){
						SCRCPY_TOUCH_UP, i, sent_x[i], sent_y[i]};
				}
				break;
			case OWNER_KEY:
				if (!held) {
					owner[i] = OWNER_NONE;
					if (key_of[i] < NKEYS) {
						keys_pressed &= ~BIT(key_of[i]);
						scrcpy_send_key(s, false, key_codes[key_of[i]]);
					}
				}
				break;
			}
		}
		if (n > 0U) {
			scrcpy_send_touch(s, ev, n, vw, vh);
			k_msleep(8);
		}
	}
}

/* ---- LVGL ----------------------------------------------------------------------------- */

K_THREAD_STACK_DEFINE(ui_stack, 16384);
static struct k_thread ui_thread;

static lv_obj_t *key_obj[NKEYS];

static void ui_main(void *a, void *b, void *c)
{
	static const char *const symbols[NKEYS] = {LV_SYMBOL_LEFT, LV_SYMBOL_HOME, LV_SYMBOL_LIST};
	struct geom g = {
		.panel_w = lv_display_get_horizontal_resolution(NULL),
		.panel_h = lv_display_get_vertical_resolution(NULL),
	};
	lv_obj_t *label, *qr_box = NULL, *qr = NULL, *qr_status = NULL;
	bool qr_shown = false;
	uint8_t shown = 0;

	lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_TRANSP, 0);
	label = lv_label_create(lv_screen_active());
	lv_obj_set_style_text_font(label, FONT_M, 0);
	lv_obj_set_style_text_color(label, C_TEXT, 0);
	lv_obj_align(label, LV_ALIGN_TOP_LEFT, 16, 14);
	/* text needs an opaque background: on the transparent ARGB layer it turns into boxes */
	lv_obj_set_style_bg_color(label, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_hor(label, 20, 0);
	lv_obj_set_style_pad_ver(label, 10, 0);
	lv_obj_set_style_radius(label, 22, 0);
	lv_obj_set_style_border_width(label, 2, 0);
	lv_obj_set_style_border_color(label, C_LINE, 0);
	lv_label_set_text(label, "");
	lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);


	for (int k = 0; k < NKEYS; k++) {
		int x, y;
		lv_obj_t *l;

		key_rect(&g, k, &x, &y);
		key_obj[k] = lv_button_create(lv_screen_active());
		lv_obj_set_size(key_obj[k], KEY_SIZE, KEY_SIZE);
		lv_obj_align(key_obj[k], LV_ALIGN_TOP_LEFT, x, y);
		lv_obj_set_style_radius(key_obj[k], LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_bg_color(key_obj[k], lv_color_make(250, 251, 253), 0);
		lv_obj_set_style_bg_opa(key_obj[k], LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(key_obj[k], C_ACCENT, LV_STATE_PRESSED);
		lv_obj_set_style_bg_opa(key_obj[k], LV_OPA_COVER, LV_STATE_PRESSED);
		lv_obj_set_style_border_width(key_obj[k], 2, 0);
		lv_obj_set_style_border_color(key_obj[k], C_LINE, 0);
		lv_obj_set_style_shadow_width(key_obj[k], 0, 0);
		/* the icon takes its color from the key, white while it is pressed */
		lv_obj_set_style_text_color(key_obj[k], C_TEXT, 0);
		lv_obj_set_style_text_color(key_obj[k], lv_color_white(), LV_STATE_PRESSED);
		l = lv_label_create(key_obj[k]);
		lv_obj_set_style_text_font(l, FONT_M, 0);
		lv_label_set_text(l, symbols[k]);
		lv_obj_center(l);
	}

	{
		int gx, gy;
		lv_obj_t *gear_btn;

		gear_rect(&g, &gx, &gy);
		gear_btn = pill_make(lv_screen_active(), LV_SYMBOL_SETTINGS, gx, gy, KEY_SIZE, KEY_SIZE,
				     lv_color_make(250, 251, 253), C_TEXT, NULL);
		lv_obj_set_style_border_width(gear_btn, 2, 0);
		lv_obj_set_style_border_color(gear_btn, C_LINE, 0);
		lv_obj_set_style_bg_color(gear_btn, C_ACCENT, LV_STATE_PRESSED);
		lv_obj_set_style_text_color(gear_btn, C_TEXT, 0);
		lv_obj_add_event_cb(gear_btn, gear_event, LV_EVENT_CLICKED, NULL);
	}

	while (true) {
		uint8_t now = keys_pressed;

		if (status_dirty) {
			status_dirty = false;
			if (qr_shown) {
				/* the code page has its own place for it, the top strip would overlap */
				lv_label_set_text(qr_status, status_text);
			} else {
				lv_label_set_text(label, status_text);
				if (status_text[0] != '\0') {
					lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
				} else {
					lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
				}
			}
		}
		if (net_dirty && form != NULL) {
			net_dirty = false;
			char opts[1100];

			snprintk(opts, sizeof(opts), "%s%s%s", net_options,
				 net_options[0] != '\0' ? "\n" : "", NET_OTHER);
			lv_dropdown_set_options(dd_net, opts);
			{
				const char *cur = lv_textarea_get_text(ta_ssid);

				lv_dropdown_set_text(dd_net, cur[0] != '\0' ? cur : NET_HINT);
			}
			lv_label_set_text(lbl_scan, LV_SYMBOL_REFRESH "  Scan");
		}
		if (wifi_request != 0) {
			wifi_request = 0;
			form_open();
		}
		if (qr_request != 0) {
			int req = qr_request;

			qr_request = 0;
			if (req == 1) {
				if (qr_box == NULL) {
					lv_obj_t *t, *h;

					qr_box = lv_obj_create(lv_screen_active());
					lv_obj_set_size(qr_box, 600, 720);
					lv_obj_center(qr_box);
					lv_obj_set_style_bg_color(qr_box, lv_color_white(), 0);
					lv_obj_set_style_bg_opa(qr_box, LV_OPA_COVER, 0);
					lv_obj_set_style_radius(qr_box, 28, 0);
					lv_obj_set_style_border_width(qr_box, 0, 0);
					lv_obj_set_flex_flow(qr_box, LV_FLEX_FLOW_COLUMN);
					lv_obj_set_flex_align(qr_box, LV_FLEX_ALIGN_START,
							      LV_FLEX_ALIGN_CENTER,
							      LV_FLEX_ALIGN_CENTER);
					lv_obj_set_style_pad_row(qr_box, 14, 0);
					lv_obj_set_style_pad_top(qr_box, 24, 0);
					lv_obj_set_style_shadow_width(qr_box, 0, 0);
					lv_obj_remove_flag(qr_box, LV_OBJ_FLAG_SCROLLABLE);

					t = lv_label_create(qr_box);
					lv_obj_set_style_text_font(t, FONT_L, 0);
					lv_obj_set_style_text_color(t, C_TEXT, 0);
					lv_label_set_text(t, "Scan to connect");

					qr = lv_qrcode_create(qr_box);
					lv_qrcode_set_size(qr, 480);
					lv_qrcode_set_dark_color(qr, lv_color_black());
					lv_qrcode_set_light_color(qr, lv_color_white());
					lv_obj_set_style_border_color(qr, lv_color_white(), 0);
					lv_obj_set_style_border_width(qr, 16, 0);

					h = lv_label_create(qr_box);
					lv_obj_set_width(h, 560);
					lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
					lv_obj_set_style_text_font(h, FONT_S, 0);
					lv_obj_set_style_text_color(h, C_MUTED, 0);
					lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
					lv_label_set_text(h, "Phone: Settings > Developer options > "
							     "Wireless debugging > Pair device "
							     "with QR code");

					qr_status = lv_label_create(qr_box);
					lv_obj_set_style_text_font(qr_status, FONT_M, 0);
					lv_obj_set_style_text_color(qr_status, C_ACCENT, 0);
					lv_label_set_text(qr_status, "");
				}
				lv_qrcode_update(qr, qr_payload, strlen(qr_payload));
				lv_obj_remove_flag(qr_box, LV_OBJ_FLAG_HIDDEN);
				qr_shown = true;
				lv_label_set_text(qr_status, status_text);
				lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
			} else if (qr_box != NULL) {
				lv_obj_add_flag(qr_box, LV_OBJ_FLAG_HIDDEN);
				qr_shown = false;
				lv_label_set_text(label, status_text);
				if (status_text[0] != '\0') {
					lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
				}
			}
		}
		if (now != shown) {
			for (int k = 0; k < NKEYS; k++) {
				if (now & BIT(k)) {
					lv_obj_add_state(key_obj[k], LV_STATE_PRESSED);
				} else {
					lv_obj_remove_state(key_obj[k], LV_STATE_PRESSED);
				}
			}
			shown = now;
		}
		lv_timer_handler();
		k_msleep(16);
	}
}

void ui_start(void)
{
	k_tid_t tid;

	if (!device_is_ready(touch_dev)) {
		printk("ui: the touch panel is not ready\n");
	}
	tid = k_thread_create(&ui_thread, ui_stack, K_THREAD_STACK_SIZEOF(ui_stack), ui_main, NULL,
			      NULL, NULL, 7, 0, K_NO_WAIT);
	k_thread_name_set(tid, "ui");
	tid = k_thread_create(&relay_thread, relay_stack, K_THREAD_STACK_SIZEOF(relay_stack),
			      relay_main, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_name_set(tid, "touch_relay");
}
