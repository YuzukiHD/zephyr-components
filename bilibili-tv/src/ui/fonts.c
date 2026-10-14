/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The text font: Noto Sans SC (SIL OFL), cut down to the GB2312 characters, the kana and the
 * punctuation by tools/make_font.py and linked into the firmware. TinyTTF draws the glyphs from it
 * on demand; the font data itself is read in place.
 */

#include <lvgl.h>
#include <zephyr/sys/printk.h>

#include "ui/ui_priv.h"

/*
 * Glyphs kept per font size. The cache is counted in glyphs, not in bytes, and a full one is
 * 300..800 bytes per glyph: with 384 for each size the LVGL heap filled up while titles and
 * comments brought new characters (about 15 KiB more after every video). The titles of a screen
 * need about 150 distinct glyphs of the middle size, the other sizes only a few.
 */
#define GLYPH_CACHE_S	160
#define GLYPH_CACHE_M	320
#define GLYPH_CACHE_L	64

static const uint8_t ttf[] = {
#include "font.inc"
};

const lv_font_t *ui_font_s, *ui_font_m, *ui_font_l;

static const lv_font_t *make(int px, int cache)
{
	lv_font_t *f = lv_tiny_ttf_create_data_ex(ttf, sizeof(ttf), px, LV_FONT_KERNING_NONE,
						  cache);

	if (f == NULL) {
		printk("ui: the font %d px could not be made\n", px);
		return &lv_font_montserrat_20;
	}
	/* the symbols of LVGL (arrows, play, ...) are not in the font */
	f->fallback = &lv_font_montserrat_20;

	return f;
}

void ui_fonts_init(void)
{
	ui_font_s = make(18, GLYPH_CACHE_S);
	ui_font_m = make(22, GLYPH_CACHE_M);
	ui_font_l = make(28, GLYPH_CACHE_L);
	printk("ui: font of %u KiB\n", (unsigned int)(sizeof(ttf) >> 10));
}
