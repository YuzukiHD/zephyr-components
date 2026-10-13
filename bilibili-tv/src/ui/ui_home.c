/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The home page: title, search field, refresh, the tabs "recommend" and "popular" and a card grid each. */

#include <zephyr/sys/printk.h>

#include "ui/ui_priv.h"

#define BAR_H	72
#define TABS_H	52
#define STATUS_H 30

static lv_obj_t *page;
static lv_obj_t *tab_btn[2];
static struct ui_grid grids[2];	/* recommend, popular */
static int tab;
static bool popular_asked;

static struct ui_grid *active(void)
{
	return &grids[tab];
}

static void show_tab(int t)
{
	tab = t;
	for (int i = 0; i < 2; i++) {
		bool on = i == t;

		if (on) {
			lv_obj_remove_flag(grids[i].obj, LV_OBJ_FLAG_HIDDEN);
		} else {
			lv_obj_add_flag(grids[i].obj, LV_OBJ_FLAG_HIDDEN);
			ui_grid_drop_covers(&grids[i]);
		}
		lv_obj_set_style_text_color(lv_obj_get_child(tab_btn[i], 0), on ? C_ACCENT : C_DIM, 0);
		lv_obj_set_style_border_width(tab_btn[i], on ? 3 : 0, 0);
	}
	if (t == 1 && !popular_asked) {
		popular_asked = true;
		app_cmd(CMD_REFRESH, LIST_POPULAR);
	}
	active()->covers_dirty = true;
}

static void tab_clicked(lv_event_t *e)
{
	show_tab((int)(intptr_t)lv_event_get_user_data(e));
}

static void search_clicked(lv_event_t *e)
{
	ui_nav(PAGE_SEARCH);
}

static void refresh_clicked(lv_event_t *e)
{
	app_cmd(CMD_REFRESH, active()->list);
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
	lv_obj_t *l = lv_label_create(parent);

	lv_obj_set_style_text_font(l, font, 0);
	lv_obj_set_style_text_color(l, color, 0);
	lv_label_set_text(l, text);

	return l;
}

/* a button of a fixed size: the label fills it and centres its text, a label of content width got cut */
void ui_button_size(lv_obj_t *b, int w, int h)
{
	lv_obj_t *l = lv_obj_get_child(b, 0);

	lv_obj_set_size(b, w, h);
	lv_obj_set_style_pad_hor(b, 0, 0);
	lv_obj_set_width(l, lv_pct(100));
	lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
	lv_obj_center(l);
}

lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user)
{
	lv_obj_t *b = lv_button_create(parent), *l;

	lv_obj_set_style_bg_color(b, C_CARD, 0);
	lv_obj_set_style_bg_color(b, C_CARD_PRESS, LV_STATE_PRESSED);
	lv_obj_set_style_shadow_width(b, 0, 0);
	lv_obj_set_style_radius(b, 24, 0);
	lv_obj_set_style_pad_hor(b, 22, 0);
	l = label(b, ui_font_m, C_TEXT, text);
	lv_obj_center(l);
	lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);

	return b;
}

lv_obj_t *ui_home_create(lv_obj_t *parent)
{
	lv_obj_t *bar, *l, *b;
	static const char *const tabs[] = {"\xe6\x8e\xa8\xe8\x8d\x90", "\xe7\x83\xad\xe9\x97\xa8"};	/* recommend, popular */

	page = lv_obj_create(parent);
	lv_obj_set_size(page, SCREEN_W, SCREEN_H);
	lv_obj_set_pos(page, 0, 0);
	lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(page, 0, 0);
	lv_obj_set_style_pad_all(page, 0, 0);
	lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);

	bar = lv_obj_create(page);
	lv_obj_set_size(bar, SCREEN_W, BAR_H);
	lv_obj_set_pos(bar, 0, 0);
	lv_obj_set_style_bg_color(bar, C_BAR, 0);
	lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(bar, 0, 0);
	lv_obj_set_style_radius(bar, 0, 0);
	lv_obj_set_style_pad_all(bar, 0, 0);
	lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

	/* the blue mark (tools/make_logo.py) */
	l = lv_image_create(bar);
	lv_image_set_src(l, &ui_logo);
	lv_obj_align(l, LV_ALIGN_LEFT_MID, MARGIN, 0);

	b = ui_button(bar, "\xe6\x90\x9c\xe7\xb4\xa2\xe8\xa7\x86\xe9\xa2\x91...", search_clicked, NULL);	/* search videos */
	ui_button_size(b, 560, 48);
	lv_obj_set_style_text_color(lv_obj_get_child(b, 0), C_DIM, 0);
	lv_obj_align(b, LV_ALIGN_CENTER, 0, 0);

	b = ui_button(bar, "\xe5\x88\xb7\xe6\x96\xb0", refresh_clicked, NULL);			/* refresh */
	ui_button_size(b, 120, 48);
	lv_obj_align(b, LV_ALIGN_RIGHT_MID, -MARGIN, 0);

	for (int i = 0; i < 2; i++) {
		lv_obj_t *t = lv_button_create(page);

		tab_btn[i] = t;
		lv_obj_set_size(t, 160, TABS_H);
		lv_obj_set_pos(t, MARGIN + i * 168, BAR_H);
		lv_obj_set_style_bg_opa(t, LV_OPA_TRANSP, 0);
		lv_obj_set_style_shadow_width(t, 0, 0);
		lv_obj_set_style_border_side(t, LV_BORDER_SIDE_BOTTOM, 0);
		lv_obj_set_style_border_color(t, C_ACCENT, 0);
		lv_obj_set_style_radius(t, 0, 0);
		l = label(t, ui_font_m, C_DIM, tabs[i]);
		lv_obj_center(l);
		lv_obj_add_event_cb(t, tab_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
	}
	for (int i = 0; i < 2; i++) {
		ui_grid_create(&grids[i], page, i == 0 ? LIST_RECOMMEND : LIST_POPULAR, BAR_H + TABS_H,
			       SCREEN_H - BAR_H - TABS_H - STATUS_H);
	}
	show_tab(0);

	return page;
}

void ui_home_show(void)
{
	active()->covers_dirty = true;
}

void ui_home_run(bool visible)
{
	struct ui_grid *g = active();

	if (!visible) {
		for (int i = 0; i < 2; i++) {
			ui_grid_covers(&grids[i], false);
		}
		return;
	}
	if (ui_grid_sync(g) || g->covers_dirty || g->cover_seen != cover_version()) {
		ui_grid_covers(g, true);
	}
	ui_grid_more(g);
}

/* selects the tab 0 (recommend) or 1 (popular), as a tap on it does (the self test) */
void ui_home_tab(int t)
{
	show_tab(t);
}

/* scrolls the list by dy pixels (the self test); false when it cannot go any further that way */
bool ui_home_scroll(int dy)
{
	struct ui_grid *g = active();
	bool can = dy > 0 ? lv_obj_get_scroll_bottom(g->obj) > 0 : lv_obj_get_scroll_top(g->obj) > 0;

	if (can) {
		lv_obj_scroll_by(g->obj, 0, -dy, LV_ANIM_OFF);
	}

	return can;
}
