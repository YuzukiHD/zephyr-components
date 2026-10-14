/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The player page. The picture of the video is on its own plane under the interface, so the page is
 * transparent: the comments, a bar at the top (back, title) and a bar at the bottom (play/pause, the
 * time, the progress with the part that is already downloaded, the comments switch). A tap on the
 * picture shows or hides the bars; they hide by themselves after a few seconds of playing. A message
 * ("loading", an error) is a pill in the middle.
 */

#include <stdio.h>

#include <zephyr/kernel.h>

#include "player/player.h"
#include "ui/ui_priv.h"

#define TOP_H		96
#define BOTTOM_H	120
#define HIDE_MS		4000
#define BTN		64
#define SHADE		LV_OPA_60

enum icon {
	ICON_BACK,
	ICON_PLAY,
};

static lv_obj_t *page, *top_bar, *bottom_bar, *title, *msg, *time_label, *total_label, *buf_bar, *play_bar,
	*knob, *play_btn, *dm_btn, *dm_label;
static uint32_t duration;
static bool paused, bars_visible = true;
static int64_t last_touch;
static int buffered;
static lv_obj_t *seek_area;
static bool dragging;
static int drag_v;		/* the position under the finger, in thousandths */

static void touched(void)
{
	last_touch = k_uptime_get();
}

static void show_bars(bool on)
{
	bars_visible = on;
	if (on) {
		lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_HIDDEN);
		lv_obj_remove_flag(bottom_bar, LV_OBJ_FLAG_HIDDEN);
		touched();
	} else {
		lv_obj_add_flag(top_bar, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(bottom_bar, LV_OBJ_FLAG_HIDDEN);
	}
}

static void page_clicked(lv_event_t *e)
{
	show_bars(!bars_visible);
}

static void back_clicked(lv_event_t *e)
{
	app_cmd(CMD_STOP, 0);
}

static void pause_clicked(lv_event_t *e)
{
	touched();
	app_cmd(CMD_PAUSE, 0);
}

/* The progress bar is also the place to seek: the finger sets the position, letting go jumps there. */
#define SEEK_MARGIN	8	/* thousandths kept back from the end of the downloaded part */

static void seek_move(void)
{
	lv_point_t p;
	lv_area_t a;
	uintptr_t geo = (uintptr_t)lv_obj_get_user_data(knob);
	int bar_x = (int)(geo & 0xffff), bar_w = (int)(geo >> 16), v, lim = buffered >= 1000 ? 1000 : buffered - SEEK_MARGIN;
	uint32_t ms;
	char s[16];

	lv_indev_get_point(lv_indev_active(), &p);
	lv_obj_get_coords(bottom_bar, &a);
	v = (p.x - a.x1 - bar_x) * 1000 / bar_w;
	v = CLAMP(v, 0, MAX(lim, 0));
	drag_v = v;
	ms = (uint32_t)((uint64_t)duration * v / 1000);
	snprintf(s, sizeof(s), "%u:%02u", ms / 60000, ms / 1000 % 60);
	lv_label_set_text(time_label, s);
	lv_bar_set_value(play_bar, v, LV_ANIM_OFF);
	lv_obj_set_x(knob, bar_x + bar_w * v / 1000 - 10);
}

static void seek_event(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);

	if (duration == 0) {
		return;
	}
	touched();
	if (code == LV_EVENT_PRESSED) {
		dragging = true;
		seek_move();
	} else if (code == LV_EVENT_PRESSING) {
		if (dragging) {
			seek_move();
		}
	} else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && dragging) {
		/* a finger that slides off the strip still sets the position */
		dragging = false;
		printk("play: seek to %u ms\n", (unsigned)((uint64_t)duration * drag_v / 1000));
		player_seek((uint32_t)((uint64_t)duration * drag_v / 1000));
	}
}

static void dm_set_look(void)
{
	bool on = ui_danmaku_enabled();

	lv_obj_set_style_text_color(dm_label, on ? C_ACCENT : C_DIM, 0);
	lv_obj_set_style_border_color(dm_btn, on ? C_ACCENT : C_DIM, 0);
}

static void dm_clicked(lv_event_t *e)
{
	touched();
	ui_danmaku_enable(!ui_danmaku_enabled());
	dm_set_look();
}

