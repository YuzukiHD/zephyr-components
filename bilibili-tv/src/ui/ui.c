/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * LVGL is used by one thread only (the main thread, ui_run()). The other threads store what they
 * want shown under a mutex and the UI thread picks it up on its next round. The pages (home,
 * search, player) are three full screen containers of which one is visible.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "gfx/shift.h"
#include "ui/ui_priv.h"

static K_MUTEX_DEFINE(lock);
static struct {
	bool status_dirty, hot_dirty, play_dirty, pos_dirty;
	char status[128];
	char hot[10][BILI_HOT_WORD_LEN];
	int hot_n;
	bool playing;
	char title[160];
	uint32_t dur_ms, pos_ms;
	bool paused;
} pend;

static lv_obj_t *scr, *status_label, *pages[PAGE_COUNT];

static struct dm_set *pend_dm;
static bool pend_dm_dirty;
static volatile uint32_t round_us;
static volatile int buffered_permille;	/* the last lv_timer_handler() */

static volatile int nav_req = -1;
static enum page cur = PAGE_HOME, return_page = PAGE_HOME;

void ui_set_status(const char *fmt, ...)
{
	va_list ap;

	k_mutex_lock(&lock, K_FOREVER);
	va_start(ap, fmt);
	vsnprintf(pend.status, sizeof(pend.status), fmt, ap);
	va_end(ap);
	pend.status_dirty = true;
	k_mutex_unlock(&lock);
}

void ui_set_hot_words(char words[][BILI_HOT_WORD_LEN], int count)
{
	k_mutex_lock(&lock, K_FOREVER);
	pend.hot_n = MIN(count, (int)ARRAY_SIZE(pend.hot));
	memcpy(pend.hot, words, pend.hot_n * BILI_HOT_WORD_LEN);
	pend.hot_dirty = true;
	k_mutex_unlock(&lock);
}

void ui_set_danmaku(struct dm_set *new_set)
{
	k_mutex_lock(&lock, K_FOREVER);
	if (pend_dm_dirty && pend_dm != NULL) {
		dm_free(pend_dm);
		free(pend_dm);
	}
	pend_dm = new_set;
	pend_dm_dirty = true;
	k_mutex_unlock(&lock);
}

void ui_set_playing(const char *title, uint32_t duration_ms)
{
	k_mutex_lock(&lock, K_FOREVER);
	pend.playing = title != NULL;
	snprintf(pend.title, sizeof(pend.title), "%s", title != NULL ? title : "");
	pend.dur_ms = duration_ms;
	pend.pos_ms = 0;
	pend.paused = false;
	pend.play_dirty = true;
	k_mutex_unlock(&lock);
}

void ui_set_buffered(int permille)
{
	buffered_permille = permille;
}

void ui_set_position(uint32_t pos_ms, bool paused)
{
	k_mutex_lock(&lock, K_FOREVER);
	pend.pos_ms = pos_ms;
	pend.paused = paused;
	pend.pos_dirty = true;
	k_mutex_unlock(&lock);
}

void ui_type(const char *keys, int pick, bool submit_now)
{
	ui_search_inject(keys, pick, submit_now);
}

void ui_goto(int page)
{
	nav_req = page;
}

enum page ui_page(void)
{
	return cur;
}

int ui_current_page(void)
{
	return (int)cur;
}

