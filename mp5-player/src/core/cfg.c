/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The settings that stay across power cycles: a text file on the SD card with one "key value"
 * line per setting (the theme, the Wi-Fi network).
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>

#include "core/app.h"
#include "core/fsutil.h"
#include "core/storage.h"
#include "core/cfg.h"

#define CFG_FILE	"/SD:/.mp5_settings"
#define CFG_MAX		28
#define KEY_LEN		16
#define VAL_LEN		96

static struct {
	char key[KEY_LEN];
	char val[VAL_LEN];
} items[CFG_MAX];
static int count;
static K_MUTEX_DEFINE(lock);

static void save(void)
{
	struct fs_file_t f;
	char line[KEY_LEN + VAL_LEN + 4];

	if (!storage_ready()) {
		return;
	}
	fs_file_t_init(&f);
	if (fs_open(&f, CFG_FILE, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC) != 0) {
		return;
	}
	for (int i = 0; i < count; i++) {
		int n = snprintf(line, sizeof(line), "%s %s\n", items[i].key, items[i].val);

		if (fs_write(&f, line, n) < 0) {
			break;
		}
	}
	fs_close(&f);
}

static int cfg_find(const char *key, int n)
{
	for (int i = 0; i < n; i++) {
		if (strcmp(items[i].key, key) == 0) {
			return i;
		}
	}

	return -1;
}

void cfg_load(void)
{
	char buf[3072];
	int n, kept;

	k_mutex_lock(&lock, K_FOREVER);
	/* settings that were changed while there was no card win over the ones in the file */
	kept = count;
	n = storage_ready() ? file_read_text(CFG_FILE, buf, sizeof(buf)) : -1;
	if (n >= 0) {
		if (n > 0) {
			char *line = buf, *next;

			for (; line != NULL && *line != '\0' && count < CFG_MAX; line = next) {
				char *sp;

				next = strchr(line, '\n');
				if (next != NULL) {
					*next++ = '\0';
				}
				sp = strchr(line, ' ');
				if (sp == NULL || sp == line || (sp - line) >= KEY_LEN) {
					continue;
				}
				*sp++ = '\0';
				if (cfg_find(line, kept) >= 0) {
					continue;
				}
				strncpy(items[count].key, line, KEY_LEN - 1);
				strncpy(items[count].val, sp, VAL_LEN - 1);
				count++;
			}
		}
	}
	if (kept > 0 && n >= 0) {
		save();
	}
	k_mutex_unlock(&lock);
}

const char *cfg_get(const char *key)
{
	const char *ret = NULL;

	k_mutex_lock(&lock, K_FOREVER);
	for (int i = 0; i < count; i++) {
		if (strcmp(items[i].key, key) == 0) {
			ret = items[i].val;
			break;
		}
	}
	k_mutex_unlock(&lock);

	return ret;
}

void cfg_set(const char *key, const char *value)
{
	int at = -1;

	k_mutex_lock(&lock, K_FOREVER);
	for (int i = 0; i < count; i++) {
		if (strcmp(items[i].key, key) == 0) {
			at = i;
			break;
		}
	}
	if (value == NULL) {
		if (at >= 0) {
			items[at] = items[count - 1];
			count--;
		}
	} else {
		if (at < 0 && count < CFG_MAX) {
			at = count++;
		}
		if (at >= 0) {
			strncpy(items[at].key, key, KEY_LEN - 1);
			items[at].key[KEY_LEN - 1] = '\0';
			strncpy(items[at].val, value, VAL_LEN - 1);
			items[at].val[VAL_LEN - 1] = '\0';
		}
	}
	save();
	k_mutex_unlock(&lock);
}

static int cfg_step(void)
{
	cfg_load();

	return 0;
}
MP5_INIT(MP5_STAGE_STORAGE, 10, "settings", cfg_step);
