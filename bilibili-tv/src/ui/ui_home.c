/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The home page: title, search field, refresh, the tabs "recommend", "popular" and (with a login) "following" with a card
 * grid each, and the buttons of the account and of the settings at the right end of the tab row.
 */

#include <zephyr/sys/printk.h>

#include "ui/ui_priv.h"

#define BAR_H	72
#define TABS_H	52
#define STATUS_H 30

static lv_obj_t *page;
#define LIST_TABS	3	/* recommend, popular, following: a card grid each */
#define TAB_ME		3	/* at the right end of the row, with the settings */
#define TAB_SETTINGS	4
#define TABS		5

static lv_obj_t *tab_btn[TABS];
static lv_obj_t *panel[TABS];		/* the account and settings panels, NULL for the list tabs */
static struct ui_grid grids[LIST_TABS];
static int tab;
static bool popular_asked, follow_asked, logged;

/* the grid of the list tab that is shown (the recommend grid while a panel is) */
static struct ui_grid *active(void)
{
	return &grids[tab < LIST_TABS ? tab : 0];
}

static void show_tab(int t)
{
	tab = t;
	for (int i = 0; i < TABS; i++) {
		bool on = i == t;
		lv_obj_t *content = i < LIST_TABS ? grids[i].obj : panel[i];

		if (on) {
			lv_obj_remove_flag(content, LV_OBJ_FLAG_HIDDEN);
		} else {
			lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN);
			if (i < LIST_TABS) {
				ui_grid_drop_covers(&grids[i]);
			}
		}
		lv_obj_set_style_text_color(lv_obj_get_child(tab_btn[i], 0), on ? C_ACCENT : C_DIM, 0);
		lv_obj_set_style_border_width(tab_btn[i], on ? 3 : 0, 0);
	}
	if (t == 1 && !popular_asked) {
		popular_asked = true;
		app_cmd(CMD_REFRESH, LIST_POPULAR);
	}
	if (t == 2 && !follow_asked) {
		follow_asked = true;
		app_cmd(CMD_REFRESH, LIST_FOLLOW);
	}
	if (t == TAB_ME) {
		ui_me_show();
	} else if (t == TAB_SETTINGS) {
		ui_settings_show();
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
	if (tab < LIST_TABS) {
		app_cmd(CMD_REFRESH, active()->list);
	}
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

lv_obj_t *ui_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int x, int y)
{
	lv_obj_t *l = lv_label_create(parent);

	lv_obj_set_style_text_font(l, font, 0);
	lv_obj_set_style_text_color(l, color, 0);
	lv_obj_set_pos(l, x, y);

	return l;
}

