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

#include <stdbool.h>
#include <stddef.h>
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
 * Where the images of a page come from. Called for every image of the page when it is laid
 * out (the size) and every time it is drawn (the pixels), with the address as it is written in
 * the page. Returns RGB565 pixels (the same pointer every time for one image, valid until
 * the next call that loads another image: a cache is up to the caller), NULL when there is
 * none. Without a loader images have no size and are not drawn.
 */
typedef const uint16_t *(*lh_image_fn)(void *user, const char *src, int *w, int *h);

void lh_set_image_loader(lh_image_fn fn, void *user);

/**
 * Glyphs for the characters that the bitmap font lacks: the content of a "LHF1" font file made
 * by tools/scrape_page.py (16 x 16 cells of alpha, full width). Has to stay valid.
 */
void lh_set_cjk_font(const void *data, size_t size);

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

/**
 * Like lh_draw() but the area is not cleared first: draws a second page into the same frame,
 * for the seam between two pieces of a big page.
 */
void lh_draw_over(struct lh_page *page, uint16_t *fb, int stride, int w, int h, int scroll_y);

void lh_free(struct lh_page *page);

/*
 * Big pages: a page that is too big for the memory is cut into pieces at the end of a child of
 * <body>; every piece is laid out and drawn on its own, with the style sheets of the head. The
 * markup has to be well formed (end tags for everything that is not void).
 */

#define LH_INDEX_MAX_CHUNKS 8192
#define LH_INDEX_MAX_CSS 16

struct lh_range {
	uint32_t offset;
	uint32_t length;
};

struct lh_index {
	int n_chunks;
	struct lh_range chunks[LH_INDEX_MAX_CHUNKS];
	/* the <style> elements of the head: where their text is */
	int n_css;
	struct lh_range css[LH_INDEX_MAX_CSS];
	/* the opening tag of <body> with its attributes, empty when the text has none */
	char body_tag[256];
};

/** Reads len bytes at offset into buf, returns 0 on success */
typedef int (*lh_read_fn)(void *user, uint32_t offset, void *buf, uint32_t len);

/**
 * Read a page once and find its pieces.
 *
 * @param target Bytes a piece should have at least (it ends at the first child of <body> that
 *               ends after that; one child that is bigger makes a bigger piece)
 * @return The number of pieces, negative when reading fails
 */
int lh_index_scan(lh_read_fn read, void *user, uint32_t size, uint32_t target,
		  struct lh_index *out);

#ifdef __cplusplus
}
#endif

#endif /* LITEHTML_ZEPHYR_LH_H_ */
