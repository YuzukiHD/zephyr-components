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
#include <doom_zephyr/doom.h>

#include "app.h"

#define STACK_SIZE	4096
#define PRIORITY	7

K_THREAD_STACK_DEFINE(pad_stack, STACK_SIZE);
static struct k_thread thread;

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
	lv_obj_add_event_cb(b, key_event, LV_EVENT_ALL, (void *)(uintptr_t)key);
}

static void pad_main(void *a, void *b, void *c)
{
	lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_TRANSP, 0);

	/* 1024x600 panel; the picture is scaled to the middle */
	key_add(30, 330, 90, 90, LV_SYMBOL_LEFT, DOOM_KEY_LEFT, 20);
	key_add(210, 330, 90, 90, LV_SYMBOL_RIGHT, DOOM_KEY_RIGHT, 20);
	key_add(120, 240, 90, 90, LV_SYMBOL_UP, DOOM_KEY_UP, 20);
	key_add(120, 420, 90, 90, LV_SYMBOL_DOWN, DOOM_KEY_DOWN, 20);

	key_add(830, 360, 110, 110, "FIRE", DOOM_KEY_FIRE, LV_RADIUS_CIRCLE);
	key_add(700, 440, 110, 110, "USE", DOOM_KEY_USE, LV_RADIUS_CIRCLE);

	key_add(20, 20, 120, 60, "ESC", DOOM_KEY_ESCAPE, 16);
	key_add(880, 20, 120, 60, "ENTER", DOOM_KEY_ENTER, 16);
	key_add(20, 100, 120, 60, "STR<", DOOM_KEY_STRAFE_L, 16);
	key_add(880, 100, 120, 60, "STR>", DOOM_KEY_STRAFE_R, 16);
	key_add(400, 520, 100, 60, "Y", DOOM_KEY_Y, 30);
	key_add(524, 520, 100, 60, "N", DOOM_KEY_N, 30);

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