/* the icons are drawn, the font has no symbols */
static void icon_draw(lv_event_t *e)
{
	lv_obj_t *o = lv_event_get_target_obj(e);
	lv_layer_t *layer = lv_event_get_layer(e);
	enum icon ic = (enum icon)(intptr_t)lv_event_get_user_data(e);
	lv_area_t a;
	int cx, cy;

	lv_obj_get_coords(o, &a);
	cx = (a.x1 + a.x2) / 2;
	cy = (a.y1 + a.y2) / 2;
	if (ic == ICON_BACK) {
		lv_draw_line_dsc_t l;

		lv_draw_line_dsc_init(&l);
		l.color = C_TEXT;
		l.width = 5;
		l.round_start = l.round_end = 1;
		l.p1.x = cx + 7;
		l.p1.y = cy - 15;
		l.p2.x = cx - 8;
		l.p2.y = cy;
		lv_draw_line(layer, &l);
		l.p1.x = cx - 8;
		l.p1.y = cy;
		l.p2.x = cx + 7;
		l.p2.y = cy + 15;
		lv_draw_line(layer, &l);
	} else if (paused) {
		lv_draw_triangle_dsc_t t;

		lv_draw_triangle_dsc_init(&t);
		t.color = C_TEXT;
		t.opa = LV_OPA_COVER;
		t.p[0].x = cx - 9;
		t.p[0].y = cy - 16;
		t.p[1].x = cx - 9;
		t.p[1].y = cy + 16;
		t.p[2].x = cx + 17;
		t.p[2].y = cy;
		lv_draw_triangle(layer, &t);
	} else {
		lv_draw_rect_dsc_t r;
		lv_area_t b;

		lv_draw_rect_dsc_init(&r);
		r.bg_color = C_TEXT;
		r.bg_opa = LV_OPA_COVER;
		r.radius = 3;
		b.x1 = cx - 12;
		b.x2 = cx - 4;
		b.y1 = cy - 15;
		b.y2 = cy + 15;
		lv_draw_rect(layer, &r, &b);
		b.x1 = cx + 4;
		b.x2 = cx + 12;
		lv_draw_rect(layer, &r, &b);
	}
}

static lv_obj_t *round_button(lv_obj_t *parent, enum icon ic, lv_event_cb_t cb)
{
	lv_obj_t *b = lv_obj_create(parent);

	lv_obj_set_size(b, BTN, BTN);
	lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(b, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(b, LV_OPA_40, 0);
	lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_PRESSED);
	lv_obj_set_style_border_width(b, 0, 0);
	lv_obj_set_style_pad_all(b, 0, 0);
	lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(b, icon_draw, LV_EVENT_DRAW_MAIN_END, (void *)(intptr_t)ic);
	lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);

	return b;
}

static lv_obj_t *plain_label(lv_obj_t *parent, const lv_font_t *font)
{
	lv_obj_t *l = lv_label_create(parent);

	lv_obj_set_style_text_font(l, font, 0);
	lv_obj_set_style_text_color(l, C_TEXT, 0);

	return l;
}

static lv_obj_t *bar_create(lv_obj_t *parent, lv_color_t track, lv_opa_t track_opa, lv_color_t ind, int w)
{
	lv_obj_t *b = lv_bar_create(parent);

	lv_bar_set_range(b, 0, 1000);
	lv_obj_set_size(b, w, 8);
	lv_obj_set_style_radius(b, 4, 0);
	lv_obj_set_style_radius(b, 4, LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(b, track, 0);
	lv_obj_set_style_bg_opa(b, track_opa, 0);
	lv_obj_set_style_bg_color(b, ind, LV_PART_INDICATOR);
	lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
	lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);

	return b;
}

