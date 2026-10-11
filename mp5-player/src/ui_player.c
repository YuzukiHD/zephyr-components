/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The video screen: the picture is on the video plane, the controls are drawn over it */

#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "mp5.h"
#include "history.h"
#include "player.h"

#define HIDE_AFTER_MS	4000
#define SKIP_MS		10000
#define PANEL_H		200
#define SCREEN_W	1024
#define SCREEN_H	768
#define MARGIN		40

static lv_obj_t *top_bar, *panel, *title_label, *cur_label, *dur_label, *seek_slider;
static lv_obj_t *vol_slider, *vol_icon, *play_btn, *play_icon, *info_label, *message;
static lv_timer_t *tick;
static bool dragging, controls_visible = true;
static int64_t last_touch_ms;
static uint32_t duration_ms;
static char current_path[256];
static enum screen_id origin = SCR_HOME;
static int64_t saved_ms;
static const char *open_path;

#define SAVE_EVERY_MS	10000
#define RESUME_MIN_MS	5000
#define RESUME_END_MS	10000
static int volume_before_mute = 70;
static uint32_t shown_seconds = UINT32_MAX;
static enum player_state shown_state = PLAYER_IDLE;

static void format_time(char *buf, size_t size, uint32_t ms)
{
	uint32_t s = ms / 1000;

	if (s >= 3600) {
		snprintf(buf, size, "%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
	} else {
		snprintf(buf, size, "%u:%02u", s / 60, s % 60);
	}
}

static void controls_show(bool show)
{
	static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	controls_visible = show;
	/* the display engine reads the whole frame buffer plane 60 times a second, which takes
	 * memory bandwidth from the decoder: with nothing to draw the plane is switched off
	 */
	display_sunxi_fb_enable(disp, show);
	if (show) {
		lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_HIDDEN);
		lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
		last_touch_ms = k_uptime_get();
	} else {
		lv_obj_add_flag(top_bar, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
	}
}

static void touched(lv_event_t *e)
{
	ARG_UNUSED(e);
	last_touch_ms = k_uptime_get();
}

static void screen_clicked(lv_event_t *e)
{
	if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
		controls_show(!controls_visible);
	}
}

static void back_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	if (origin == SCR_FILES) {
		char dir[sizeof(current_path)];
		char *slash;

		strcpy(dir, current_path);
		slash = strrchr(dir, '/');
		if (slash != NULL) {
			*slash = '\0';
		}
		ui_goto_arg(SCR_FILES, dir);
	} else {
		ui_goto(origin);
	}
}

/* where the video is now goes to the list of the videos played last */
static void history_save(void)
{
	enum player_state st = player_state();
	uint32_t pos = st == PLAYER_ENDED ? 0 : player_position_ms();

	if (current_path[0] != '\0' && duration_ms > 0 && st != PLAYER_ERROR) {
		history_update(current_path, pos, duration_ms);
		saved_ms = k_uptime_get();
	}
}

void ui_player_open(const char *path, enum screen_id from)
{
	origin = from;
	open_path = path;
	ui_goto_arg(SCR_PLAYER, (void *)path);
}

static void play_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	switch (player_state()) {
	case PLAYER_PLAYING:
		player_pause();
		break;
	case PLAYER_PAUSED:
	case PLAYER_ENDED:
		player_play();
		break;
	default:
		break;
	}
}

static void skip(int32_t delta_ms)
{
	int64_t to = (int64_t)player_position_ms() + delta_ms;

	player_seek((uint32_t)CLAMP(to, 0, (int64_t)duration_ms));
}

static void back10_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	skip(-SKIP_MS);
}

static void fwd10_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	skip(SKIP_MS);
}

/* the time label follows the finger, the player jumps when it is let go */
static void seek_event(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);
	uint32_t value = lv_slider_get_value(seek_slider);
	char buf[16];

	last_touch_ms = k_uptime_get();
	if (code == LV_EVENT_PRESSED) {
		dragging = true;
	}
	if (code == LV_EVENT_VALUE_CHANGED) {
		format_time(buf, sizeof(buf), value);
		lv_label_set_text(cur_label, buf);
	} else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
		dragging = false;
		player_seek(value);
		shown_seconds = UINT32_MAX;
	}
}

