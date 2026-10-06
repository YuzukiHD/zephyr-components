/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * A page that does not fit the memory as a whole: the file is read once to find its pieces
 * (lh_index_scan), and only the piece that is on the screen and the one after it are laid out.
 * Every piece is a page of its own, made of the style sheets of the head, the <body> tag and the
 * text of the piece. The page scrolls by itself, from the first piece to the last and then
 * again.
 */

#include <stdlib.h>
#include <string.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <litehtml_zephyr/lh.h>

#include "chunked.h"

#define W 1024
#define H 600
#define BG 0xffff

static struct lh_index ix;
static struct fs_file_t file;
static char *css;
static size_t css_len;

struct piece {
	struct lh_page *page;
	int idx;
	int height;
};

static int read_cb(void *user, uint32_t off, void *buf, uint32_t len)
{
	ARG_UNUSED(user);
	if (fs_seek(&file, off, FS_SEEK_SET) != 0) {
		return -1;
	}
	return fs_read(&file, buf, len) == (ssize_t)len ? 0 : -1;
}

static bool read_css(void)
{
	size_t total = 0;

	for (int i = 0; i < ix.n_css; i++) {
		total += ix.css[i].length;
	}
	css = malloc(total + 1);
	if (css == NULL) {
		return false;
	}
	for (int i = 0; i < ix.n_css; i++) {
		if (read_cb(NULL, ix.css[i].offset, css + css_len, ix.css[i].length) != 0) {
			return false;
		}
		css_len += ix.css[i].length;
	}
	css[css_len] = '\0';
	return true;
}

/* the text of piece idx as a page of its own */
static char *piece_text(int idx)
{
	static const char head[] = "<html><head><meta charset=\"utf-8\"><style>";
	static const char mid[] = "</style></head>";
	static const char tail[] = "</body></html>";
	const char *body = ix.body_tag[0] != '\0' ? ix.body_tag : "<body>";
	size_t n = ix.chunks[idx].length;
	size_t total = sizeof(head) + css_len + sizeof(mid) + strlen(body) + n + sizeof(tail);
	char *t = malloc(total), *p;

	if (t == NULL) {
		return NULL;
	}
	p = t;
	memcpy(p, head, sizeof(head) - 1);
	p += sizeof(head) - 1;
	memcpy(p, css, css_len);
	p += css_len;
	memcpy(p, mid, sizeof(mid) - 1);
	p += sizeof(mid) - 1;
	memcpy(p, body, strlen(body));
	p += strlen(body);
	if (read_cb(NULL, ix.chunks[idx].offset, p, n) != 0) {
		free(t);
		return NULL;
	}
	p += n;
	memcpy(p, tail, sizeof(tail));
	return t;
}

static bool load_piece(const struct lh_font *font, int idx, struct piece *pc)
{
	int64_t t0 = k_uptime_get();
	char *text = piece_text(idx);

	if (text == NULL) {
		printk("chunk %d: no memory for the text\n", idx);
		return false;
	}
	pc->page = lh_load(font, text, W, H);
	free(text);
	if (pc->page == NULL) {
		printk("chunk %d: lh_load failed\n", idx);
		return false;
	}
	pc->idx = idx;
	pc->height = lh_content_height(pc->page);
	printk("chunk %d/%d: %u bytes, %d px high, loaded in %d ms\n", idx, ix.n_chunks,
	       (unsigned int)ix.chunks[idx].length, pc->height, (int)(k_uptime_get() - t0));
	return true;
}

static void drop_piece(struct piece *pc)
{
	if (pc->page != NULL) {
		lh_free(pc->page);
		pc->page = NULL;
	}
}

int chunked_run(const char *path, const struct lh_font *font, const struct device *disp,
		uint16_t *fbs[2])
{
	struct fs_dirent st;
	struct piece cur = {0}, next = {0};
	int64_t t0;
	int n, y = 0, shown = 0, cur_buf = 0;

	if (fs_stat(path, &st) != 0) {
		return -1;
	}
	fs_file_t_init(&file);
	if (fs_open(&file, path, FS_O_READ) != 0) {
		return -1;
	}
	t0 = k_uptime_get();
	n = lh_index_scan(read_cb, NULL, st.size, CONFIG_SAMPLE_HTML_CHUNK_KB * 1024U, &ix);
	if (n < 0 || !read_css()) {
		printk("chunked: cannot scan %s\n", path);
		return -1;
	}
	printk("chunked: %s, %u KiB in %d pieces of about %d KiB, scanned in %d ms, %u bytes of css\n",
	       path, (unsigned int)(st.size >> 10), n, CONFIG_SAMPLE_HTML_CHUNK_KB,
	       (int)(k_uptime_get() - t0), (unsigned int)css_len);

	if (!load_piece(font, 0, &cur)) {
		return -1;
	}
	while (true) {
		uint16_t *fb = fbs[cur_buf];
		struct display_sunxi_rgb img = {.data = fb, .width = W, .height = H, .stride = W * 2};

		/* the next piece is laid out before the end of this one is on the screen */
		if (next.page == NULL && cur.idx + 1 < n && y + 2 * H > cur.height) {
			if (!load_piece(font, cur.idx + 1, &next)) {
				return -1;
			}
		}

		t0 = k_uptime_get();
		lh_draw(cur.page, fb, W, W, H, y, BG);
		if (next.page != NULL && y + H > cur.height) {
			lh_draw_over(next.page, fb, W, W, H, y - cur.height);
		}
		sys_cache_data_flush_range(fb, W * H * sizeof(uint16_t));
		if ((shown++ % 60) == 0) {
			printk("chunked: piece %d y %d drawn in %d ms\n", cur.idx, y,
			       (int)(k_uptime_get() - t0));
		}
		if (display_sunxi_show_rgb(disp, &img) != 0) {
			printk("show_rgb failed\n");
			return -1;
		}
		cur_buf ^= 1;

		y += CONFIG_SAMPLE_HTML_SCROLL_PX;
		if (y >= cur.height) {
			/* the next piece is at the top now */
			y -= cur.height;
			drop_piece(&cur);
			if (next.page != NULL) {
				cur = next;
				next.page = NULL;
			} else {
				/* the end: start again */
				printk("chunked: the end of the page, starting again\n");
				k_sleep(K_SECONDS(2));
				y = 0;
				if (!load_piece(font, 0, &cur)) {
					return -1;
				}
			}
		}
	}
}