void ui_nav(enum page page)
{
	cur = page;
	/* the player page has its own messages */
	if (status_label != NULL) {
		if (page == PAGE_PLAY) {
			lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
		} else {
			lv_obj_remove_flag(status_label, LV_OBJ_FLAG_HIDDEN);
		}
	}
	/* the picture of the video is worth more than the redraws of the comments over it */
	k_thread_priority_set(k_current_get(), page == PAGE_PLAY ? 8 : 6);
	for (int i = 0; i < PAGE_COUNT; i++) {
		if (i == page) {
			lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
		} else {
			lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
		}
	}
	/* the video plane shows through while a video plays */
	lv_obj_set_style_bg_opa(scr, page == PAGE_PLAY ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
	lv_obj_invalidate(scr);
	if (page == PAGE_HOME) {
		ui_home_show();
	} else if (page == PAGE_SEARCH) {
		ui_search_show();
	}
}

/*
 * The screen at half the size as RGB565, made by the UI thread when it comes round to it. The full
 * size picture (3 MiB) is rendered into memory of malloc, the memory pool of LVGL is too small for
 * it, and the small picture is written over its beginning.
 */
void ui_set_status_label(const char *text)
{
	lv_label_set_text(status_label, text);
	ui_play_status(text);
}

void ui_init(void)
{
	ui_fonts_init();
	cover_init();

	scr = lv_screen_active();
	lv_obj_set_style_bg_color(scr, C_BG, 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
	lv_obj_set_style_text_color(scr, C_TEXT, 0);
	lv_obj_set_style_text_font(scr, ui_font_m, 0);
	lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

	/* below the pages, in the strip they leave free */
	status_label = lv_label_create(scr);
	lv_obj_set_style_text_font(status_label, ui_font_s, 0);
	lv_obj_set_style_text_color(status_label, C_DIM, 0);
	lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);
	lv_obj_set_width(status_label, SCREEN_W - 2 * MARGIN);
	lv_obj_align(status_label, LV_ALIGN_BOTTOM_LEFT, MARGIN, -4);
	lv_label_set_text(status_label, "");

	pages[PAGE_HOME] = ui_home_create(scr);
	pages[PAGE_SEARCH] = ui_search_create(scr);
	pages[PAGE_PLAY] = ui_play_create(scr);
	ui_nav(PAGE_HOME);
}

uint32_t ui_run(void)
{
	static bool blanked = true;
	char status[128], hot[10][BILI_HOT_WORD_LEN], title[160];
	int hot_n = 0;
	bool status_dirty, hot_dirty, play_dirty, pos_dirty, playing = false, paused = false, dm_dirty = false;
	struct dm_set *new_dm = NULL;
	uint32_t dur = 0, pos = 0;

	ui_grids_scroll_flush();
	k_mutex_lock(&lock, K_FOREVER);
	status_dirty = pend.status_dirty;
	hot_dirty = pend.hot_dirty;
	play_dirty = pend.play_dirty;
	pos_dirty = pend.pos_dirty;
	if (status_dirty) {
		memcpy(status, pend.status, sizeof(status));
	}
	if (hot_dirty) {
		hot_n = pend.hot_n;
		memcpy(hot, pend.hot, sizeof(hot));
	}
	if (play_dirty || pos_dirty) {
		playing = pend.playing;
		paused = pend.paused;
		dur = pend.dur_ms;
		pos = pend.pos_ms;
		memcpy(title, pend.title, sizeof(title));
	}
	pend.status_dirty = pend.hot_dirty = pend.play_dirty = pend.pos_dirty = false;
	if (pend_dm_dirty) {
		new_dm = pend_dm;
		dm_dirty = true;
		pend_dm = NULL;
		pend_dm_dirty = false;
	}
	k_mutex_unlock(&lock);
	if (dm_dirty) {
		ui_danmaku_set(new_dm);
	}

	if (status_dirty) {
		ui_set_status_label(status);
	}
	if (hot_dirty) {
		ui_search_hot_words(hot, hot_n);
	}
	if (play_dirty) {
		if (playing) {
			if (cur != PAGE_PLAY) {
				return_page = cur;
			}
			ui_play_set(title, dur);
			cover_pause(true);
			if (IS_ENABLED(CONFIG_BILI_SCROLL_SHIFT)) {
				shift_release();
			}
			ui_nav(PAGE_PLAY);
		} else {
			printk("ui: leaving the player\n");
			cover_pause(false);
			ui_danmaku_set(NULL);
			printk("ui: the comments are gone\n");
			ui_nav(return_page);
			printk("ui: back on page %d\n", (int)cur);
		}
	} else if (pos_dirty && cur == PAGE_PLAY) {
		ui_play_position(pos, paused);
		ui_play_buffered(buffered_permille);
	}

	if (nav_req >= 0) {
		int p = nav_req;

		nav_req = -1;
		if (p <= PAGE_SEARCH) {
			ui_nav(p);
		}
	}
	ui_home_run(cur == PAGE_HOME);
	ui_search_run(cur == PAGE_SEARCH);

	if (cur == PAGE_PLAY && ui_danmaku_active()) {
		ui_danmaku_run(round_us);
	}
	{
		uint32_t t0 = k_cycle_get_32(), next = lv_timer_handler();

		round_us = k_cyc_to_us_floor32(k_cycle_get_32() - t0);
		if (blanked) {
			display_blanking_off(DEVICE_DT_GET(DT_CHOSEN(zephyr_display)));
			blanked = false;
		}

		/* the comments move with the pictures of the video: 30 times a second is enough */
		return cur == PAGE_PLAY && ui_danmaku_active() ? 40U : MIN(next, 20U);
	}
}