static const char *volume_symbol(int percent)
{
	if (percent == 0) {
		return LV_SYMBOL_MUTE;
	}

	return percent < 50 ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_VOLUME_MAX;
}

static void volume_event(lv_event_t *e)
{
	int v = lv_slider_get_value(vol_slider);

	ARG_UNUSED(e);
	last_touch_ms = k_uptime_get();
	player_set_volume(v);
	lv_label_set_text(vol_icon, volume_symbol(v));
}

static void mute_clicked(lv_event_t *e)
{
	int v = player_volume();

	ARG_UNUSED(e);
	if (v > 0) {
		volume_before_mute = v;
		v = 0;
	} else {
		v = volume_before_mute;
	}
	player_set_volume(v);
	lv_slider_set_value(vol_slider, v, LV_ANIM_OFF);
	lv_label_set_text(vol_icon, volume_symbol(v));
}

static void tick_cb(lv_timer_t *t)
{
	enum player_state st = player_state();
	uint32_t pos = player_position_ms();
	char buf[16];

	ARG_UNUSED(t);
	if (!dragging && pos / 1000 != shown_seconds) {
		shown_seconds = pos / 1000;
		lv_slider_set_value(seek_slider, pos, LV_ANIM_OFF);
		format_time(buf, sizeof(buf), pos);
		lv_label_set_text(cur_label, buf);
	}
	if (st != shown_state) {
		shown_state = st;
		lv_label_set_text(play_icon, st == PLAYER_PLAYING ? LV_SYMBOL_PAUSE :
					     st == PLAYER_ENDED ? LV_SYMBOL_REFRESH : LV_SYMBOL_PLAY);
		if (st != PLAYER_PLAYING) {
			controls_show(true);
		}
		if (st == PLAYER_ERROR) {
			lv_label_set_text(message, LV_SYMBOL_WARNING "\nThis video cannot be decoded\n(not enough memory for its size?)");
			lv_obj_remove_flag(message, LV_OBJ_FLAG_HIDDEN);
		}
	}
	if (st == PLAYER_PLAYING && k_uptime_get() - saved_ms > SAVE_EVERY_MS) {
		history_save();
	}
	if (st == PLAYER_PLAYING && controls_visible && !dragging &&
	    k_uptime_get() - last_touch_ms > HIDE_AFTER_MS) {
		controls_show(false);
	}
}

static lv_obj_t *flat_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
	lv_obj_t *l = lv_label_create(parent);

	lv_obj_set_style_text_font(l, font, 0);
	lv_obj_set_style_text_color(l, lv_color_hex(color), 0);

	return l;
}

static void style_slider(lv_obj_t *s, int32_t height, int32_t knob)
{
	lv_obj_remove_style_all(s);
	lv_obj_set_height(s, height);
	lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_opa(s, LV_OPA_30, 0);
	lv_obj_set_style_bg_color(s, lv_color_white(), 0);
	lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
	lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(s, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
	lv_obj_set_style_bg_grad_color(s, lv_color_hex(C_ACCENT2), LV_PART_INDICATOR);
	lv_obj_set_style_bg_grad_dir(s, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
	lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_KNOB);
	lv_obj_set_style_bg_color(s, lv_color_white(), LV_PART_KNOB);
	lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, LV_PART_KNOB);
	lv_obj_set_style_pad_all(s, knob / 2 - height / 2, LV_PART_KNOB);
	lv_obj_set_ext_click_area(s, 26);
}

