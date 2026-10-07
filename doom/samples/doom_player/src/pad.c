/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The keys of the console as translucent LVGL buttons on the ARGB plane, over
 * the game picture (which the video plane shows scaled to the panel).
 *
 * With a touch panel (the chosen zephyr,touch) the fingers are read straight from the input
 * events, one per touch slot, and each is matched against the buttons here: LVGL's pointer
 * handles one finger, a game needs the directions and a button at the same time. The buttons
 * only show the state.
 */

#include <lvgl.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <doom_zephyr/doom.h>

#include "app.h"

#define HAS_TOUCH	DT_NODE_EXISTS(DT_CHOSEN(zephyr_touch))

#if HAS_TOUCH
#include <zephyr/input/input.h>
#endif

#define STACK_SIZE	4096
#define PRIORITY	7

K_THREAD_STACK_DEFINE(pad_stack, STACK_SIZE);
static struct k_thread thread;

#define MAX_KEYS	12
#define HIT_MARGIN	12
/*
 * The controller now and then reports a frame without the finger while it stays on the glass,
 * which would be a release and a new press: a key is held this long after its finger left.
 */
#define RELEASE_HOLD_MS	50

static struct pad_key {
	lv_obj_t *obj;
	int x, y, w, h;
	unsigned char key;
	bool shown;
	uint32_t hold_until;
} pad_keys_tbl[MAX_KEYS];
static int nkeys;

#if HAS_TOUCH
#define TOUCH_SLOTS	5

static struct {
	int x, y;
	bool down;
} fingers[TOUCH_SLOTS];
static int cur_slot;

static void touch_event(struct input_event *evt, void *user_data)
{
	switch (evt->code) {
	case INPUT_ABS_MT_SLOT:
		cur_slot = CLAMP(evt->value, 0, TOUCH_SLOTS - 1);
		break;
	case INPUT_ABS_X:
		fingers[cur_slot].x = evt->value;
		break;
	case INPUT_ABS_Y:
		fingers[cur_slot].y = evt->value;
		break;
	case INPUT_BTN_TOUCH:
		fingers[cur_slot].down = evt->value != 0;
		break;
	}
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_CHOSEN(zephyr_touch)), touch_event, NULL);

/* hands the key changes to the engine and shows the state on the buttons */
static void touch_scan(void)
{
	uint32_t now = k_uptime_get_32();

	for (int k = 0; k < nkeys; k++) {
		struct pad_key *key = &pad_keys_tbl[k];
		bool pressed = false;

		for (int f = 0; f < TOUCH_SLOTS; f++) {
			if (fingers[f].down && fingers[f].x >= key->x - HIT_MARGIN &&
			    fingers[f].x < key->x + key->w + HIT_MARGIN &&
			    fingers[f].y >= key->y - HIT_MARGIN &&
			    fingers[f].y < key->y + key->h + HIT_MARGIN) {
				pressed = true;
			}
		}
		if (pressed) {
			key->hold_until = now + RELEASE_HOLD_MS;
		} else if ((int32_t)(now - key->hold_until) < 0) {
			pressed = true;
		}
		if (pressed != key->shown) {
			if (pressed) {
				lv_obj_add_state(key->obj, LV_STATE_PRESSED);
			} else {
				lv_obj_remove_state(key->obj, LV_STATE_PRESSED);
			}
			doom_key(pressed ? 1 : 0, key->key);
			key->shown = pressed;
		}
	}
}
#endif /* HAS_TOUCH */

#if !HAS_TOUCH
static void key_event(lv_event_t *e)
{
	unsigned char key = (unsigned char)(uintptr_t)lv_event_get_user_data(e);
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_PRESSED) {
		doom_key(1, key);
	} else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
		doom_key(0, key);
	}
}
#endif

static void key_add(int x, int y, int w, int h, const char *text, unsigned char key, int radius)
{
	lv_obj_t *b = lv_button_create(lv_screen_active());
	lv_obj_t *l = lv_label_create(b);

	lv_obj_set_size(b, w, h);
	lv_obj_align(b, LV_ALIGN_TOP_LEFT, x, y);
	lv_obj_set_style_radius(b, radius, 0);
	lv_obj_set_style_bg_color(b, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(b, LV_OPA_30, 0);
	lv_obj_set_style_bg_color(b, lv_color_make(255, 200, 60), LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
	lv_obj_set_style_border_color(b, lv_color_white(), 0);
	lv_obj_set_style_border_opa(b, LV_OPA_50, 0);
	lv_obj_set_style_border_width(b, 2, 0);
	lv_obj_set_style_shadow_width(b, 0, 0);
	lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(l, lv_color_white(), 0);
	lv_label_set_text(l, text);
	lv_obj_center(l);
#if !HAS_TOUCH
	lv_obj_add_event_cb(b, key_event, LV_EVENT_ALL, (void *)(uintptr_t)key);
#endif

	if (nkeys < MAX_KEYS) {
		pad_keys_tbl[nkeys++] = (struct pad_key){
			.obj = b, .x = x, .y = y, .w = w, .h = h, .key = key,
		};
	}
}

static void pad_main(void *a, void *b, void *c)
{
	lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_TRANSP, 0);

	/*
	 * The layout was drawn for a 1024x600 panel, the keys at the bottom stay at the bottom of
	 * a taller one. The picture is scaled to the middle.
	 */
	const int dy = lv_display_get_vertical_resolution(NULL) - 600;

	key_add(30, 330 + dy, 90, 90, LV_SYMBOL_LEFT, DOOM_KEY_LEFT, 20);
	key_add(210, 330 + dy, 90, 90, LV_SYMBOL_RIGHT, DOOM_KEY_RIGHT, 20);
	key_add(120, 240 + dy, 90, 90, LV_SYMBOL_UP, DOOM_KEY_UP, 20);
	key_add(120, 420 + dy, 90, 90, LV_SYMBOL_DOWN, DOOM_KEY_DOWN, 20);

	key_add(830, 360 + dy, 110, 110, "FIRE", DOOM_KEY_FIRE, LV_RADIUS_CIRCLE);
	key_add(700, 440 + dy, 110, 110, "USE", DOOM_KEY_USE, LV_RADIUS_CIRCLE);

	key_add(20, 20, 120, 60, "ESC", DOOM_KEY_ESCAPE, 16);
	key_add(880, 20, 120, 60, "ENTER", DOOM_KEY_ENTER, 16);
	key_add(20, 100, 120, 60, "STR<", DOOM_KEY_STRAFE_L, 16);
	key_add(880, 100, 120, 60, "STR>", DOOM_KEY_STRAFE_R, 16);
	key_add(400, 520 + dy, 100, 60, "Y", DOOM_KEY_Y, 30);
	key_add(524, 520 + dy, 100, 60, "N", DOOM_KEY_N, 30);

	while (true) {
#if HAS_TOUCH
		touch_scan();
#endif
		lv_timer_handler();
		k_msleep(16);
	}
}

int pad_start(void)
{
	k_tid_t tid = k_thread_create(&thread, pad_stack, STACK_SIZE, pad_main, NULL, NULL, NULL,
				      PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(tid, "pad");

	return 0;
}
