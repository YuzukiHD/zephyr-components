/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The videos that were played last, with the position they were left at. The list is a text file
 * on the SD card, one line per video: "<position ms> <duration ms> <path>".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "core/fsutil.h"
#include "history.h"

#define HISTORY_FILE "/SD:/.mp5_recent"

static struct history_entry list[HISTORY_MAX];
static int count;
static bool loaded;

static void save(void)
{
	struct fs_file_t f;
	char line[HISTORY_PATH_MAX + 32];

	fs_file_t_init(&f);
	if (fs_open(&f, HISTORY_FILE, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC) != 0) {
		return;
	}
	for (int i = 0; i < count; i++) {
		int n = snprintf(line, sizeof(line), "%u %u %s\n", (unsigned)list[i].pos_ms,
				 (unsigned)list[i].dur_ms, list[i].path);

		if (fs_write(&f, line, n) < 0) {
			break;
		}
	}
	fs_close(&f);
}

void history_load(void)
{
	struct fs_dirent st;
	char *buf, *p, *end;

	count = 0;
	loaded = true;
	if (fs_stat(HISTORY_FILE, &st) != 0 || st.size == 0 || st.size > 16 * 1024) {
		return;
	}
	buf = malloc(st.size + 1);
	if (buf == NULL) {
		return;
	}
	if (file_read_text(HISTORY_FILE, buf, st.size + 1) < 0) {
		free(buf);
		return;
	}

	for (p = buf; *p != '\0' && count < HISTORY_MAX; p = end) {
		unsigned long pos, dur;
		char *q;

		end = strchr(p, '\n');
		if (end != NULL) {
			*end++ = '\0';
		} else {
			end = p + strlen(p);
		}
		pos = strtoul(p, &q, 10);
		dur = strtoul(q, &q, 10);
		if (*q == ' ' && strlen(q + 1) < HISTORY_PATH_MAX) {
			strcpy(list[count].path, q + 1);
			list[count].pos_ms = pos;
			list[count].dur_ms = dur;
			count++;
		}
	}
	free(buf);
}

int history_count(void)
{
	if (!loaded) {
		history_load();
	}

	return count;
}

const struct history_entry *history_get(int index)
{
	return index >= 0 && index < history_count() ? &list[index] : NULL;
}

const struct history_entry *history_find(const char *path)
{
	for (int i = 0; i < history_count(); i++) {
		if (strcmp(list[i].path, path) == 0) {
			return &list[i];
		}
	}

	return NULL;
}

void history_update(const char *path, uint32_t pos_ms, uint32_t dur_ms)
{
	int at = -1;
	struct history_entry e;

	if (strlen(path) >= HISTORY_PATH_MAX) {
		return;
	}
	(void)history_count();
	for (int i = 0; i < count; i++) {
		if (strcmp(list[i].path, path) == 0) {
			at = i;
			break;
		}
	}
	if (at < 0) {
		at = count < HISTORY_MAX ? count++ : HISTORY_MAX - 1;
	}
	memmove(&list[1], &list[0], at * sizeof(list[0]));
	strcpy(e.path, path);
	e.pos_ms = pos_ms;
	e.dur_ms = dur_ms;
	list[0] = e;
	save();
}
 