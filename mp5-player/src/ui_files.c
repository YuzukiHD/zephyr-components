/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The file browser: one directory of the SD card at a time, in a list that scrolls by swiping.
 * Only the rows on the screen exist as objects: a pool of rows is moved along with the scroll
 * position and gets the text of the entry it shows.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "mp5.h"

#define ROOT		"/SD:"
#define MAX_ENTRIES	512
#define NAME_MAX_LEN	96
#define PATH_MAX_LEN	256
#define POOL		13
#define ROW_H		72
#define LIST_Y		88
#define LIST_H		(768 - LIST_Y)

struct entry {
	uint32_t size;
	bool dir;
	char name[NAME_MAX_LEN];
};

static struct entry *entries;
static int count;
static enum files_filter filter = FILES_ALL;
static lv_obj_t *title_label, *recent_btn;
static char dir_path[PATH_MAX_LEN] = ROOT;
static lv_obj_t *path_label, *list, *spacer, *empty_label;
static lv_obj_t *rows[POOL];
static int row_idx[POOL];

static char lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int name_cmp(const char *a, const char *b)
{
	while (*a != '\0' && lower(*a) == lower(*b)) {
		a++;
		b++;
	}
	return (unsigned char)lower(*a) - (unsigned char)lower(*b);
}

static bool has_ext(const char *name, const char *ext)
{
	size_t n = strlen(name), e = strlen(ext);

	return n > e && name_cmp(name + n - e, ext) == 0;
}

static bool is_video(const char *name)
{
	return has_ext(name, ".mp4") || has_ext(name, ".h264") || has_ext(name, ".264");
}

static bool is_photo(const char *name)
{
	return has_ext(name, ".jpg") || has_ext(name, ".jpeg") || has_ext(name, ".png");
}

static const char *icon_of(const struct entry *e)
{
	if (e->dir) {
		return LV_SYMBOL_DIRECTORY;
	}
	if (is_video(e->name)) {
		return LV_SYMBOL_VIDEO;
	}
	if (is_photo(e->name)) {
		return LV_SYMBOL_IMAGE;
	}
	if (ui_game_ext(e->name)) {
		return LV_SYMBOL_KEYBOARD;
	}
	return LV_SYMBOL_FILE;
}

static uint32_t icon_color(const struct entry *e)
{
	if (e->dir) {
		return C_WARN;
	}
	if (is_video(e->name)) {
		return C_ACCENT;
	}
	if (is_photo(e->name)) {
		return C_OK;
	}
	if (ui_game_ext(e->name)) {
		return C_PINK;
	}
	return C_DIM;
}

/* folders first, then by name; inserted while reading so no second pass is needed */
static bool before(const struct entry *a, const struct entry *b)
{
	if (a->dir != b->dir) {
		return a->dir;
	}
	return name_cmp(a->name, b->name) < 0;
}

static bool shown_by_filter(const struct fs_dirent *d)
{
	if (d->type == FS_DIR_ENTRY_DIR || filter == FILES_ALL) {
		return true;
	}
	if (filter == FILES_NES) {
		return has_ext(d->name, ".nes");
	}
	if (filter == FILES_VIDEO) {
		return is_video(d->name);
	}

	return has_ext(d->name, ".gb") || has_ext(d->name, ".gbc") || has_ext(d->name, ".gba");
}

static void entry_add(const struct fs_dirent *d)
{
	struct entry e = {.size = d->size, .dir = d->type == FS_DIR_ENTRY_DIR};
	int at = count;

	if (count >= MAX_ENTRIES || d->name[0] == '.' || strlen(d->name) >= NAME_MAX_LEN ||
	    !shown_by_filter(d)) {
		return;
	}
	/* the font has no glyphs outside ASCII */
	for (int i = 0; d->name[i] != '\0'; i++) {
		unsigned char c = d->name[i];

		e.name[i] = (c >= 0x20 && c < 0x7f) ? c : '?';
	}
	while (at > 0 && before(&e, &entries[at - 1])) {
		at--;
	}
	memmove(&entries[at + 1], &entries[at], (count - at) * sizeof(e));
	entries[at] = e;
	count++;
}

static void entries_free(void)
{
	free(entries);
	entries = NULL;
	count = 0;
}

static int dir_load(void)
{
	struct fs_dir_t dir;
	struct fs_dirent d;
	int ret;

	entries_free();
	entries = malloc(MAX_ENTRIES * sizeof(*entries));
	if (entries == NULL) {
		return -ENOMEM;
	}
	fs_dir_t_init(&dir);
	ret = fs_opendir(&dir, dir_path);
	if (ret != 0) {
		entries_free();
		return ret;
	}
	while (fs_readdir(&dir, &d) == 0 && d.name[0] != '\0') {
		entry_add(&d);
	}
	fs_closedir(&dir);

	return 0;
}

