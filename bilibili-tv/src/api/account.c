/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "api/account.h"

#define FILE_PATH	CONFIG_BILI_CACHE_DIR "/auth.cfg"

static K_MUTEX_DEFINE(lock);
static struct {
	char sessdata[280];
	char jct[48];
	char uid[24];
	char name[64];
} acc;

/*
 * The value of "key=" in a text, up to the next character that ends a value in an address or a JSON
 * string (& or the backslash of an escaped one). The text is the raw answer: the '&' between the
 * values may be written as \u0026 in it.
 */
static bool text_value(const char *text, const char *key, char *out, size_t size)
{
	size_t kl = strlen(key);

	for (const char *p = strstr(text, key); p != NULL; p = strstr(p + 1, key)) {
		size_t n;

		if (p[kl] != '=' || (p != text && p[-1] != '?' && p[-1] != '&' && p[-1] != '6' && p[-1] != '"' && p[-1] != ' ')) {
			continue;
		}
		p += kl + 1;
		n = strcspn(p, "&;\\ \r\n\"");
		if (n == 0 || n >= size) {
			printk("account: %s is %u bytes, does not fit\n", key, (unsigned)n);
			return false;
		}
		memcpy(out, p, n);
		out[n] = '\0';

		return true;
	}

	return false;
}

static void save(void)
{
	struct fs_file_t f;
	char buf[420];
	int n;

	n = snprintf(buf, sizeof(buf), "SESSDATA=%s\nbili_jct=%s\nDedeUserID=%s\n", acc.sessdata, acc.jct, acc.uid);
	fs_mkdir(CONFIG_BILI_CACHE_DIR);
	fs_file_t_init(&f);
	if (fs_open(&f, FILE_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC) != 0) {
		printk("account: %s cannot be written\n", FILE_PATH);
		return;
	}
	if (fs_write(&f, buf, n) != n) {
		printk("account: write failed\n");
	}
	fs_close(&f);
}

void account_load(void)
{
	struct fs_file_t f;
	char buf[420];
	ssize_t n;

	fs_file_t_init(&f);
	if (fs_open(&f, FILE_PATH, FS_O_READ) != 0) {
		return;
	}
	n = fs_read(&f, buf, sizeof(buf) - 1);
	fs_close(&f);
	if (n <= 0) {
		return;
	}
	buf[n] = '\0';
	k_mutex_lock(&lock, K_FOREVER);
	for (char *line = buf; line != NULL && *line != '\0';) {
		char *nl = strchr(line, '\n'), *eq;

		if (nl != NULL) {
			*nl++ = '\0';
		}
		line[strcspn(line, "\r")] = '\0';
		eq = strchr(line, '=');
		if (eq != NULL) {
			*eq++ = '\0';
			if (strcmp(line, "SESSDATA") == 0) {
				snprintf(acc.sessdata, sizeof(acc.sessdata), "%s", eq);
			} else if (strcmp(line, "bili_jct") == 0) {
				snprintf(acc.jct, sizeof(acc.jct), "%s", eq);
			} else if (strcmp(line, "DedeUserID") == 0) {
				snprintf(acc.uid, sizeof(acc.uid), "%s", eq);
			}
		}
		line = nl;
	}
	k_mutex_unlock(&lock);
	printk("account: login of user %s read from the card\n", acc.uid);
}

int account_set_from_text(const char *text)
{
	char s[sizeof(acc.sessdata)], j[sizeof(acc.jct)], u[sizeof(acc.uid)];
	bool hs = text_value(text, "SESSDATA", s, sizeof(s)), hj = text_value(text, "bili_jct", j, sizeof(j));
	bool hu = text_value(text, "DedeUserID", u, sizeof(u));

	if (!hs || !hj || !hu) {
		printk("account: no login in the answer (%u bytes): SESSDATA %d, bili_jct %d, DedeUserID %d\n",
		       (unsigned)strlen(text), hs, hj, hu);
		return -EBADMSG;
	}
	k_mutex_lock(&lock, K_FOREVER);
	memcpy(acc.sessdata, s, sizeof(s));
	memcpy(acc.jct, j, sizeof(j));
	memcpy(acc.uid, u, sizeof(u));
	acc.name[0] = '\0';
	k_mutex_unlock(&lock);
	save();

	return 0;
}

void account_clear(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	memset(&acc, 0, sizeof(acc));
	k_mutex_unlock(&lock);
	fs_unlink(FILE_PATH);
}

bool account_logged_in(void)
{
	return acc.sessdata[0] != '\0';
}

void account_cookie(char *out, size_t size)
{
	k_mutex_lock(&lock, K_FOREVER);
	if (acc.sessdata[0] != '\0') {
		snprintf(out, size, "SESSDATA=%s; bili_jct=%s; DedeUserID=%s", acc.sessdata, acc.jct, acc.uid);
	} else {
		out[0] = '\0';
	}
	k_mutex_unlock(&lock);
}

void account_name(char *out, size_t size)
{
	k_mutex_lock(&lock, K_FOREVER);
	snprintf(out, size, "%s", acc.name);
	k_mutex_unlock(&lock);
}

void account_set_name(const char *name)
{
	k_mutex_lock(&lock, K_FOREVER);
	snprintf(acc.name, sizeof(acc.name), "%s", name);
	k_mutex_unlock(&lock);
}
