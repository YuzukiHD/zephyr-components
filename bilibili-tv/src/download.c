/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "download.h"
#include "http.h"

#define STAGE_SIZE	(64 * 1024)
#define ATTEMPTS	6
#define REPORT_MS	250

struct dl {
	struct fs_file_t *fp, own;
	uint8_t *stage;
	size_t used;
	uint64_t done, total;
	dl_progress_fn progress;
	void *ctx;
	int64_t reported;
	atomic_t *synced;	/* bytes that are in the file and in its directory entry (for a reader), or NULL */
	uint64_t written;
};

static int flush(struct dl *d)
{
	ssize_t n;

	if (d->used == 0) {
		return 0;
	}
	n = fs_write(d->fp, d->stage, d->used);
	if (n != (ssize_t)d->used) {
		return n < 0 ? (int)n : -EIO;
	}
	d->written += d->used;
	d->used = 0;
	if (d->synced != NULL) {
		/* a reader opens the file by its own handle: the size must be in the directory entry */
		int ret = fs_sync(d->fp);

		if (ret != 0) {
			return ret;
		}
		atomic_set(d->synced, (atomic_val_t)d->written);
	}

	return 0;
}

static int sink(void *ctx, const uint8_t *data, size_t len)
{
	struct dl *d = ctx;

	while (len > 0) {
		size_t take = MIN(len, STAGE_SIZE - d->used);

		memcpy(d->stage + d->used, data, take);
		d->used += take;
		d->done += take;
		data += take;
		len -= take;
		if (d->used == STAGE_SIZE) {
			int ret = flush(d);

			if (ret != 0) {
				return ret;
			}
		}
	}
	if (d->progress != NULL && k_uptime_get() - d->reported >= REPORT_MS) {
		d->reported = k_uptime_get();
		if (d->progress(d->done, d->total, d->ctx) != 0) {
			return -ECANCELED;
		}
	}

	return 0;
}

static volatile int last_status;

int dl_last_status(void)
{
	return last_status;
}

static int run(const char *url, const char *path, uint64_t total, dl_progress_fn fn, void *ctx,
	       struct fs_file_t *opened, atomic_t *synced)
{
	struct dl d = {.total = total, .progress = fn, .ctx = ctx, .synced = synced};
	int ret, attempt;

	d.stage = malloc(STAGE_SIZE);
	if (d.stage == NULL) {
		return -ENOMEM;
	}
	if (opened != NULL) {
		d.fp = opened;
	} else {
		d.fp = &d.own;
		fs_file_t_init(&d.own);
		if (fs_stat(path, &(struct fs_dirent){0}) == 0) {
			fs_unlink(path);
		}
		ret = fs_open(&d.own, path, FS_O_CREATE | FS_O_WRITE);
		if (ret != 0) {
			printk("download: cannot create %s (%d)\n", path, ret);
			free(d.stage);
			return ret;
		}
	}

	for (attempt = 0; attempt < ATTEMPTS; attempt++) {
		struct http_opts o = {.range_start = d.done, .timeout_ms = 10000};
		struct http_result r;

		ret = http_get(url, &o, sink, &d, &r);
		last_status = ret == 0 ? r.status : 0;
		if (ret == 0) {
			ret = flush(&d);
		} else {
			flush(&d);
		}
		if (ret == -ECANCELED) {
			break;
		}
		if (ret == 0 && (r.status >= 500 || r.status == 429) && attempt + 1 < ATTEMPTS) {
			/* the cache server is busy or has none of the file yet: a new request often works */
			printk("download: status %d, again in %d s\n", r.status, attempt + 1);
			if (d.progress != NULL && d.progress(d.done, d.total, d.ctx) != 0) {
				ret = -ECANCELED;
				break;
			}
			k_msleep(1000 * (attempt + 1));
			continue;
		}
		if (ret == 0 && r.status / 100 != 2) {
			printk("download: status %d\n", r.status);
			ret = r.status == 403 || r.status == 404 ? -EACCES : -EIO;
			break;
		}
		if (ret == 0 && d.done > 0 && o.range_start > 0 && r.status != 206) {
			ret = -EPROTO;	/* the server does not do ranges: cannot continue */
			break;
		}
		if (ret == 0 && (total == 0 || d.done >= total)) {
			break;
		}
		if (ret == -ENOSPC || ret == -EIO || ret == -ENOMEM) {
			break;
		}
		printk("download: %llu of %llu bytes, continuing (%d)\n", (unsigned long long)d.done,
		       (unsigned long long)total, ret);
		k_msleep(500);
		ret = -ECONNRESET;
	}
	fs_close(d.fp);
	free(d.stage);
	/* a file that a reader has open is removed by whoever closes the reader */
	if (ret != 0 && opened == NULL && fs_stat(path, &(struct fs_dirent){0}) == 0) {
		fs_unlink(path);
	}