static lv_obj_t *player_create(void)
{
	lv_obj_t *scr = ui_screen_new(true);
	lv_obj_t *back, *b10, *f10, *l10, *lf10;

	lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(scr, screen_clicked, LV_EVENT_CLICKED, NULL);

	/* the bar at the top */
	top_bar = lv_obj_create(scr);
	lv_obj_remove_style_all(top_bar);
	lv_obj_set_size(top_bar, SCREEN_W, 112);
	lv_obj_set_style_bg_color(top_bar, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(top_bar, LV_OPA_50, 0);
	lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
	back = ui_icon_button(top_bar, LV_SYMBOL_LEFT, 64);
	lv_obj_set_pos(back, 32, 24);
	lv_obj_add_event_cb(back, back_clicked, LV_EVENT_CLICKED, NULL);
	title_label = flat_label(top_bar, &lv_font_montserrat_24, C_TEXT);
	lv_obj_set_width(title_label, 780);
	lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
	lv_obj_set_pos(title_label, 120, 38);
	info_label = flat_label(top_bar, &lv_font_montserrat_16, C_DIM);
	lv_obj_align(info_label, LV_ALIGN_TOP_RIGHT, -32, 42);

	/* the panel at the bottom */
	panel = lv_obj_create(scr);
	lv_obj_remove_style_all(panel);
	lv_obj_set_size(panel, SCREEN_W - 2 * MARGIN, PANEL_H);
	lv_obj_set_pos(panel, MARGIN, SCREEN_H - MARGIN - PANEL_H);
	lv_obj_set_style_radius(panel, 36, 0);
	lv_obj_set_style_bg_color(panel, lv_color_hex(0x0B0F17), 0);
	lv_obj_set_style_bg_opa(panel, LV_OPA_70, 0);
	lv_obj_set_style_border_width(panel, 1, 0);
	lv_obj_set_style_border_color(panel, lv_color_hex(C_BORDER), 0);
	lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(panel, touched, LV_EVENT_PRESSING, NULL);

	cur_label = flat_label(panel, &lv_font_montserrat_16, C_TEXT);
	lv_obj_set_pos(cur_label, 32, 30);
	lv_label_set_text(cur_label, "0:00");
	dur_label = flat_label(panel, &lv_font_montserrat_16, C_DIM);
	lv_obj_align(dur_label, LV_ALIGN_TOP_RIGHT, -32, 30);
	lv_label_set_text(dur_label, "0:00");

	seek_slider = lv_slider_create(panel);
	style_slider(seek_slider, 10, 30);
	lv_obj_set_width(seek_slider, SCREEN_W - 2 * MARGIN - 2 * 120);
	lv_obj_set_pos(seek_slider, 120, 36);
	lv_slider_set_range(seek_slider, 0, 1);
	lv_obj_add_event_cb(seek_slider, seek_event, LV_EVENT_ALL, NULL);

	/* the transport buttons in the middle */
	b10 = ui_icon_button(panel, LV_SYMBOL_PREV, 68);
	lv_obj_set_pos(b10, 330, 86);
	lv_obj_add_event_cb(b10, back10_clicked, LV_EVENT_CLICKED, NULL);
	lv_obj_add_event_cb(b10, touched, LV_EVENT_PRESSING, NULL);
	l10 = flat_label(panel, &lv_font_montserrat_14, C_DIM);
	lv_label_set_text(l10, "-10 s");
	lv_obj_set_pos(l10, 338, 160);

	play_btn = ui_icon_button(panel, LV_SYMBOL_PAUSE, 92);
	play_icon = lv_obj_get_child(play_btn, 0);
	lv_obj_set_style_bg_color(play_btn, lv_color_hex(C_ACCENT), 0);
	lv_obj_set_style_bg_grad_color(play_btn, lv_color_hex(C_ACCENT2), 0);
	lv_obj_set_style_bg_grad_dir(play_btn, LV_GRAD_DIR_VER, 0);
	lv_obj_set_pos(play_btn, (SCREEN_W - 2 * MARGIN) / 2 - 46, 74);
	lv_obj_add_event_cb(play_btn, play_clicked, LV_EVENT_CLICKED, NULL);
	lv_obj_add_event_cb(play_btn, touched, LV_EVENT_PRESSING, NULL);

	f10 = ui_icon_button(panel, LV_SYMBOL_NEXT, 68);
	lv_obj_set_pos(f10, 546, 86);
	lv_obj_add_event_cb(f10, fwd10_clicked, LV_EVENT_CLICKED, NULL);
	lv_obj_add_event_cb(f10, touched, LV_EVENT_PRESSING, NULL);
	lf10 = flat_label(panel, &lv_font_montserrat_14, C_DIM);
	lv_label_set_text(lf10, "+10 s");
	lv_obj_set_pos(lf10, 554, 160);

	/* the volume on the left */
	{
		lv_obj_t *mute = lv_obj_create(panel);

		lv_obj_remove_style_all(mute);
		lv_obj_set_size(mute, 56, 56);
		lv_obj_set_pos(mute, 24, 94);
		lv_obj_add_flag(mute, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_event_cb(mute, mute_clicked, LV_EVENT_CLICKED, NULL);
		vol_icon = flat_label(mute, &lv_font_montserrat_24, C_TEXT);
		lv_label_set_text(vol_icon, LV_SYMBOL_VOLUME_MAX);
		lv_obj_center(vol_icon);
	}
	vol_slider = lv_slider_create(panel);
	style_slider(vol_slider, 8, 26);
	lv_obj_set_width(vol_slider, 190);
	lv_obj_set_pos(vol_slider, 92, 114);
	lv_slider_set_range(vol_slider, 0, 100);
	lv_obj_add_event_cb(vol_slider, volume_event, LV_EVENT_VALUE_CHANGED, NULL);

	message = flat_label(scr, &lv_font_montserrat_24, C_TEXT);
	lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(message, 700);
	lv_obj_align(message, LV_ALIGN_CENTER, 0, -40);
	lv_obj_add_flag(message, LV_OBJ_FLAG_HIDDEN);

	return scr;
}

static void player_enter(void *arg)
{
	const char *path = arg != NULL ? arg : CONFIG_SAMPLE_MP5_VIDEO_DEFAULT;
	const char *name = strrchr(path, '/');
	struct player_info info;
	char buf[16];
	int ret;

	if (arg == NULL) {
		origin = SCR_HOME;
	}
	strncpy(current_path, path, sizeof(current_path) - 1);
	name = name != NULL ? name + 1 : path;
	lv_label_set_text(title_label, name);
	lv_obj_add_flag(message, LV_OBJ_FLAG_HIDDEN);
	dragging = false;
	shown_seconds = UINT32_MAX;
	shown_state = PLAYER_IDLE;
	duration_ms = 0;

	ret = player_open(path, &info);
	if (ret != 0) {
		lv_label_set_text_fmt(message, LV_SYMBOL_WARNING "\nCannot play this file (%d)", ret);
		lv_obj_remove_flag(message, LV_OBJ_FLAG_HIDDEN);
		lv_label_set_text(info_label, "");
		lv_slider_set_range(seek_slider, 0, 1);
		lv_slider_set_value(seek_slider, 0, LV_ANIM_OFF);
		controls_show(true);
		if (tick == NULL) {
			tick = lv_timer_create(tick_cb, 100, NULL);
		}
		return;
	}
	duration_ms = info.duration_ms;
	lv_slider_set_range(seek_slider, 0, MAX(duration_ms, 1U));
	lv_slider_set_value(seek_slider, 0, LV_ANIM_OFF);
	format_time(buf, sizeof(buf), duration_ms);
	lv_label_set_text(dur_label, buf);
	lv_label_set_text(cur_label, "0:00");
	lv_label_set_text_fmt(info_label, "%ux%u  H.264%s", info.width, info.height,
			      info.has_audio ? "  AAC" : "");
	lv_slider_set_value(vol_slider, player_volume(), LV_ANIM_OFF);
	lv_label_set_text(vol_icon, volume_symbol(player_volume()));
	{
		/* go on from where the video was left, unless that was the start or the end */
		const struct history_entry *h = history_find(path);

		if (h != NULL && h->pos_ms > RESUME_MIN_MS && h->pos_ms + RESUME_END_MS < duration_ms) {
			player_seek(h->pos_ms);
		}
	}
	saved_ms = k_uptime_get();
	controls_show(true);
	if (tick == NULL) {
		tick = lv_timer_create(tick_cb, 100, NULL);
	}
	lv_timer_resume(tick);
}

static void player_leave(void)
{
	display_sunxi_fb_enable(DEVICE_DT_GET(DT_CHOSEN(zephyr_display)), true);
	if (tick != NULL) {
		lv_timer_pause(tick);
	}
	history_save();
	player_close();
}

const struct screen_ops player_ops = {
	.create = player_create,
	.enter = player_enter,
	.leave = player_leave,
};
