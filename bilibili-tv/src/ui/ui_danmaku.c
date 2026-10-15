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

#include "core/lv_refr_private.h"
#include "display/lv_display_private.h"
#include "misc/lv_area_private.h"
#include "gfx/shift.h"
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
	int32_t x;		/**< where a scrolling comment has to be moved to */
	int32_t s0;		/**< scroll distance (scroll_px()) at the start of a scrolling comment */
};

static struct slot slots[CONFIG_BILI_DM_ACTIVE];
static uint32_t scroll_free_at[SCROLL_ROWS];
static uint32_t top_until[TOP_ROWS], bottom_until[BOTTOM_ROWS];
static lv_obj_t *layer;
static struct dm_set *set;
static uint32_t next_item;
static bool enabled = IS_ENABLED(CONFIG_BILI_DM_DEFAULT);
static uint32_t last_pos;
/*
 * The time of the comments: the video position is only known when a picture is shown (30 times a
 * second, with jitter), so the comments run on the uptime clock. clk_off is "position minus uptime",
 * pulled towards each new reading of the position a little at a time, so a late or early picture
 * does not move the comments back and forth.
 */
static int32_t clk_off;
static bool clk_sync = true;
static uint32_t clk_p;
#define CLK_SNAP_MS	400
#define CLK_AHEAD_MS	150
static uint32_t density_div = 1;

/*
 * All scrolling comments move with the same speed, so what they show is one picture that moves to
 * the left: the distance covered since the start of the video is the same for all of them, and a
 * comment is at SCREEN_W + s0 - scroll_px(now). The picture of the lanes is moved by the 2D
 * accelerator by the distance of the round (shift_cols()), and only the columns that come in at the
 * right are drawn again, like the list does it when it scrolls.
 */
#define LANES_Y		(4 + TOP_ROWS * LANE_H)
#define LANES_H		(SCROLL_ROWS * LANE_H)
#define MAX_SHIFT	200

static int32_t scroll_px(uint32_t p)
{
	return (int32_t)((uint64_t)p * SPEED / 1000);
}

static bool shift_mode, hooked;
static int32_t scroll_seen, pend_dx;
static bool scroll_valid;
static uint32_t n_shifted, n_full, n_other, n_failed, n_msg, n_range, n_still;

static void refresh_soon(lv_display_t *d)
{
	lv_timer_resume(d->refr_timer);
	lv_timer_ready(d->refr_timer);
}

