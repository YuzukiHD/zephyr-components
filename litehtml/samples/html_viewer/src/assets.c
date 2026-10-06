/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The pictures and the extra glyphs of a page made by tools/scrape_page.py: files next to the
 * page (img/*.lhi, font.lhf). The pictures are cached up to a limit and the oldest ones are
 * dropped.
 */

#include <stdlib.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <litehtml_zephyr/lh.h>

#include "assets.h"

#define CACHE_ENTRIES 40
#define CACHE_LIMIT (6U << 20)

struct entry {
	char name[96];
	uint16_t *px;
	int w, h;
	size_t size;
	uint32_t used;
};

static char dir[96];
static struct entry cache[CACHE_ENTRIES];
static size_t cached;
static uint32_t tick;
static int loaded, evicted;

static uint8_t *read_all(const char *path, size_t *size)
{
	struct fs_dirent st;
	struct fs_file_t f;
	uint8_t *buf;
	size_t done = 0;

	if (fs_stat(path, &st) != 0) {
		return NULL;
	}
	buf = malloc(st.size + 1);
	if (buf == NULL) {
		return NULL;
	}
	fs_file_t_init(&f);
	if (fs_open(&f, path, FS_O_READ) != 0) {
		free(buf);
		return NULL;
	}
	while (done < st.size) {
		ssize_t n = fs_read(&f, buf + done, MIN(st.size - done, (size_t)32768));

		if (n <= 0) {
			break;
		}
		done += n;
	}
	fs_close(&f);
	*size = done;
	return buf;
}

static void drop(struct entry *e)
{
	cached -= e->size;
	free(e->px);
	memset(e, 0, sizeof(*e));
	evicted++;
}

static struct entry *oldest(void)
{
	struct entry *o = NULL;

	for (int i = 0; i < CACHE_ENTRIES; i++) {
		if (cache[i].px != NULL && (o == NULL || cache[i].used < o->used)) {
			o = &cache[i];
		}
	}
	return o;
}

/* a name that is not in the cache is read from the card; a failed read is remembered */
static const uint16_t *image_cb(void *user, const char *src, int *w, int *h)
{
	struct entry *e = NULL, *slot = NULL;
	char path[160];
	uint8_t *raw;
	size_t n;

	ARG_UNUSED(user);
	for (int i = 0; i < CACHE_ENTRIES; i++) {
		if (strcmp(cache[i].name, src) == 0 && cache[i].name[0] != '\0') {
			e = &cache[i];
			break;
		}
		if (slot == NULL && cache[i].name[0] == '\0') {
			slot = &cache[i];
		}
	}
	if (e != NULL) {
		e->used = ++tick;
		if (e->px == NULL) {
			return NULL;
		}
		*w = e->w;
		*h = e->h;
		return e->px;
	}
	if (slot == NULL) {
		slot = oldest();
		if (slot == NULL) {
			return NULL;
		}
		drop(slot);
	}
	snprintk(path, sizeof(path), "%s/%s", dir, src);
	raw = read_all(path, &n);
	strncpy(slot->name, src, sizeof(slot->name) - 1);
	slot->used = ++tick;
	if (raw == NULL || n < 8 || memcmp(raw, "LHI1", 4) != 0) {
		free(raw);
		return NULL;
	}
	slot->w = raw[4] | raw[5] << 8;
	slot->h = raw[6] | raw[7] << 8;
	if (n < 8 + (size_t)slot->w * slot->h * 2) {
		free(raw);
		return NULL;
	}
	/* make room, never by dropping this one */
	while (cached + n > CACHE_LIMIT) {
		struct entry *o = oldest();

		if (o == NULL) {
			break;
		}
		drop(o);
	}
	/* the pixels start 8 bytes into the file: keep the buffer and point into it */
	memmove(raw, raw + 8, n - 8);
	slot->px = (uint16_t *)raw;
	slot->size = n;
	cached += n;
	loaded++;
	*w = slot->w;
	*h = slot->h;
	return slot->px;
}

void assets_init(const char *html_path)
{
	char path[160];
	size_t n;
	uint8_t *font;

	strncpy(dir, html_path, sizeof(dir) - 1);
	char *slash = strrchr(dir, '/');

	if (slash != NULL) {
		*slash = '\0';
	}
	snprintk(path, sizeof(path), "%s/font.lhf", dir);
	font = read_all(path, &n);
	if (font != NULL) {
		lh_set_cjk_font(font, n);
		printk("assets: extra glyphs from %s (%u KiB)\n", path, (unsigned int)(n >> 10));
	}
	lh_set_image_loader(image_cb, NULL);
	printk("assets: images from %s/img\n", dir);
}

void assets_report(void)
{
	printk("assets: %u KiB of images cached, %d loaded, %d dropped\n", (unsigned int)(cached >> 10),
	       loaded, evicted);
}