static void format_size(char *buf, size_t n, uint32_t size)
{
	if (size >= 1024U * 1024U * 1024U) {
		snprintf(buf, n, "%u.%02u GB", size >> 30, (uint32_t)(((uint64_t)(size & 0x3fffffff) * 100) >> 30));
	} else if (size >= 1024U * 1024U) {
		snprintf(buf, n, "%u.%u MB", size >> 20, (uint32_t)(((uint64_t)(size & 0xfffff) * 10) >> 20));
	} else if (size >= 1024U) {
		snprintf(buf, n, "%u KB", size >> 10);
	} else {
		snprintf(buf, n, "%u B", size);
	}
}

static void path_join(char *out, size_t n, const char *dir, const char *name)
{
	snprintf(out, n, "%s/%s", dir, name);
}

/* "/SD:/a/b" -> "/SD:/a"; the root stays */
static bool path_up(char *path)
{
	char *slash = strrchr(path, '/');

	if (slash == NULL || slash == path || strcmp(path, ROOT) == 0) {
		return false;
	}
	*slash = '\0';

	return true;
}

static void list_show(void);

static void row_clicked(lv_event_t *e)
{
	int idx = row_idx[(int)(uintptr_t)lv_event_get_user_data(e)];
	char path[PATH_MAX_LEN];

	if (idx < 0 || idx >= count) {
		return;
	}
	path_join(path, sizeof(path), dir_path, entries[idx].name);
	if (entries[idx].dir) {
		strcpy(dir_path, path);
		if (dir_load() != 0) {
			path_up(dir_path);
			dir_load();
		}
		list_show();
	} else if (is_video(entries[idx].name)) {
		ui_player_open(path, SCR_FILES);
	} else if (is_photo(entries[idx].name)) {
		ui_goto_arg(SCR_PHOTOS, path);
	} else if (ui_game_ext(entries[idx].name)) {
		ui_game_open(path, SCR_FILES);
	}
}

static void recent_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_goto(SCR_RECENT);
}

static void back_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	if (path_up(dir_path)) {
		dir_load();
		list_show();
	} else {
		ui_goto(SCR_HOME);
	}
}

/* puts the entries that are in view on the rows of the pool */
static void rows_update(void)
{
	int first = MAX(0, lv_obj_get_scroll_y(list) / ROW_H);

	for (int i = 0; i < POOL; i++) {
		lv_obj_t *row = rows[i];
		const struct entry *e;
		char buf[24];
		/* a row keeps its slot: entry n is always on row n % POOL */
		int idx = first + ((i - first % POOL) + POOL) % POOL;

		if (idx >= count) {
			lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
			row_idx[i] = -1;
			continue;
		}
		lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
		lv_obj_set_y(row, idx * ROW_H);
		if (row_idx[i] == idx) {
			continue;
		}
		row_idx[i] = idx;
		e = &entries[idx];
		lv_label_set_text(lv_obj_get_child(row, 0), icon_of(e));
		lv_obj_set_style_text_color(lv_obj_get_child(row, 0), lv_color_hex(icon_color(e)), 0);
		lv_label_set_text(lv_obj_get_child(row, 1), e->name);
		if (e->dir) {
			buf[0] = '\0';
		} else {
			format_size(buf, sizeof(buf), e->size);
		}
		lv_label_set_text(lv_obj_get_child(row, 2), buf);
	}
}

static void list_scrolled(lv_event_t *e)
{
	ARG_UNUSED(e);
	rows_update();
}

static void list_scroll_begin(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_stats_scroll_begin("files");
}

static void list_scroll_end(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_stats_scroll_end();
}

static void list_show(void)
{
	lv_label_set_text(path_label, dir_path);
	lv_obj_set_height(spacer, MAX(count * ROW_H + 16, 1));
	lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
	for (int i = 0; i < POOL; i++) {
		row_idx[i] = -1;
	}
	rows_update();
	if (count == 0) {
		lv_obj_remove_flag(empty_label, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(empty_label, LV_OBJ_FLAG_HIDDEN);
	}
}

static lv_obj_t *row_create(lv_obj_t *parent, int slot)
{
	lv_obj_t *row = lv_button_create(parent);
	lv_obj_t *icon = lv_label_create(row);
	lv_obj_t *name = lv_label_create(row);
	lv_obj_t *size = lv_label_create(row);

	lv_obj_remove_style_all(row);
	lv_obj_set_size(row, 960, ROW_H - 8);
	lv_obj_set_x(row, 32);
	lv_obj_set_style_radius(row, 18, 0);
	lv_obj_set_style_bg_color(row, lv_color_hex(C_SURFACE), 0);
	lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(row, lv_color_hex(C_SURFACE2), LV_STATE_PRESSED);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_hor(row, 24, 0);
	lv_obj_set_style_pad_column(row, 20, 0);
	lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
	/* a swipe that starts on a row scrolls the list, a tap opens the entry */
	lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
	lv_obj_add_event_cb(row, row_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)slot);

	lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
	lv_obj_set_width(icon, 40);
	lv_obj_set_style_text_color(name, lv_color_hex(C_TEXT), 0);
	lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
	lv_obj_set_flex_grow(name, 1);
	lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
	lv_obj_set_style_text_color(size, lv_color_hex(C_DIM), 0);
	lv_obj_set_style_text_font(size, &lv_font_montserrat_20, 0);

	return row;
}

