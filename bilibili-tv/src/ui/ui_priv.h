/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* What the parts of the user interface share. Everything here runs in the thread that owns LVGL. */

#ifndef BILI_UI_PRIV_H_
#define BILI_UI_PRIV_H_

#include <lvgl.h>

#include "core/app.h"
#include "ui/cover.h"
#include "api/danmaku.h"

#define SCREEN_W	1024
#define SCREEN_H	768
#define MARGIN		16

#define C_BG		lv_color_hex(0x0b0f14)
#define C_BAR		lv_color_hex(0x141a21)
#define C_CARD		lv_color_hex(0x1c232c)
#define C_CARD_PRESS	lv_color_hex(0x2a3643)
#define C_ACCENT	lv_color_hex(0x00a1d6)
#define C_TEXT		lv_color_hex(0xe8edf2)
#define C_DIM		lv_color_hex(0x8a97a6)

/* Chinese capable fonts, cut from the embedded TrueType (fonts.c) */
extern const lv_font_t *ui_font_s, *ui_font_m, *ui_font_l;
extern const lv_image_dsc_t ui_logo;
void ui_fonts_init(void);

/* ---- a grid of video cards (ui_card.c) ---------------------------------------------------- */

#define CARD_W		236
#define CARD_COLS	4

struct ui_card {
	lv_obj_t *obj;
	lv_obj_t *img;
	lv_image_dsc_t dsc;
	char pic[160];
	bool has_cover;
};

struct ui_grid {
	lv_obj_t *obj;			/**< the scrolling container */
	lv_obj_t *footer;		/**< last child: "loading" / "no more" */
	enum list_id list;
	struct ui_card cards[MAX_VIDEOS];
	int built;
	uint32_t gen;
	uint32_t cover_seen;		/**< cover_version() at the last cover pass */
	bool covers_dirty;
	int asked_at;			/**< the list length when the next page was asked for */
};

void ui_grid_create(struct ui_grid *g, lv_obj_t *parent, enum list_id list, int top, int height);
/** Makes the cards match the list; true when something changed */
bool ui_grid_sync(struct ui_grid *g);
/** Gives the covers of far away cards back and asks for the ones that come into view */
void ui_grid_covers(struct ui_grid *g, bool visible);
/** Asks the worker for the next page when the end is near */
void ui_grid_more(struct ui_grid *g);
int ui_card_height(void);
void ui_grid_drop_covers(struct ui_grid *g);
void ui_grid_scroll_top(struct ui_grid *g);

/* ---- the pages ---------------------------------------------------------------------------- */

enum page {
	PAGE_HOME,
	PAGE_SEARCH,
	PAGE_PLAY,
	PAGE_COUNT,
};

void ui_nav(enum page page);
enum page ui_page(void);

lv_obj_t *ui_home_create(lv_obj_t *parent);
void ui_home_run(bool visible);
void ui_home_show(void);

lv_obj_t *ui_search_create(lv_obj_t *parent);
void ui_search_run(bool visible);
void ui_search_show(void);
void ui_search_inject(const char *keys, int pick, bool submit_now);
void ui_search_hot_words(char words[][BILI_HOT_WORD_LEN], int count);

/* The account and settings panels fill the area below the tabs of the home page */
lv_obj_t *ui_me_create(lv_obj_t *parent, int top, int height);
void ui_me_run(bool visible);
void ui_me_show(void);

/** @p overlay is the full screen parent for the keyboard and the password box */
lv_obj_t *ui_settings_create(lv_obj_t *parent, lv_obj_t *overlay, int top, int height);
void ui_settings_run(bool visible);
void ui_settings_show(void);

lv_obj_t *ui_danmaku_create(lv_obj_t *parent);
void ui_danmaku_set(struct dm_set *set);	/* takes the set over and frees the one before; NULL clears */
void ui_danmaku_run(uint32_t round_us);
void ui_danmaku_enable(bool on);
bool ui_danmaku_enabled(void);
bool ui_danmaku_active(void);

lv_obj_t *ui_play_create(lv_obj_t *parent);
void ui_play_set(const char *title, uint32_t duration_ms);
void ui_play_status(const char *text);
void ui_play_position(uint32_t pos_ms, bool paused);
void ui_play_buffered(int permille);

/** A rounded panel of the surface colour; returns it */
lv_obj_t *ui_card_panel(lv_obj_t *parent, int x, int y, int w, int h);
/** A label with a font and a colour at a position */
lv_obj_t *ui_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int x, int y);

/** A round button with a label; returns the button */
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user);
void ui_button_size(lv_obj_t *b, int w, int h);
void ui_set_status_label(const char *text);

#endif /* BILI_UI_PRIV_H_ */
