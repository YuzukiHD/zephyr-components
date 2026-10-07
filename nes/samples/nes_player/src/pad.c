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
#include <zephyr/sys/atomic.h>
#include <nes_zephyr/nesemu.h>

#include "app.h"

#define HAS_TOUCH	DT_NODE_EXISTS(DT_CHOSEN(zephyr_touch))

#if HAS_TOUCH
#include <zephyr/input/input.h>
#endif

#define STACK_SIZE	4096
#define PRIORITY	7

K_THREAD_STACK_DEFINE(pad_stack, STACK_SIZE);
static struct k_thread thread;
static atomic_t keys;

#define MAX_KEYS	10
#define HIT_MARGIN	12
/*
 * The controller now and then reports a frame without the finger while it stays on the glass,
 * which would be a release and a new press: a key is held this long after its finger left.
 */
#define RELEASE_HOLD_MS	50

static struct pad_key {
	lv_obj_t *obj;
	int x, y, w, h;
	uint32_t bit;
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

/* the keys that have a finger on them; also shows the state on the buttons */
static uint32_t touch_scan(void)
{
	uint32_t mask = 0;
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
		if (pressed) {
			mask |= key->bit;
		}
		if (pressed != key->shown) {
			if (pressed) {
				lv_obj_add_state(key->obj, LV_STATE_PRESSED);
			} else {
				lv_obj_remove_state(key->obj, LV_STATE_PRESSED);
			}
			key->shown = pressed;
		}
	}

	return mask;
}
#endif /* HAS_TOUCH */

static void key_event(lv_event_t *e)
{
	uint32_t bit = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

	if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
		atomic_or(&keys, bit);
	} else {
		atomic_and(&keys, ~bit);
	}
}

static void key_add(int x, int y, int w, int h, const char *text, uint32_t bit, int radius)
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
	lv_obj_add_event_cb(b, key_event, LV_EVENT_ALL, (void *)(uintptr_t)bit);

	if (nkeys < MAX_KEYS) {
		pad_keys_tbl[nkeys++] = (struct pad_key){
			.obj = b, .x = x, .y = y, .w = w, .h = h, .bit = bit,
		};
	}
}

static void pad_main(void *a, void *b, void *c)
{
	lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_TRANSP, 0);

	/*
	 * The layout was drawn for a 1024x600 panel, the keys at the bottom stay at the bottom of
	 * a taller one. The picture is 900 wide in the middle.
	 */
	const int dy = lv_display_get_vertical_resolution(NULL) - 600;

	key_add(30, 330 + dy, 90, 90, LV_SYMBOL_LEFT, NESEMU_KEY_LEFT, 20);
	key_add(210, 330 + dy, 90, 90, LV_SYMBOL_RIGHT, NESEMU_KEY_RIGHT, 20);
	key_add(120, 240 + dy, 90, 90, LV_SYMBOL_UP, NESEMU_KEY_UP, 20);
	key_add(120, 420 + dy, 90, 90, LV_SYMBOL_DOWN, NESEMU_KEY_DOWN, 20);

	key_add(830, 360 + dy, 100, 100, "A", NESEMU_KEY_A, LV_RADIUS_CIRCLE);
	key_add(720, 430 + dy, 100, 100, "B", NESEMU_KEY_B, LV_RADIUS_CIRCLE);


	key_add(380, 520 + dy, 120, 60, "SELECT", NESEMU_KEY_SELECT, 30);
	key_add(524, 520 + dy, 120, 60, "START", NESEMU_KEY_START, 30);

	while (true) {
#if HAS_TOUCH
		atomic_set(&keys, touch_scan());
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

uint32_t pad_keys(void)
{
	return atomic_get(&keys);
}