/* LV_EVENT_REFR_START: move the picture of the lanes and add the columns that came in */
static void dm_strips(lv_event_t *e)
{
	lv_display_t *d = lv_display_get_default();
	lv_area_t lanes = {0, LANES_Y, SCREEN_W - 1, LANES_Y + LANES_H - 1}, x;
	bool other = false;

	if (pend_dx == 0) {
		return;
	}
	for (uint32_t i = 0; i < d->inv_p && !other; i++) {
		other = !d->inv_area_joined[i] && lv_area_intersect(&x, &d->inv_areas[i], &lanes);
	}
	if (other) {
		n_other++;
	} else if (shift_cols(0, LANES_Y, SCREEN_W, LANES_H, pend_dx, IS_ENABLED(CONFIG_BILI_DM_VSYNC)) != 0) {
		n_failed++;
	} else {
		lanes.x1 = SCREEN_W - pend_dx;
		n_shifted++;
	}
	if (lanes.x1 == 0) {
		n_full++;		/* something else is drawn there or the move failed: all of it */
	}
	lv_inv_area(d, &lanes);
	pend_dx = 0;
}
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
	if (!hooked && IS_ENABLED(CONFIG_BILI_DM_SHIFT)) {
		hooked = true;
		lv_display_add_event_cb(lv_display_get_default(), dm_strips, LV_EVENT_REFR_START, NULL);
	}
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
	scroll_valid = false;
	pend_dx = 0;
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
	last_pos = 0;
	clk_sync = true;
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
	s->s0 = scroll_px(p);
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
	bool playing = player_state() == PLAYER_PLAYING;

	if (set == NULL || !enabled) {
		return;
	}
	if (pos != last_pos) {
		if (pos + 1000 < last_pos || pos > last_pos + 5000) {
			/* the video jumped */
			clear_all();
			next_item = dm_find(set, pos);
			clk_sync = true;
		}
		last_pos = pos;
		if (playing) {
			int32_t err = (int32_t)(pos - now) - clk_off;

			if (clk_sync || err > CLK_SNAP_MS || err < -CLK_SNAP_MS) {
				clk_off = (int32_t)(pos - now);
				clk_sync = false;
			} else {
				clk_off += err / 8;
			}
		}
	}
	if (!playing) {
		/* paused or ended: the comments stand still, and the clock starts again at the next play */
		p = pos;
		clk_sync = true;
	} else if (clk_sync) {
		p = pos;
	} else {
		p = now + clk_off;
		/* never far ahead of the video (a stall), and never backwards */
		if ((int32_t)(p - (pos + CLK_AHEAD_MS)) > 0) {
			p = pos + CLK_AHEAD_MS;
		}
		if ((int32_t)(p - clk_p) < 0 && clk_p - p < CLK_SNAP_MS) {
			p = clk_p;
		}
	}
	clk_p = p;
	/* PROBE-BEGIN */
	{
		static uint32_t old_t, old_pos_t, pp_new, pp_old, pn, cnt;
		static uint32_t sum_new, sum_old, max_new, max_old, back_new, back_old, rep;
		uint32_t o = pos + (playing ? MIN(now - old_pos_t, 100U) : 0);
		static uint32_t lp;

		if (pos != lp) {
			lp = pos;
			old_pos_t = now;
		}
		if (playing && pn != 0 && now - old_t < 100) {
			int32_t dn = (int32_t)(p - pp_new) - (int32_t)(now - old_t);
			int32_t dold = (int32_t)(o - pp_old) - (int32_t)(now - old_t);
			uint32_t an = dn < 0 ? -dn : dn, ao = dold < 0 ? -dold : dold;

			sum_new += an; sum_old += ao; cnt++;
			max_new = MAX(max_new, an); max_old = MAX(max_old, ao);
			back_new += dn + (int32_t)(now - old_t) < 0;
			back_old += dold + (int32_t)(now - old_t) < 0;
		}
		pp_new = p; pp_old = o; old_t = now; pn = 1;
		if (cnt >= 150 && now - rep > 4000) {
			printk("dmprobe: %u steps: step error mean/max/backwards new %u/%u/%u ms, old %u/%u/%u ms\n",
			       cnt, sum_new / cnt, max_new, back_new, sum_old / cnt, max_old, back_old);
			sum_new = sum_old = max_new = max_old = back_new = back_old = cnt = 0;
			rep = now;
		}
	}
	/* PROBE-END */

	/* a round that took very long (wall time, the video thread runs in between): half of the comments are left out until it is fast again */
	if (now - last_check > 1000) {
		last_check = now;
		density_div = round_us > 60000 ? 2 : 1;
	}

	bool moved_any = false;

	/* the picture of the lanes moves by the distance since the last round when nothing else is drawn there */
	{
		int32_t now_px = scroll_px(p), dpx = now_px - scroll_seen;

		shift_mode = IS_ENABLED(CONFIG_BILI_DM_SHIFT) && scroll_valid && dpx > 0 && dpx <= MAX_SHIFT &&
			     !ui_play_msg_visible() && shift_ready();
		if (!shift_mode) {
			if (dpx == 0) {
				n_still++;
			} else if (ui_play_msg_visible()) {
				n_msg++;
			} else {
				n_range++;
			}
		}
		if (shift_mode) {
			pend_dx += dpx;
		}
		scroll_seen = now_px;
		scroll_valid = true;
	}
	for (int i = 0; i < CONFIG_BILI_DM_ACTIVE; i++) {
		struct slot *s = &slots[i];

		if (!s->used) {
			continue;
		}
		if (s->kind == K_SCROLL) {
			int32_t x = SCREEN_W + s->s0 - scroll_seen;

			if (x + s->w < 0) {
				s->used = false;
				lv_obj_add_flag(s->lbl, LV_OBJ_FLAG_HIDDEN);
				n_active--;
			} else if (shift_mode) {
				s->x = x;		/* moved below, with the invalidation off */
				moved_any = true;
			} else {
				lv_obj_set_x(s->lbl, x);
			}
		} else if (p >= s->end_ms) {
			s->used = false;
			lv_obj_add_flag(s->lbl, LV_OBJ_FLAG_HIDDEN);
			n_active--;
		}
	}
	if (moved_any) {
		/*
		 * The position of a label is a style: LVGL moves the label when it updates the layout, and
		 * draws its old and its new place again then. The picture has moved already, so the move
		 * is made now, with the invalidation off.
		 */
		lv_display_t *d = lv_display_get_default();

		lv_display_enable_invalidation(d, false);
		for (int i = 0; i < CONFIG_BILI_DM_ACTIVE; i++) {
			if (slots[i].used && slots[i].kind == K_SCROLL) {
				lv_obj_set_x(slots[i].lbl, slots[i].x);
			}
		}
		lv_obj_update_layout(layer);
		lv_display_enable_invalidation(d, true);
	}
	if (pend_dx != 0) {
		refresh_soon(lv_display_get_default());
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

void ui_danmaku_moves(uint32_t *moved, uint32_t *full)
{
	*moved = n_shifted;
	*full = n_full;
	{
		uint32_t n, wait, total;

		shift_cols_times(&n, &wait, &total);
		printk("dm: shift_cols %u calls, %u us in all each, %u us of that waiting for the blank\n", n,
		       n ? total / n : 0, n ? wait / n : 0);
	}
	printk("dm: lanes moved %u, drawn whole %u (other drawing there %u, move failed %u); not moved: still %u, message %u, range %u\n",
	       n_shifted, n_full, n_other, n_failed, n_still, n_msg, n_range);
}

uint32_t ui_danmaku_count(void)
{
	return n_active;
}

bool ui_danmaku_active(void)
{
	return set != NULL && enabled;
}