lv_obj_t *ui_play_create(lv_obj_t *parent)
{
	int bar_x, bar_w;

	page = lv_obj_create(parent);
	lv_obj_set_size(page, SCREEN_W, SCREEN_H);
	lv_obj_set_pos(page, 0, 0);
	lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(page, 0, 0);
	lv_obj_set_style_pad_all(page, 0, 0);
	lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(page, page_clicked, LV_EVENT_CLICKED, NULL);

	/* the comments first: the bars are drawn over them */
	if (IS_ENABLED(CONFIG_BILI_DM_FETCH)) {
		ui_danmaku_create(page);
	}

	/* top: back and the title */
	top_bar = lv_obj_create(page);
	lv_obj_set_size(top_bar, SCREEN_W, TOP_H);
	lv_obj_set_pos(top_bar, 0, 0);
	lv_obj_set_style_bg_color(top_bar, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(top_bar, SHADE, 0);
	lv_obj_set_style_border_width(top_bar, 0, 0);
	lv_obj_set_style_radius(top_bar, 0, 0);
	lv_obj_set_style_pad_all(top_bar, 0, 0);
	lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

	round_button(top_bar, ICON_BACK, back_clicked);
	lv_obj_align(lv_obj_get_child(top_bar, 0), LV_ALIGN_LEFT_MID, MARGIN, 0);
	title = plain_label(top_bar, ui_font_l);
	lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
	lv_obj_set_width(title, SCREEN_W - 3 * MARGIN - BTN - 8);
	lv_obj_align(title, LV_ALIGN_LEFT_MID, 2 * MARGIN + BTN + 8, 0);

	/* bottom: play/pause, time, progress, total, comments */
	bottom_bar = lv_obj_create(page);
	lv_obj_set_size(bottom_bar, SCREEN_W, BOTTOM_H);
	lv_obj_set_pos(bottom_bar, 0, SCREEN_H - BOTTOM_H);
	lv_obj_set_style_bg_color(bottom_bar, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(bottom_bar, SHADE, 0);
	lv_obj_set_style_border_width(bottom_bar, 0, 0);
	lv_obj_set_style_radius(bottom_bar, 0, 0);
	lv_obj_set_style_pad_all(bottom_bar, 0, 0);
	lv_obj_remove_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);

	play_btn = round_button(bottom_bar, ICON_PLAY, pause_clicked);
	lv_obj_align(play_btn, LV_ALIGN_LEFT_MID, MARGIN, 0);

	time_label = plain_label(bottom_bar, ui_font_s);
	lv_obj_set_width(time_label, 72);
	lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_align(time_label, LV_ALIGN_LEFT_MID, 2 * MARGIN + BTN, 0);

	bar_x = 2 * MARGIN + BTN + 72 + 14;
	bar_w = SCREEN_W - bar_x - 14 - 72 - 14 - 128 - MARGIN;
	buf_bar = bar_create(bottom_bar, lv_color_white(), LV_OPA_30, lv_color_hex(0x9aa8b8), bar_w);
	lv_obj_align(buf_bar, LV_ALIGN_LEFT_MID, bar_x, 0);
	play_bar = bar_create(bottom_bar, lv_color_black(), LV_OPA_TRANSP, C_ACCENT, bar_w);
	lv_obj_align(play_bar, LV_ALIGN_LEFT_MID, bar_x, 0);
	knob = lv_obj_create(bottom_bar);
	lv_obj_set_size(knob, 20, 20);
	lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(knob, C_ACCENT, 0);
	lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(knob, 3, 0);
	lv_obj_set_style_border_color(knob, lv_color_white(), 0);
	lv_obj_set_style_pad_all(knob, 0, 0);
	lv_obj_remove_flag(knob, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_align(knob, LV_ALIGN_LEFT_MID, bar_x - 10, 0);
	lv_obj_set_user_data(knob, (void *)(intptr_t)(bar_x | bar_w << 16));

	/* a tall invisible strip over the bar, a finger is wider than 8 pixels */
	seek_area = lv_obj_create(bottom_bar);
	lv_obj_set_size(seek_area, bar_w + 40, 64);
	lv_obj_align(seek_area, LV_ALIGN_LEFT_MID, bar_x - 20, 0);
	lv_obj_set_style_bg_opa(seek_area, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(seek_area, 0, 0);
	lv_obj_set_style_pad_all(seek_area, 0, 0);
	lv_obj_remove_flag(seek_area, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(seek_area, seek_event, LV_EVENT_PRESSED, NULL);
	lv_obj_add_event_cb(seek_area, seek_event, LV_EVENT_PRESSING, NULL);
	lv_obj_add_event_cb(seek_area, seek_event, LV_EVENT_RELEASED, NULL);
	lv_obj_add_event_cb(seek_area, seek_event, LV_EVENT_PRESS_LOST, NULL);

	total_label = plain_label(bottom_bar, ui_font_s);
	lv_obj_set_width(total_label, 72);
	lv_obj_align(total_label, LV_ALIGN_LEFT_MID, bar_x + bar_w + 14, 0);

	dm_btn = lv_button_create(bottom_bar);
	lv_obj_set_size(dm_btn, 112, 48);
	lv_obj_set_style_bg_opa(dm_btn, LV_OPA_TRANSP, 0);
	lv_obj_set_style_bg_color(dm_btn, C_CARD_PRESS, LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(dm_btn, LV_OPA_COVER, LV_STATE_PRESSED);
	lv_obj_set_style_shadow_width(dm_btn, 0, 0);
	lv_obj_set_style_radius(dm_btn, 24, 0);
	lv_obj_set_style_border_width(dm_btn, 2, 0);
	lv_obj_set_style_pad_all(dm_btn, 0, 0);
	lv_obj_add_event_cb(dm_btn, dm_clicked, LV_EVENT_CLICKED, NULL);
	lv_obj_align(dm_btn, LV_ALIGN_RIGHT_MID, -MARGIN, 0);
	dm_label = plain_label(dm_btn, ui_font_m);
	lv_label_set_text(dm_label, "\xe5\xbc\xb9\xe5\xb9\x95");			/* danmaku */
	lv_obj_center(dm_label);
	dm_set_look();

	/* a message in the middle: loading, errors */
	msg = lv_label_create(page);
	lv_obj_set_style_text_font(msg, ui_font_m, 0);
	lv_obj_set_style_text_color(msg, C_TEXT, 0);
	lv_obj_set_style_bg_color(msg, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(msg, LV_OPA_70, 0);
	lv_obj_set_style_radius(msg, 26, 0);
	lv_obj_set_style_pad_hor(msg, 28, 0);
	lv_obj_set_style_pad_ver(msg, 12, 0);
	lv_label_set_long_mode(msg, LV_LABEL_LONG_DOT);
	lv_obj_set_width(msg, 640);
	lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_flag(msg, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(msg, LV_OBJ_FLAG_CLICKABLE);

	return page;
}

/* a new video: the bars are up, nothing is played or loaded yet */
void ui_play_set(const char *text, uint32_t duration_ms)
{
	duration = duration_ms;
	paused = false;
	buffered = 0;
	lv_label_set_text(title, text);
	lv_label_set_text(total_label, "");
	show_bars(true);
	lv_bar_set_value(buf_bar, 0, LV_ANIM_OFF);
	ui_play_position(0, false);
	dm_set_look();
}

void ui_play_status(const char *text)
{
	if (text == NULL || text[0] == '\0') {
		lv_obj_add_flag(msg, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_label_set_text(msg, text);
		lv_obj_remove_flag(msg, LV_OBJ_FLAG_HIDDEN);
	}
}

void ui_play_buffered(int permille)
{
	if (permille != buffered) {
		buffered = permille;
		lv_bar_set_value(buf_bar, permille, LV_ANIM_OFF);
	}
}

void ui_play_position(uint32_t pos_ms, bool is_paused)
{
	static int shown = -1;
	char s[16];
	int v = duration ? (int)((uint64_t)pos_ms * 1000 / duration) : 0;
	uintptr_t geo = (uintptr_t)lv_obj_get_user_data(knob);

	if (dragging) {
		return;
	}

	if (is_paused != paused) {
		paused = is_paused;
		lv_obj_invalidate(play_btn);
		if (paused) {
			show_bars(true);	/* a paused picture keeps its controls */
		}
	}
	if (duration > 0) {
		snprintf(s, sizeof(s), "%u:%02u", duration / 60000, duration / 1000 % 60);
		lv_label_set_text(total_label, s);
	}
	snprintf(s, sizeof(s), "%u:%02u", pos_ms / 60000, pos_ms / 1000 % 60);
	lv_label_set_text(time_label, s);
	if (v != shown) {
		shown = v;
		lv_bar_set_value(play_bar, v, LV_ANIM_OFF);
		lv_obj_set_x(knob, (int)(geo & 0xffff) + (int)((geo >> 16) * v / 1000) - 10);
	}
	if (bars_visible && !paused && k_uptime_get() - last_touch > HIDE_MS) {
		show_bars(false);
	}
}
