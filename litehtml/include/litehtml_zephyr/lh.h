/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Draw an HTML page with litehtml into an RGB565 buffer
 *
 * Text is drawn with a fixed width bitmap font scaled to the CSS font size. Images, gradients
 * and rounded corners are not drawn, borders are solid.
 */

#ifndef LITEHTML_ZEPHYR_LH_H_
#define LITEHTML_ZEPHYR_LH_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A bitmap font: glyphs for the characters first..last, each one cell_w columns of two bytes
 * (column major, the least significant bit is the top row), like the character frame buffer
 * fonts of Zephyr (cfb_font_1016: 10 x 16).
 */
struct lh_font {
	const uint8_t (*glyphs)[20];
	uint8_t first;
	uint8_t last;
	uint8_t cell_w;
	uint8_t cell_h;
};

struct lh_page;

/**
 * Parse and lay out a page for a viewport of the given width.
 *
 * @param font   Bitmap font, must stay valid while the page exists
 * @param html   UTF-8 text of the page
 * @param width  Viewport width in pixels
 * @param height Viewport height in pixels (for media queries and vh units)
 * @return The page, NULL on failure
 */
struct lh_page *lh_load(const struct lh_font *font, const char *html, int width, int height);

/** Height of the laid out page in pixels */
int lh_content_height(const struct lh_page *page);

/** Text of the <title> element, empty when there is none */
const char *lh_title(const struct lh_page *page);

/**
 * Draw the part of the page that starts scroll_y pixels from the top into a frame.
 *
 * @param fb      RGB565 pixels
 * @param stride  Pixels from one row to the next
 * @param w       Width of the area to draw
 * @param h       Height of the area to draw
 * @param scroll_y Page row shown at the top of the area
 * @param bg      Colour of the area behind the page (RGB565)
 */
void lh_draw(struct lh_page *page, uint16_t *fb, int stride, int w, int h, int scroll_y,
	     uint16_t bg);

void lh_free(struct lh_page *page);

#ifdef __cplusplus
}
#endif

#endif /* LITEHTML_ZEPHYR_LH_H_ */
