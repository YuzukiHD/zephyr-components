/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The comments over the video. A transparent layer of the player page holds a pool of labels; the
 * UI thread moves them once per round. The position of a scrolling comment is not animated but
 * computed from the position of the video, so it stays right when pictures are late.
 *
 * Rows of 34 pixels: 4 at the top for the comments that stay (4 s), then the rows of the scrolling
 * ones (all with the same speed, so they never overtake), and 4 at the bottom for the others that
 * stay. Mode 6 (left to right) is shown as a scrolling one.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "player/player.h"
#include "ui/ui_priv.h"

#define LANE_H		34
#define SPEED		170		/* pixels per second */
#define GAP		28
#define TOP_ROWS	4
#define BOTTOM_ROWS	4
#define BAR_H		132
#define SCROLL_ROWS	((SCREEN_H - BAR_H - 8) / LANE_H - TOP_ROWS - BOTTOM_ROWS)
#define FIXED_MS	4000
#define STALE_MS	1500

enum kind {
	K_SCROLL,
	K_TOP,
	K_BOTTOM,
};

struct slot {
	lv_obj_t *lbl;
	bool used;
	enum kind kind;
	uint32_t t0, end_ms;
	int32_t w;
};

static struct slot slots[CONFIG_BILI_DM_ACTIVE];
static uint32_t scroll_free_at[SCROLL_ROWS];
static uint32_t top_until[TOP_ROWS], bottom_until[BOTTOM_ROWS];
static lv_obj_t *layer;
static struct dm_set *set;
static uint32_t next_item;
static bool enabled = IS_ENABLED(CONFIG_BILI_DM_DEFAULT);
static uint32_t last_pos, last_pos_t;
static uint32_t density_div = 1;
static uint32_t n_spawned, n_dropped, n_active, last_check;

