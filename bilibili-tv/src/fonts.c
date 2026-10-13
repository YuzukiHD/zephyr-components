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

#include "ui_priv.h"

/* glyphs kept per font size: the bitmaps of a screen full of titles */
#define GLYPH_CACHE	384

static const uint8_t ttf[] = {
#include "font.inc"
};

const lv_font_t *ui_font_s, *ui_font_m, *ui_font_l;

static const lv_font_t *make(int px)
{
	lv_font_t *f = lv_tiny_ttf_create_data_ex(ttf, sizeof(ttf), px, LV_FONT_KERNING_NONE,
						  GLYPH_CACHE);

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
	ui_font_s = make(18);
	ui_font_m = make(22);
	ui_font_l = make(28);
	printk("ui: font of %u KiB\n", (unsigned int)(sizeof(ttf) >> 10));
}