static lv_obj_t *files_create(void)
{
	lv_obj_t *scr = ui_screen_new(false);
	lv_obj_t *bar = ui_title_bar(scr, "Files", back_clicked);

	title_label = lv_obj_get_child(bar, 1);

	recent_btn = lv_button_create(bar);
	{
		lv_obj_t *l = lv_label_create(recent_btn);

		lv_obj_remove_style_all(recent_btn);
		lv_obj_set_size(recent_btn, 190, 56);
		lv_obj_set_style_radius(recent_btn, LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_bg_color(recent_btn, lv_color_hex(C_ACCENT), 0);
		lv_obj_set_style_bg_opa(recent_btn, LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(recent_btn, lv_color_hex(C_ACCENT2), LV_STATE_PRESSED);
		lv_obj_set_style_text_color(l, lv_color_hex(C_TEXT), 0);
		lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
		lv_label_set_text(l, LV_SYMBOL_LIST "  Recent");
		lv_obj_center(l);
		lv_obj_add_event_cb(recent_btn, recent_clicked, LV_EVENT_CLICKED, NULL);
	}

	path_label = lv_label_create(bar);
	lv_obj_set_flex_grow(path_label, 1);
	lv_label_set_long_mode(path_label, LV_LABEL_LONG_DOT);
	lv_obj_set_style_text_align(path_label, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_set_style_text_color(path_label, lv_color_hex(C_DIM), 0);
	lv_obj_set_style_text_font(path_label, &lv_font_montserrat_20, 0);
	/* the button at the right end of the bar */
	lv_obj_move_to_index(recent_btn, -1);

	list = lv_obj_create(scr);
	lv_obj_remove_style_all(list);
	lv_obj_set_size(list, LV_PCT(100), LIST_H);
	lv_obj_set_pos(list, 0, LIST_Y);
	lv_obj_set_scroll_dir(list, LV_DIR_VER);
	lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
	lv_obj_set_style_bg_color(list, lv_color_hex(C_DIM), LV_PART_SCROLLBAR);
	lv_obj_set_style_bg_opa(list, LV_OPA_50, LV_PART_SCROLLBAR);
	lv_obj_set_style_width(list, 6, LV_PART_SCROLLBAR);
	lv_obj_set_style_radius(list, 3, LV_PART_SCROLLBAR);

	/* it gives the list its scrollable height */
	spacer = lv_obj_create(list);
	lv_obj_remove_style_all(spacer);
	lv_obj_set_size(spacer, 1, 1);
	lv_obj_remove_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
	for (int i = 0; i < POOL; i++) {
		rows[i] = row_create(list, i);
	}
	lv_obj_add_event_cb(list, list_scrolled, LV_EVENT_SCROLL, NULL);
	lv_obj_add_event_cb(list, list_scroll_begin, LV_EVENT_SCROLL_BEGIN, NULL);
	lv_obj_add_event_cb(list, list_scroll_end, LV_EVENT_SCROLL_END, NULL);

	empty_label = lv_label_create(scr);
	lv_label_set_text(empty_label, "This folder is empty");
	lv_obj_set_style_text_color(empty_label, lv_color_hex(C_DIM), 0);
	lv_obj_set_style_text_font(empty_label, &lv_font_montserrat_28, 0);
	lv_obj_align(empty_label, LV_ALIGN_CENTER, 0, -40);

	return scr;
}

void ui_files_open(const char *dir, enum files_filter f)
{
	filter = f;
	ui_goto_arg(SCR_FILES, (void *)dir);
}

/* the argument is a directory to open, or NULL for the root; a file path opens its folder */
static void files_enter(void *arg)
{
	if (arg != NULL) {
		const char *p = arg;

		strncpy(dir_path, p, sizeof(dir_path) - 1);
		dir_path[sizeof(dir_path) - 1] = '\0';
	} else {
		strcpy(dir_path, ROOT);
	}
	if (filter == FILES_VIDEO) {
		lv_obj_remove_flag(recent_btn, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(recent_btn, LV_OBJ_FLAG_HIDDEN);
	}
	lv_label_set_text(title_label, filter == FILES_NES ? "NES" : filter == FILES_GB ? "Game Boy" :
				       filter == FILES_VIDEO ? "Videos" : "Files");
	if (!storage_ready() || dir_load() != 0) {
		strcpy(dir_path, ROOT);
		if (!storage_ready() || dir_load() != 0) {
			entries_free();
		}
	}
	list_show();
}

static void files_leave(void)
{
	entries_free();
}

const struct screen_ops files_ops = {
	.create = files_create,
	.enter = files_enter,
	.leave = files_leave,
};

void files_demo_scroll(int32_t dy)
{
	lv_obj_scroll_by(list, 0, dy, LV_ANIM_ON);
}
