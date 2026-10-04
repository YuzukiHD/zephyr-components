/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The keys of the console as translucent LVGL buttons on the ARGB plane, over
 * the game picture (which the video plane shows scaled to the panel).
 */

#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <mgba_zephyr/gba.h>

#include "app.h"

#define STACK_SIZE	4096
#define PRIORITY	7

K_THREAD_STACK_DEFINE(pad_stack, STACK_SIZE);
static struct k_thread thread;
static atomic_t keys;

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
}

static void pad_main(void *a, void *b, void *c)
{
	lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_TRANSP, 0);

	/* 1024x600 panel; the picture is 900 wide in the middle */
	key_add(30, 330, 90, 90, LV_SYMBOL_LEFT, GBA_KEY_LEFT, 20);
	key_add(210, 330, 90, 90, LV_SYMBOL_RIGHT, GBA_KEY_RIGHT, 20);
	key_add(120, 240, 90, 90, LV_SYMBOL_UP, GBA_KEY_UP, 20);
	key_add(120, 420, 90, 90, LV_SYMBOL_DOWN, GBA_KEY_DOWN, 20);

	key_add(830, 360, 100, 100, "A", GBA_KEY_A, LV_RADIUS_CIRCLE);
	key_add(720, 430, 100, 100, "B", GBA_KEY_B, LV_RADIUS_CIRCLE);

	key_add(20, 20, 140, 70, "L", GBA_KEY_L, 16);
	key_add(864, 20, 140, 70, "R", GBA_KEY_R, 16);

	key_add(380, 520, 120, 60, "SELECT", GBA_KEY_SELECT, 30);
	key_add(524, 520, 120, 60, "START", GBA_KEY_START, 30);

	while (true) {
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