void ui_keyboard_style(lv_obj_t *kb)
{
	/* the keys of every state: the theme of LVGL has its own light colours for pressed and checked ones */
	lv_obj_set_style_bg_color(kb, C_BAR, 0);
	lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(kb, 0, 0);
	lv_obj_set_style_radius(kb, 0, 0);
	lv_obj_set_style_pad_all(kb, 8, 0);
	lv_obj_set_style_pad_gap(kb, 8, 0);
	lv_obj_set_style_text_font(kb, &lv_font_montserrat_28, LV_PART_ITEMS);
	lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
	lv_obj_set_style_bg_color(kb, lv_color_hex(0x26303c), LV_PART_ITEMS);
	lv_obj_set_style_text_color(kb, C_TEXT, LV_PART_ITEMS);
	lv_obj_set_style_radius(kb, 10, LV_PART_ITEMS);
	lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
	lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
	lv_obj_set_style_bg_color(kb, C_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
	lv_obj_set_style_text_color(kb, lv_color_white(), LV_PART_ITEMS | LV_STATE_PRESSED);
	/* the keys that change the mode or end the input */
	lv_obj_set_style_bg_color(kb, lv_color_hex(0x3a4856), LV_PART_ITEMS | LV_STATE_CHECKED);
	lv_obj_set_style_text_color(kb, lv_color_white(), LV_PART_ITEMS | LV_STATE_CHECKED);
	lv_obj_set_style_bg_color(kb, C_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED | LV_STATE_PRESSED);
	lv_obj_set_style_bg_color(kb, lv_color_hex(0x1c232c), LV_PART_ITEMS | LV_STATE_DISABLED);
	lv_obj_set_style_text_color(kb, C_DIM, LV_PART_ITEMS | LV_STATE_DISABLED);
	lv_obj_set_style_outline_width(kb, 0, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
	lv_obj_set_style_outline_width(kb, 0, LV_PART_ITEMS | LV_STATE_FOCUSED);
}

lv_obj_t *ui_card_panel(lv_obj_t *parent, int x, int y, int w, int h)
{
	lv_obj_t *c = lv_obj_create(parent);

	lv_obj_set_pos(c, x, y);
	lv_obj_set_size(c, w, h);
	lv_obj_set_style_bg_color(c, C_CARD, 0);
	lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(c, 0, 0);
	lv_obj_set_style_radius(c, 16, 0);
	lv_obj_set_style_pad_all(c, 0, 0);
	lv_obj_set_style_shadow_width(c, 0, 0);
	lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);

	return c;
}

lv_obj_t *ui_home_create(lv_obj_t *parent)
{
	lv_obj_t *bar, *l, *b;
	static const char *const tabs[TABS] = {"\xe6\x8e\xa8\xe8\x8d\x90", "\xe7\x83\xad\xe9\x97\xa8",
					       "\xe5\x85\xb3\xe6\xb3\xa8", "\xe6\x88\x91\xe7\x9a\x84",
					       "\xe8\xae\xbe\xe7\xbd\xae"};	/* recommend, popular, following, mine, settings */

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

	for (int i = 0; i < TABS; i++) {
		lv_obj_t *t = lv_button_create(page);
		int w = i < LIST_TABS ? 160 : 120;
		int x = i < LIST_TABS ? MARGIN + i * 168 : SCREEN_W - MARGIN - (TABS - i) * 128 + 8;

		tab_btn[i] = t;
		lv_obj_set_size(t, w, TABS_H);
		lv_obj_set_pos(t, x, BAR_H);
		lv_obj_set_style_bg_opa(t, LV_OPA_TRANSP, 0);
		lv_obj_set_style_shadow_width(t, 0, 0);
		lv_obj_set_style_border_side(t, LV_BORDER_SIDE_BOTTOM, 0);
		lv_obj_set_style_border_color(t, C_ACCENT, 0);
		lv_obj_set_style_radius(t, 0, 0);
		l = label(t, ui_font_m, C_DIM, tabs[i]);
		lv_obj_center(l);
		lv_obj_add_event_cb(t, tab_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
	}
	for (int i = 0; i < LIST_TABS; i++) {
		static const enum list_id ids[LIST_TABS] = {LIST_RECOMMEND, LIST_POPULAR, LIST_FOLLOW};

		ui_grid_create(&grids[i], page, ids[i], BAR_H + TABS_H, SCREEN_H - BAR_H - TABS_H - STATUS_H);
	}
	panel[TAB_ME] = ui_me_create(page, BAR_H + TABS_H, SCREEN_H - BAR_H - TABS_H - STATUS_H);
	panel[TAB_SETTINGS] = ui_settings_create(page, page, BAR_H + TABS_H, SCREEN_H - BAR_H - TABS_H - STATUS_H);
	lv_obj_add_flag(tab_btn[2], LV_OBJ_FLAG_HIDDEN);	/* until there is a login */
	show_tab(0);

	return page;
}

void ui_home_show(void)
{
	active()->covers_dirty = true;
}

void ui_home_run(bool visible)
{
	struct ui_grid *g;
	bool now = app_account(NULL, 0);

	/* the tab of the followed users exists while there is a login */
	if (now != logged) {
		logged = now;
		follow_asked = false;
		if (logged) {
			lv_obj_remove_flag(tab_btn[2], LV_OBJ_FLAG_HIDDEN);
		} else {
			lv_obj_add_flag(tab_btn[2], LV_OBJ_FLAG_HIDDEN);
			if (tab == 2) {
				show_tab(0);
			}
		}
	}
	g = active();
	if (!visible || tab >= LIST_TABS) {
		for (int i = 0; i < LIST_TABS; i++) {
			ui_grid_covers(&grids[i], false);
		}
		ui_me_run(visible && tab == TAB_ME);
		ui_settings_run(visible && tab == TAB_SETTINGS);
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