lv_obj_t *ui_danmaku_create(lv_obj_t *parent)
{
	layer = lv_obj_create(parent);
	lv_obj_set_size(layer, SCREEN_W, SCREEN_H);
	lv_obj_set_pos(layer, 0, 0);
	lv_obj_set_style_bg_opa(layer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(layer, 0, 0);
	lv_obj_set_style_pad_all(layer, 0, 0);
	lv_obj_remove_flag(layer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
	for (int i = 0; i < CONFIG_BILI_DM_ACTIVE; i++) {
		lv_obj_t *l = lv_label_create(layer);

		lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_style_text_font(l, ui_font_m, 0);
		lv_obj_set_style_bg_color(l, lv_color_black(), 0);
		lv_obj_set_style_bg_opa(l, LV_OPA_40, 0);
		lv_obj_set_style_radius(l, 8, 0);
		lv_obj_set_style_pad_hor(l, 8, 0);
		lv_obj_set_style_pad_ver(l, 2, 0);
		lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
		lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
		slots[i].lbl = l;
	}

	return layer;
}

static void clear_all(void)
{
	for (int i = 0; i < CONFIG_BILI_DM_ACTIVE; i++) {
		if (slots[i].used) {
			lv_obj_add_flag(slots[i].lbl, LV_OBJ_FLAG_HIDDEN);
			slots[i].used = false;
		}
	}
	memset(scroll_free_at, 0, sizeof(scroll_free_at));
	memset(top_until, 0, sizeof(top_until));
	memset(bottom_until, 0, sizeof(bottom_until));
	n_active = 0;
}

void ui_danmaku_set(struct dm_set *new_set)
{
	clear_all();
	if (set != NULL) {
		dm_free(set);
		free(set);
	}
	set = new_set;
	next_item = 0;
	last_pos = last_pos_t = 0;
	n_spawned = n_dropped = 0;
}

void ui_danmaku_enable(bool on)
{
	enabled = on;
	if (!on) {
		clear_all();
	} else if (set != NULL) {
		/* start again from the position of the video */
		next_item = dm_find(set, last_pos);
	}
}

bool ui_danmaku_enabled(void)
{
	return enabled;
}

static struct slot *free_slot(void)
{
	for (int i = 0; i < CONFIG_BILI_DM_ACTIVE; i++) {
		if (!slots[i].used) {
			return &slots[i];
		}
	}

	return NULL;
}

/* a text colour that can be read over a picture: the very dark ones become white */
static lv_color_t readable(uint32_t rgb)
{
	uint32_t r = rgb >> 16 & 255, g = rgb >> 8 & 255, b = rgb & 255;

	return (r * 3 + g * 6 + b) / 10 < 70 ? lv_color_white() : lv_color_hex(rgb);
}

/* shows a comment; false when there is no label or no row for it just now */
static bool spawn(const struct dm_item *it, uint32_t p)
{
	struct slot *s = free_slot();
	const char *text = dm_text(set, it);
	int32_t w, x, y;
	enum kind kind = it->mode == DM_TOP ? K_TOP : (it->mode == DM_BOTTOM ? K_BOTTOM : K_SCROLL);
	int row = -1;

	if (s == NULL) {
		return false;
	}
	w = lv_text_get_width(text, it->len, ui_font_m, 0) + 16;
	if (kind == K_SCROLL) {
		for (int i = 0; i < SCROLL_ROWS; i++) {
			if (p >= scroll_free_at[i]) {
				row = i;
				break;
			}
		}
		if (row < 0) {
			return false;
		}
		scroll_free_at[row] = p + (uint32_t)(w + GAP) * 1000 / SPEED;
		x = SCREEN_W;
		y = 4 + (TOP_ROWS + row) * LANE_H;
	} else {
		uint32_t *until = kind == K_TOP ? top_until : bottom_until;
		int rows = kind == K_TOP ? TOP_ROWS : BOTTOM_ROWS;

		for (int i = 0; i < rows; i++) {
			if (p >= until[i]) {
				row = i;
				break;
			}
		}
		if (row < 0) {
			return false;
		}
		until[row] = p + FIXED_MS;
		x = (SCREEN_W - w) / 2;
		y = kind == K_TOP ? 4 + row * LANE_H : SCREEN_H - BAR_H - (row + 1) * LANE_H;
	}
	s->used = true;
	s->kind = kind;
	s->t0 = p;
	s->end_ms = p + FIXED_MS;
	s->w = w;
	lv_label_set_text(s->lbl, text);
	lv_obj_set_style_text_color(s->lbl, readable(it->rgb), 0);
	lv_obj_set_size(s->lbl, w, LANE_H - 4);
	lv_obj_set_pos(s->lbl, x, y);
	lv_obj_remove_flag(s->lbl, LV_OBJ_FLAG_HIDDEN);
	n_active++;
	n_spawned++;

	return true;
}

void ui_danmaku_run(uint32_t round_us)
{
	uint32_t now = k_uptime_get_32(), pos = player_position_ms(), p;

	if (set == NULL || !enabled) {
		return;
	}
	if (pos != last_pos) {
		if (pos + 1000 < last_pos || pos > last_pos + 5000) {
			/* the video jumped */
			clear_all();
			next_item = dm_find(set, pos);
		}
		last_pos = pos;
		last_pos_t = now;
	}
	/* between two pictures the position moves on with the clock, but never far ahead of the video */
	p = pos + (player_state() == PLAYER_PLAYING ? MIN(now - last_pos_t, 100U) : 0);

	/* a round that took very long (wall time, the video thread runs in between): half of the comments are left out until it is fast again */
	if (now - last_check > 1000) {
		last_check = now;
		density_div = round_us > 60000 ? 2 : 1;
	}

	for (int i = 0; i < CONFIG_BILI_DM_ACTIVE; i++) {
		struct slot *s = &slots[i];

		if (!s->used) {
			continue;
		}
		if (s->kind == K_SCROLL) {
			int32_t x = SCREEN_W - (int32_t)((int64_t)(p - s->t0) * SPEED / 1000);

			if (x + s->w < 0) {
				s->used = false;
				lv_obj_add_flag(s->lbl, LV_OBJ_FLAG_HIDDEN);
				n_active--;
			} else {
				lv_obj_set_x(s->lbl, x);
			}
		} else if (p >= s->end_ms) {
			s->used = false;
			lv_obj_add_flag(s->lbl, LV_OBJ_FLAG_HIDDEN);
			n_active--;
		}
	}
	while (next_item < set->count) {
		const struct dm_item *it = &set->items[next_item];

		if (it->t_ms > p) {
			break;
		}
		if (p - it->t_ms > STALE_MS || (next_item % density_div) != 0) {
			n_dropped++;
			next_item++;
			continue;
		}
		if (!spawn(it, p)) {
			break;
		}
		next_item++;
	}
}

bool ui_danmaku_active(void)
{
	return set != NULL && enabled;
}
