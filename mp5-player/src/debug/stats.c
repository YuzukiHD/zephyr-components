/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Numbers about the user interface, printed when they happen: the frame rate while a list is
 * swiped, and how long a screen change takes from the touch to the finished picture.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "mp5.h"
#include "player.h"

static bool scrolling;
static uint32_t frames, worst_us, render_start, busy_us;
static int64_t scroll_t0;
static const char *scroll_name = "";
static uint32_t total_frames, total_ms, total_busy_us, total_swipes;

static bool page_pending;
static int64_t page_t0;
static uint32_t page_enter_us;
static const char *page_name = "";

static void refr_start(lv_event_t *e)
{
	ARG_UNUSED(e);
	render_start = k_cycle_get_32();
}

static void refr_ready(lv_event_t *e)
{
	uint32_t us = k_cyc_to_us_floor32(k_cycle_get_32() - render_start);

	ARG_UNUSED(e);
	if (scrolling) {
		frames++;
		busy_us += us;
		worst_us = MAX(worst_us, us);
	}
	if (page_pending) {
		page_pending = false;
		printk("ui: screen %s: %d ms to the first picture (%u ms to set up, %u ms to draw)\n",
		       page_name, (int)(k_uptime_get() - page_t0), page_enter_us / 1000, us / 1000);
	}
}

void ui_stats_init(void)
{
	lv_display_t *d = lv_display_get_default();

	lv_display_add_event_cb(d, refr_start, LV_EVENT_REFR_START, NULL);
	lv_display_add_event_cb(d, refr_ready, LV_EVENT_REFR_READY, NULL);
}

void ui_stats_scroll_begin(const char *name)
{
	scrolling = true;
	scroll_name = name;
	frames = worst_us = busy_us = 0;
	scroll_t0 = k_uptime_get();
}

void ui_stats_scroll_end(void)
{
	int ms = (int)(k_uptime_get() - scroll_t0);

	if (!scrolling) {
		return;
	}
	scrolling = false;
	if (frames > 1 && ms > 0) {
		total_frames += frames;
		total_ms += ms;
		total_busy_us += busy_us;
		total_swipes++;
		printk("ui: swipe in %s: %u frames in %d ms = %u.%u fps, %u us per frame, worst %u us\n",
		       scroll_name, frames, ms, frames * 1000 / ms, (frames * 10000 / ms) % 10,
		       busy_us / frames, worst_us);
	}
}

void ui_stats_page_begin(const char *name)
{
	page_name = name;
	page_t0 = k_uptime_get();
	page_pending = true;
}

void ui_stats_page_set_up(uint32_t us)
{
	page_enter_us = us;
}

#ifdef CONFIG_SAMPLE_MP5_UI_DEMO
#include "history.h"

/* a scripted tour of the screens, for the numbers above without a finger */
static void demo_step(lv_timer_t *t)
{
	static int step;
	int n = step++;

	if (n == 0) {
		ui_goto_arg(SCR_FILES, NULL);
	} else if (n >= 1 && n <= 8) {
		/* the file list down and up, four times: a swipe of about 2000 pixels each */
		files_demo_scroll(n % 2 == 1 ? -2000 : 2000);
	} else if (n == 9) {
		ui_stats_scroll_summary();
	} else if (n == 10) {
		ui_goto(SCR_RECENT);
	} else if (n == 11) {
		ui_goto(SCR_HOME);
	} else if (n == 12) {
		ui_player_open(CONFIG_SAMPLE_MP5_VIDEO_DEFAULT, SCR_RECENT);
	} else if (n == 14) {
		/* back to the list: the video is in it, with the position it was left at */
		ui_goto(SCR_RECENT);
		printk("ui demo: %d videos in the history\n", history_count());
		for (int i = 0; i < history_count(); i++) {
			printk("ui demo:   %s at %u of %u ms\n", history_get(i)->path,
			       (unsigned)history_get(i)->pos_ms, (unsigned)history_get(i)->dur_ms);
		}
	} else if (n == 15) {
		ui_goto(SCR_HOME);
	} else if (n == 16) {
		ui_player_open(CONFIG_SAMPLE_MP5_VIDEO_DEFAULT, SCR_RECENT);
	} else if (n == 18) {
		/* the second time it goes on from the saved position */
		printk("ui demo: second start at %u ms\n", (unsigned)player_position_ms());
		ui_goto(SCR_HOME);
		lv_timer_delete(t);
	}
}

void ui_stats_demo(void)
{
	lv_timer_create(demo_step, 3000, NULL);
}
#endif

void ui_stats_scroll_summary(void)
{
	if (total_ms > 0) {
		printk("ui: %u swipes: %u frames in %u ms = %u.%u fps, %u us per frame\n", total_swipes,
		       total_frames, total_ms, total_frames * 1000 / total_ms,
		       (total_frames * 10000 / total_ms) % 10, total_busy_us / MAX(total_frames, 1U));
	}
	total_frames = total_ms = total_busy_us = total_swipes = 0;
}
