/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * A scrolling grid of video cards: cover, title, uploader and counts. It shows one list.
 *
 * A card costs about 5 KiB of the LVGL heap (the object, the cover box, four labels and their styles), so
 * a list of 100 videos needs more than half a megabyte and two tabs more than the heap has: when LVGL cannot
 * allocate the buffer of a layer it retries for ever. Only the rows near the screen have widgets; the others
 * are made when they scroll in and deleted when they are well out (the position of every card is
 * calculated, the footer below the last row gives the container its height).
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/sys/util.h>

#include "ui/ui_priv.h"

int ui_card_height(void)
{
	return COVER_H + 8 + 2 * lv_font_get_line_height(ui_font_m) + 4 + lv_font_get_line_height(ui_font_s) +
	       10;
}

static void fmt_count(char *out, size_t size, uint32_t n)
{
	if (n >= 100000000U) {
		snprintf(out, size, "%u.%u\xe4\xba\xbf", n / 100000000U, (n / 10000000U) % 10);	/* yi */
	} else if (n >= 10000U) {
		snprintf(out, size, "%u.%u\xe4\xb8\x87", n / 10000U, (n / 1000U) % 10);		/* wan */
	} else {
		snprintf(out, size, "%u", n);
	}
}

static void card_click(lv_event_t *e)
{
	app_cmd(CMD_PLAY, (int)(intptr_t)lv_event_get_user_data(e));
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int x, int y, int w, int lines)
{
	lv_obj_t *l = lv_label_create(parent);

	lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_text_font(l, font, 0);
	lv_obj_set_style_text_color(l, color, 0);
	lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
	lv_obj_set_pos(l, x, y);
	lv_obj_set_size(l, w, lines * lv_font_get_line_height(font));

	return l;
}