	return ret;
}

int dl_file(const char *url, const char *path, uint64_t total, dl_progress_fn fn, void *ctx)
{
	return run(url, path, total, fn, ctx, NULL, NULL);
}

/* ---- the download in the background, for a file that is played while it comes ------------------ */

static K_THREAD_STACK_DEFINE(bg_stack, 12288);
static struct k_thread bg_thread;
static struct {
	char url[640], path[96];
	uint64_t total;
	dl_progress_fn fn;
	void *ctx;
	struct fs_file_t file;
} bg;
static atomic_t bg_synced, bg_state;	/* state: 0 running, 1 done, negative errno */
static atomic_t bg_cancel;
static bool bg_started;

static int bg_progress(uint64_t done, uint64_t total, void *ctx)
{
	if (bg.fn != NULL) {
		bg.fn(done, total, bg.ctx);
	}

	return atomic_get(&bg_cancel) ? -ECANCELED : 0;
}

static void bg_main(void *a, void *b, void *c)
{
	int ret = run(bg.url, bg.path, bg.total, bg_progress, NULL, &bg.file, &bg_synced);

	if (ret != 0) {
		printk("download: ended with %d (http %d), %u of %llu bytes\n", ret, last_status, (unsigned int)atomic_get(&bg_synced),
		       (unsigned long long)bg.total);
	}
	atomic_set(&bg_state, ret == 0 ? 1 : (ret < 0 ? ret : -EIO));
}

int dl_start(const char *url, const char *path, uint64_t total, dl_progress_fn fn, void *ctx)
{
	int ret;

	dl_stop();
	if (strlen(url) >= sizeof(bg.url) || strlen(path) >= sizeof(bg.path)) {
		return -ENAMETOOLONG;
	}
	snprintf(bg.url, sizeof(bg.url), "%s", url);
	snprintf(bg.path, sizeof(bg.path), "%s", path);
	bg.total = total;
	bg.fn = fn;
	bg.ctx = ctx;
	if (fs_stat(path, &(struct fs_dirent){0}) == 0) {
		fs_unlink(path);
	}
	fs_file_t_init(&bg.file);
	ret = fs_open(&bg.file, path, FS_O_CREATE | FS_O_WRITE);
	if (ret != 0) {
		return ret;
	}
	fs_sync(&bg.file);
	atomic_set(&bg_synced, 0);
	atomic_set(&bg_state, 0);
	atomic_set(&bg_cancel, 0);
	bg_started = true;
	k_thread_create(&bg_thread, bg_stack, K_THREAD_STACK_SIZEOF(bg_stack), bg_main, NULL, NULL, NULL, 8, 0,
			K_NO_WAIT);
	k_thread_name_set(&bg_thread, "download");

	return 0;
}

uint64_t dl_avail(void *ctx, int *state)
{
	*state = (int)atomic_get(&bg_state);

	return (uint64_t)(uint32_t)atomic_get(&bg_synced);
}

void dl_stop(void)
{
	if (!bg_started) {
		return;
	}
	atomic_set(&bg_cancel, 1);
	k_thread_join(&bg_thread, K_SECONDS(15));
	bg_started = false;
}
