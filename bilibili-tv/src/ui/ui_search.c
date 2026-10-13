/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The search page: a text field with an on-screen keyboard and the pinyin input method, a row of
 * trending words to tap, and the grid of the results.
 */

#include <stdio.h>
#include <string.h>

#include "ui/pinyin_dict.h"
#include "ui/ui_priv.h"

#define BAR_H		72
#define STATUS_H	30
#define KB_H		300

static lv_obj_t *page, *ta, *kb, *ime, *hot_box, *hot_title;
static struct ui_grid grid;
static bool searched;

/* key presses and candidate picks asked for by the shell, played by the UI thread */
static char key_req[64];
static volatile int pick_req = -1;
static volatile bool submit_req;
static bool hot_asked;

static void kb_show(bool on)
{
	lv_obj_t *cand = lv_ime_pinyin_get_cand_panel(ime);

	if (on) {
		lv_keyboard_set_textarea(kb, ta);
		lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(cand, LV_OBJ_FLAG_HIDDEN);
	}
}

static void submit(void)
{
	char kw[96];
	const char *t = lv_textarea_get_text(ta);
	size_t n = strlen(t);

	while (n > 0 && t[n - 1] == ' ') {
		n--;
	}
	while (n > 0 && *t == ' ') {
		t++;
		n--;
	}
	if (n == 0 || n >= sizeof(kw)) {
		return;
	}
	memcpy(kw, t, n);
	kw[n] = '\0';
	searched = true;
	lv_obj_add_flag(hot_box, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(hot_title, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(grid.obj, LV_OBJ_FLAG_HIDDEN);
	kb_show(false);
	app_cmd_search(kw);
}

static void kb_event(lv_event_t *e)
{
	if (lv_event_get_code(e) == LV_EVENT_READY) {
		submit();
	} else if (lv_event_get_code(e) == LV_EVENT_CANCEL) {
		kb_show(false);
	}
}

static void ta_clicked(lv_event_t *e)
{
	kb_show(true);
}

static void back_clicked(lv_event_t *e)
{
	kb_show(false);
	ui_nav(PAGE_HOME);
}

static void go_clicked(lv_event_t *e)
{
	submit();
}

static void chip_clicked(lv_event_t *e)
{
	lv_obj_t *l = lv_obj_get_child(lv_event_get_target(e), 0);

	lv_textarea_set_text(ta, lv_label_get_text(l));
	submit();
}

void ui_search_hot_words(char words[][BILI_HOT_WORD_LEN], int count)
{
	lv_obj_clean(hot_box);
	for (int i = 0; i < count; i++) {
		lv_obj_t *b = ui_button(hot_box, words[i], chip_clicked, NULL);

		lv_obj_set_height(b, 44);
		lv_obj_set_style_radius(b, 22, 0);
	}
}

lv_obj_t *ui_search_create(lv_obj_t *parent)
{
	lv_obj_t *bar, *b;

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

	b = ui_button(bar, "\xe8\xbf\x94\xe5\x9b\x9e", back_clicked, NULL);			/* back */
	ui_button_size(b, 120, 48);
	lv_obj_align(b, LV_ALIGN_LEFT_MID, MARGIN, 0);

	ta = lv_textarea_create(bar);
	lv_textarea_set_one_line(ta, true);
	lv_textarea_set_max_length(ta, 30);
	lv_textarea_set_placeholder_text(ta, "\xe8\xbe\x93\xe5\x85\xa5\xe5\x85\xb3\xe9\x94\xae\xe8\xaf\x8d...");	/* type a keyword */
	lv_obj_set_style_text_font(ta, ui_font_m, 0);
	lv_obj_set_style_text_color(ta, C_TEXT, 0);
	lv_obj_set_style_bg_color(ta, C_CARD, 0);
	lv_obj_set_style_border_color(ta, C_ACCENT, 0);
	lv_obj_set_size(ta, 560, 52);
	lv_obj_align(ta, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_event_cb(ta, ta_clicked, LV_EVENT_CLICKED, NULL);

	b = ui_button(bar, "\xe6\x90\x9c\xe7\xb4\xa2", go_clicked, NULL);			/* search */
	ui_button_size(b, 120, 48);
	lv_obj_set_style_bg_color(b, C_ACCENT, 0);
	lv_obj_align(b, LV_ALIGN_RIGHT_MID, -MARGIN, 0);

	hot_title = lv_label_create(page);
	lv_obj_set_style_text_font(hot_title, ui_font_m, 0);
	lv_obj_set_style_text_color(hot_title, C_DIM, 0);
	lv_label_set_text(hot_title, "\xe7\x83\xad\xe6\x90\x9c");				/* trending */
	lv_obj_set_pos(hot_title, MARGIN, BAR_H + 16);

	hot_box = lv_obj_create(page);
	lv_obj_set_pos(hot_box, 0, BAR_H + 56);
	lv_obj_set_size(hot_box, SCREEN_W, 200);
	lv_obj_set_style_bg_opa(hot_box, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(hot_box, 0, 0);
	lv_obj_set_style_pad_hor(hot_box, MARGIN, 0);
	lv_obj_set_style_pad_gap(hot_box, 12, 0);
	lv_obj_set_flex_flow(hot_box, LV_FLEX_FLOW_ROW_WRAP);
	lv_obj_remove_flag(hot_box, LV_OBJ_FLAG_SCROLLABLE);

	ui_grid_create(&grid, page, LIST_SEARCH, BAR_H, SCREEN_H - BAR_H - STATUS_H);
	lv_obj_add_flag(grid.obj, LV_OBJ_FLAG_HIDDEN);

	kb = lv_keyboard_create(page);
	lv_obj_set_size(kb, SCREEN_W, KB_H);
	lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_obj_set_style_text_font(kb, &lv_font_montserrat_28, LV_PART_ITEMS);
	lv_obj_set_style_bg_color(kb, C_BAR, 0);
	lv_obj_set_style_bg_color(kb, C_CARD, LV_PART_ITEMS);
	lv_obj_set_style_text_color(kb, C_TEXT, LV_PART_ITEMS);
	lv_obj_add_event_cb(kb, kb_event, LV_EVENT_ALL, NULL);

	ime = lv_ime_pinyin_create(page);
	lv_obj_set_style_text_font(ime, ui_font_m, 0);
	lv_ime_pinyin_set_dict(ime, (lv_pinyin_dict_t *)bili_pinyin_dict);
	lv_ime_pinyin_set_keyboard(ime, kb);
	{
		lv_obj_t *cand = lv_ime_pinyin_get_cand_panel(ime);

		lv_obj_set_size(cand, SCREEN_W, 64);
		lv_obj_set_style_bg_color(cand, C_BAR, 0);
		lv_obj_set_style_bg_opa(cand, LV_OPA_COVER, 0);
		lv_obj_set_style_pad_all(cand, 6, 0);
		/* the candidates are the buttons (items) of the matrix: they need their own colours */
		lv_obj_set_style_text_font(cand, ui_font_l, LV_PART_ITEMS);
		lv_obj_set_style_text_color(cand, C_TEXT, LV_PART_ITEMS);
		lv_obj_set_style_bg_color(cand, C_CARD, LV_PART_ITEMS);
		lv_obj_set_style_bg_opa(cand, LV_OPA_COVER, LV_PART_ITEMS);
		lv_obj_set_style_bg_color(cand, C_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
		lv_obj_set_style_radius(cand, 8, LV_PART_ITEMS);
		lv_obj_set_style_border_width(cand, 0, LV_PART_ITEMS);
		lv_obj_set_style_shadow_width(cand, 0, LV_PART_ITEMS);
		lv_obj_align_to(cand, kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
	}
	lv_keyboard_set_textarea(kb, ta);

	return page;
}

void ui_search_show(void)
{
	if (!searched) {
		kb_show(true);
	}
	if (!hot_asked) {
		hot_asked = true;
		app_cmd(CMD_HOT, 0);
	}
	grid.covers_dirty = true;
}

static void inject_run(void);

void ui_search_run(bool visible)
{
	if (visible) {
		inject_run();
	}
	if (!visible) {
		ui_grid_covers(&grid, false);
		return;
	}
	if (!searched) {
		return;
	}
	if (ui_grid_sync(&grid) || grid.covers_dirty || grid.cover_seen != cover_version()) {
		ui_grid_covers(&grid, true);
	}
	ui_grid_more(&grid);
}

/* presses the key with this label of the on-screen keyboard, as a finger would */
static void press(const char *label)
{
	uint32_t n = 0;

	/* the number of buttons: the map ends with an empty string */
	for (const char *const *m = lv_keyboard_get_map_array(kb); m[n][0] != '\0'; n++) {
	}
	for (uint32_t i = 0, b = 0; i < n; i++) {
		const char *s = lv_keyboard_get_map_array(kb)[i];

		if (s[0] == '\n') {
			continue;
		}
		if (strcmp(s, label) == 0) {
			lv_buttonmatrix_set_selected_button(kb, b);
			lv_obj_send_event(kb, LV_EVENT_VALUE_CHANGED, NULL);
			return;
		}
		b++;
	}
}

void ui_search_inject(const char *keys, int pick, bool submit_now)
{
	if (keys != NULL) {
		snprintf(key_req, sizeof(key_req), "%s", keys);
	}
	pick_req = pick;
	submit_req = submit_now;
}

static void inject_run(void)
{
	if (key_req[0] != '\0') {
		char one[2] = {0, 0};

		kb_show(true);
		for (const char *k = key_req; *k != '\0'; k++) {
			one[0] = *k;
			press(one);
		}
		key_req[0] = '\0';
	}
	if (pick_req >= 0) {
		lv_obj_t *cand = lv_ime_pinyin_get_cand_panel(ime);

		/* button 0 is "<", then the candidates */
		lv_buttonmatrix_set_selected_button(cand, 1 + pick_req);
		lv_obj_send_event(cand, LV_EVENT_VALUE_CHANGED, NULL);
		pick_req = -1;
	}
	if (submit_req) {
		submit_req = false;
		submit();
	}
}