static void card_build(struct ui_grid *g, int idx, const struct bili_video *v)
{
	struct ui_card *c = &g->cards[idx];
	char buf[128], views[16];
	int title_h = 2 * lv_font_get_line_height(ui_font_m);
	lv_obj_t *o = lv_obj_create(g->obj), *l;

	memset(c, 0, sizeof(*c));
	snprintf(c->pic, sizeof(c->pic), "%s", v->cover);
	c->obj = o;
	lv_obj_set_size(o, CARD_W, ui_card_height());
	lv_obj_set_pos(o, (idx % CARD_COLS) * (CARD_W + MARGIN), (idx / CARD_COLS) * (ui_card_height() + MARGIN));
	lv_obj_set_style_bg_color(o, C_CARD, 0);
	lv_obj_set_style_bg_color(o, C_CARD_PRESS, LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(o, 0, 0);
	lv_obj_set_style_radius(o, 10, 0);
	lv_obj_set_style_pad_all(o, 0, 0);
	lv_obj_set_style_clip_corner(o, true, 0);
	lv_obj_set_style_shadow_width(o, 0, 0);
	lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(o, card_click, LV_EVENT_CLICKED, (void *)(intptr_t)(g->list << 16 | idx));

	/* the cover; the box shows through until the picture is there */
	c->img = lv_image_create(o);
	lv_obj_remove_flag(c->img, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(c->img, COVER_W, COVER_H);
	lv_obj_set_pos(c->img, 0, 0);
	lv_obj_set_style_bg_color(c->img, lv_color_hex(0x232c37), 0);
	lv_obj_set_style_bg_opa(c->img, LV_OPA_COVER, 0);

	if (v->duration > 0) {
		if (v->duration >= 3600) {
			snprintf(buf, sizeof(buf), "%u:%02u:%02u", v->duration / 3600, v->duration / 60 % 60,
				 v->duration % 60);
		} else {
			snprintf(buf, sizeof(buf), "%u:%02u", v->duration / 60, v->duration % 60);
		}
		l = lv_label_create(o);
		lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_style_text_font(l, ui_font_s, 0);
		lv_obj_set_style_text_color(l, lv_color_white(), 0);
		lv_obj_set_style_bg_color(l, lv_color_black(), 0);
		lv_obj_set_style_bg_opa(l, LV_OPA_60, 0);
		lv_obj_set_style_radius(l, 4, 0);
		lv_obj_set_style_pad_hor(l, 6, 0);
		lv_obj_set_style_pad_ver(l, 1, 0);
		lv_label_set_text(l, buf);
		lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
		lv_obj_update_layout(l);
		lv_obj_set_pos(l, CARD_W - lv_obj_get_width(l) - 6, COVER_H - lv_obj_get_height(l) - 6);
	}

	l = text(o, ui_font_m, C_TEXT, 8, COVER_H + 8, CARD_W - 16, 2);
	lv_label_set_text(l, v->title);
	/* the uploader on the left (cut short when long), the number of views on the right */
	fmt_count(views, sizeof(views), v->views);
	snprintf(buf, sizeof(buf), "%s\xe6\x92\xad\xe6\x94\xbe", views);				/* bo fang */
	l = text(o, ui_font_s, C_DIM, CARD_W - 8 - 92, COVER_H + 8 + title_h + 4, 92, 1);
	lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
	lv_label_set_text(l, buf);
	l = text(o, ui_font_s, C_DIM, 8, COVER_H + 8 + title_h + 4, CARD_W - 16 - 100, 1);
	lv_label_set_text(l, v->author);
}

static void card_drop_cover(struct ui_card *c)
{
	if (c->has_cover) {
		lv_image_cache_drop(&c->dsc);
		lv_image_set_src(c->img, NULL);
		cover_release(c->pic);
		c->has_cover = false;
	}
}

void ui_grid_drop_covers(struct ui_grid *g)
{
	for (int i = 0; i < g->built; i++) {
		if (g->cards[i].obj != NULL) {
			card_drop_cover(&g->cards[i]);
		}
	}
}

/* the widgets of a card are deleted (the cover goes back to the cache) */
static void card_destroy(struct ui_card *c)
{
	if (c->obj == NULL) {
		return;
	}
	card_drop_cover(c);
	lv_obj_delete(c->obj);
	c->obj = NULL;
	c->img = NULL;
}

/* the first and the last row that have to have widgets: the screen and a card's height around it */
static void window_rows(struct ui_grid *g, int *first, int *last)
{
	int row_h = ui_card_height() + MARGIN;
	int top = lv_obj_get_scroll_y(g->obj), vh = lv_obj_get_height(g->obj);
	int rows = (g->built + CARD_COLS - 1) / CARD_COLS;

	*first = MAX(0, (top - row_h) / row_h);
	*last = MIN(rows - 1, (top + vh + row_h) / row_h);
}

static void scrolled(lv_event_t *e)
{
	struct ui_grid *g = lv_event_get_user_data(e);

	g->covers_dirty = true;
}

void ui_grid_create(struct ui_grid *g, lv_obj_t *parent, enum list_id list, int top, int height)
{
	memset(g, 0, sizeof(*g));
	g->list = list;
	g->asked_at = -1;
	g->obj = lv_obj_create(parent);
	lv_obj_set_pos(g->obj, 0, top);
	lv_obj_set_size(g->obj, SCREEN_W, height);
	lv_obj_set_style_bg_opa(g->obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(g->obj, 0, 0);
	lv_obj_set_style_radius(g->obj, 0, 0);
	lv_obj_set_style_pad_hor(g->obj, MARGIN, 0);
	lv_obj_set_style_pad_top(g->obj, 4, 0);
	lv_obj_set_style_pad_bottom(g->obj, MARGIN, 0);
	lv_obj_set_style_pad_row(g->obj, MARGIN, 0);
	lv_obj_set_style_pad_column(g->obj, MARGIN, 0);
	lv_obj_set_scroll_dir(g->obj, LV_DIR_VER);
	lv_obj_add_event_cb(g->obj, scrolled, LV_EVENT_SCROLL, g);

	g->footer = lv_label_create(g->obj);
	lv_obj_set_size(g->footer, lv_pct(100), 40);
	lv_obj_set_style_text_font(g->footer, ui_font_m, 0);
	lv_obj_set_style_text_color(g->footer, C_DIM, 0);
	lv_obj_set_style_text_align(g->footer, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(g->footer, "");
}

bool ui_grid_sync(struct ui_grid *g)
{
	struct list_info info;
	bool changed = false;
	const char *foot;
	int first, last, count, rows;

	app_list_info(g->list, &info);
	if (info.gen != g->gen) {
		/* the list was replaced */
		for (int i = 0; i < g->built; i++) {
			card_destroy(&g->cards[i]);
		}
		g->built = 0;
		g->gen = info.gen;
		g->asked_at = -1;
		lv_obj_scroll_to_y(g->obj, 0, LV_ANIM_OFF);
		changed = true;
	}
	count = MIN(info.count, MAX_VIDEOS);
	if (count != g->built) {
		g->built = count;
		changed = true;
	}
	rows = (g->built + CARD_COLS - 1) / CARD_COLS;
	lv_obj_set_y(g->footer, rows * (ui_card_height() + MARGIN));

	/* widgets for the rows near the screen, none for the rest */
	window_rows(g, &first, &last);
	for (int i = 0; i < g->built; i++) {
		int row = i / CARD_COLS;
		struct ui_card *c = &g->cards[i];

		if (row >= first && row <= last) {
			struct bili_video v;

			if (c->obj == NULL && app_video(g->list, i, &v)) {
				card_build(g, i, &v);
				changed = true;
			}
		} else if (c->obj != NULL && (row < first - 1 || row > last + 1)) {
			card_destroy(c);
			changed = true;
		}
	}
	if (changed) {
		g->covers_dirty = true;
	}
	foot = info.loading ? "\xe5\x8a\xa0\xe8\xbd\xbd\xe4\xb8\xad..." :		/* loading */
	       (!info.more && info.count > 0 ? "\xe6\xb2\xa1\xe6\x9c\x89\xe6\x9b\xb4\xe5\xa4\x9a\xe4\xba\x86" : "");
	if (strcmp(lv_label_get_text(g->footer), foot) != 0) {
		lv_label_set_text(g->footer, foot);
	}

	return changed;
}

void ui_grid_covers(struct ui_grid *g, bool visible)
{
	int margin = ui_card_height();
	int row_h = ui_card_height() + MARGIN;
	int top, vh;

	if (!visible) {
		ui_grid_drop_covers(g);
		return;
	}
	top = lv_obj_get_scroll_y(g->obj);
	vh = lv_obj_get_height(g->obj);
	for (int i = 0; i < g->built; i++) {
		struct ui_card *c = &g->cards[i];
		int y1, y2;
		bool near;

		if (c->obj == NULL) {
			continue;
		}
		y1 = (i / CARD_COLS) * row_h;
		y2 = y1 + ui_card_height();
		near = y2 >= top - margin && y1 <= top + vh + margin;
		if (near && !c->has_cover) {
			const uint16_t *px;

			cover_request(c->pic);
			px = cover_acquire(c->pic);
			if (px != NULL) {
				c->dsc = (lv_image_dsc_t){
					.header = {
						.magic = LV_IMAGE_HEADER_MAGIC,
						.cf = LV_COLOR_FORMAT_RGB565,
						.w = COVER_W,
						.h = COVER_H,
						.stride = COVER_STRIDE,
					},
					.data_size = COVER_BYTES,
					.data = (const uint8_t *)px,
				};
				lv_image_set_src(c->img, &c->dsc);
				c->has_cover = true;
			}
		} else if (!near) {
			card_drop_cover(c);
		}
	}
	g->cover_seen = cover_version();
	g->covers_dirty = false;
}

void ui_grid_more(struct ui_grid *g)
{
	struct list_info info;

	app_list_info(g->list, &info);
	if (!info.more || info.loading || info.count == 0 || g->asked_at == info.count) {
		return;
	}
	if (lv_obj_get_scroll_bottom(g->obj) < 2 * ui_card_height()) {
		g->asked_at = info.count;
		app_cmd(CMD_MORE, g->list);
	}
}

void ui_grid_scroll_top(struct ui_grid *g)
{
	lv_obj_scroll_to_y(g->obj, 0, LV_ANIM_OFF);
}
