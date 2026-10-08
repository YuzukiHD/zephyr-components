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

static int key_hit(const struct geom *g, int px, int py)
{
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
				if (down) {
					int k = key_hit(&g, fx, fy);

					if (k >= 0) {
						owner[i] = OWNER_KEY;
						key_of[i] = k;
						keys_pressed |= BIT(k);
						scrcpy_send_key(s, true, key_codes[k]);
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
					keys_pressed &= ~BIT(key_of[i]);
					scrcpy_send_key(s, false, key_codes[key_of[i]]);
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
	lv_obj_t *label, *qr_box = NULL, *qr = NULL;
	uint8_t shown = 0;

	lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_TRANSP, 0);
	label = lv_label_create(lv_screen_active());
	lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(label, lv_color_white(), 0);
	lv_obj_align(label, LV_ALIGN_TOP_LEFT, 12, 8);
	/* text needs an opaque background: on the transparent ARGB layer it turns into boxes */
	lv_obj_set_style_bg_color(label, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(label, 6, 0);
	lv_obj_set_style_radius(label, 6, 0);
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
		lv_obj_set_style_bg_color(key_obj[k], lv_color_make(48, 48, 48), 0);
		lv_obj_set_style_bg_opa(key_obj[k], LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(key_obj[k], lv_color_make(255, 200, 60),
					  LV_STATE_PRESSED);
		lv_obj_set_style_bg_opa(key_obj[k], LV_OPA_COVER, LV_STATE_PRESSED);
		lv_obj_set_style_border_width(key_obj[k], 0, 0);
		lv_obj_set_style_shadow_width(key_obj[k], 0, 0);
		l = lv_label_create(key_obj[k]);
		lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
		lv_obj_set_style_text_color(l, lv_color_white(), 0);
		lv_label_set_text(l, symbols[k]);
		lv_obj_center(l);
	}

	while (true) {
		uint8_t now = keys_pressed;

		if (status_dirty) {
			status_dirty = false;
			lv_label_set_text(label, status_text);
			if (status_text[0] != '\0') {
				lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
			} else {
				lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
			}
		}
		if (qr_request != 0) {
			int req = qr_request;

			qr_request = 0;
			if (req == 1) {
				if (qr_box == NULL) {
					lv_obj_t *t, *h;

					qr_box = lv_obj_create(lv_screen_active());
					lv_obj_set_size(qr_box, 600, 700);
					lv_obj_center(qr_box);
					lv_obj_set_style_bg_color(qr_box, lv_color_white(), 0);
					lv_obj_set_style_bg_opa(qr_box, LV_OPA_COVER, 0);
					lv_obj_set_style_radius(qr_box, 16, 0);
					lv_obj_set_style_border_width(qr_box, 0, 0);
					lv_obj_set_flex_flow(qr_box, LV_FLEX_FLOW_COLUMN);
					lv_obj_set_flex_align(qr_box, LV_FLEX_ALIGN_START,
							      LV_FLEX_ALIGN_CENTER,
							      LV_FLEX_ALIGN_CENTER);
					lv_obj_set_style_pad_row(qr_box, 14, 0);
					lv_obj_remove_flag(qr_box, LV_OBJ_FLAG_SCROLLABLE);

					t = lv_label_create(qr_box);
					lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
					lv_obj_set_style_text_color(t, lv_color_black(), 0);
					lv_label_set_text(t, "Scan to connect");

					qr = lv_qrcode_create(qr_box);
					lv_qrcode_set_size(qr, 520);
					lv_qrcode_set_dark_color(qr, lv_color_black());
					lv_qrcode_set_light_color(qr, lv_color_white());
					lv_obj_set_style_border_color(qr, lv_color_white(), 0);
					lv_obj_set_style_border_width(qr, 16, 0);

					h = lv_label_create(qr_box);
					lv_obj_set_width(h, 560);
					lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
					lv_obj_set_style_text_font(h, &lv_font_montserrat_16, 0);
					lv_obj_set_style_text_color(h, lv_color_black(), 0);
					lv_label_set_text(h, "Phone: Settings > Developer options > "
							     "Wireless debugging > Pair device "
							     "with QR code");
				}
				lv_qrcode_update(qr, qr_payload, strlen(qr_payload));
				lv_obj_remove_flag(qr_box, LV_OBJ_FLAG_HIDDEN);
			} else if (qr_box != NULL) {
				lv_obj_add_flag(qr_box, LV_OBJ_FLAG_HIDDEN);
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
