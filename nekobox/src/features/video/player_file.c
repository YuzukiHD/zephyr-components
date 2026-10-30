/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/mp4.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include "player_file.h"

/* Implemented by the session controller; waits must also end on pause, seek and close. */
extern bool pl_req_changed(uint32_t seq);

static int grow_wait(struct reader *r, uint64_t need)
{
	uint64_t available;
	int64_t started = 0;
	int state, ret;

	for (;;) {
		if (!r->opening && pl_req_changed(r->seq)) {
			return -ECANCELED;
		}
		available = r->grow.avail(r->grow.ctx, &state);
		if (state < 0) {
			return state;
		}
		if (available >= need) {
			break;
		}
		if (state == 1) {
			return -EIO;
		}
		if (r->opening) {
			return -EAGAIN;
		}
		if (started == 0) {
			started = k_uptime_get();
		}
		k_msleep(20);
	}
	if (started != 0) {
		r->wait_ms += k_uptime_get() - started;
	}
	if (need > r->known) {
		/* Reopening refreshes the size cached by FAT. The writer syncs before publishing. */
		ret = fs_close(&r->f);
		if (ret != 0) {
			return ret;
		}
		fs_file_t_init(&r->f);
		ret = fs_open(&r->f, r->path, FS_O_READ);
		if (ret != 0) {
			return ret;
		}
		r->known = available;
	}
	return 0;
}

int pl_file_read(void *ctx, uint64_t off, void *buf, size_t len)
{
	struct reader *r = ctx;
	ssize_t n;
	int ret;

	if (len > UINT64_MAX - off) {
		return -EOVERFLOW;
	}
	if (r->grow.avail != NULL) {
		ret = grow_wait(r, off + len);
		if (ret != 0) {
			return ret;
		}
	}
	if (len > r->win_size) {
		ret = fs_seek(&r->f, (off_t)off, FS_SEEK_SET);
		n = ret == 0 ? fs_read(&r->f, buf, len) : ret;
		return n == (ssize_t)len ? 0 : (n < 0 ? (int)n : -EIO);
	}
	if (off < r->win_off || off + len > r->win_off + r->win_len) {
		size_t want = r->win_size;

		ret = fs_seek(&r->f, (off_t)off, FS_SEEK_SET);
		if (ret != 0) {
			return ret;
		}
		if (r->grow.avail != NULL) {
			want = MIN((uint64_t)want, r->known - off);
		}
		n = fs_read(&r->f, r->win, want);
		if (n < (ssize_t)len) {
			r->win_len = 0;
			return n < 0 ? (int)n : -EIO;
		}
		r->win_off = off;
		r->win_len = n;
	}
	memcpy(buf, r->win + (off - r->win_off), len);
	return 0;
}

void pl_reader_close(struct reader *r)
{
	if (r->win != NULL) {
		fs_close(&r->f);
		free(r->win);
		r->win = NULL;
	}
	r->win_len = 0;
}

int pl_reader_open(struct reader *r, const char *path, size_t win_size)
{
	int ret;

	memset(r, 0, sizeof(*r));
	if (strlen(path) >= sizeof(r->path)) {
		return -ENAMETOOLONG;
	}
	strcpy(r->path, path);
	fs_file_t_init(&r->f);
	r->win_size = win_size;
	r->win = aligned_alloc(64, win_size);
	if (r->win == NULL) {
		return -ENOMEM;
	}
	ret = fs_open(&r->f, path, FS_O_READ);
	if (ret != 0) {
		free(r->win);
		r->win = NULL;
	}
	return ret;
}

void pl_reader_growing(struct reader *r, const struct player_growing *g)
{
	r->grow = *g;
	r->opening = true;
}

/* Only the complete movie box is needed to parse the tables, even with no Content-Length.
 * Stopping at its end also avoids waiting for boxes after the media payload on the UI thread.
 */
int pl_reader_movie_size(struct reader *r, uint64_t *size)
{
	uint64_t off = 0;

	for (;;) {
		uint8_t h[16];
		uint64_t n, available;
		int state, ret;
		size_t header = 8;

		available = r->grow.avail(r->grow.ctx, &state);
		if (state < 0) {
			return state;
		}
		if (off > available || available - off < 8) {
			return state == 1 ? -EINVAL : -EAGAIN;
		}
		ret = pl_file_read(r, off, h, 8);
		if (ret != 0) {
			return ret;
		}
		n = sys_get_be32(h);
		if (n == 1) {
			ret = pl_file_read(r, off + 8, h + 8, 8);
			if (ret != 0) {
				return ret;
			}
			n = sys_get_be64(h + 8);
			header = 16;
		} else if (n == 0) {
			if (state != 1) {
				return -EAGAIN;
			}
			n = available - off;
		}
		if (n < header || n > UINT64_MAX - off ||
		    (r->grow.total && (off > r->grow.total || n > r->grow.total - off))) {
			return -EINVAL;
		}
		if (memcmp(h + 4, "moov", 4) == 0) {
			if (n > available - off) {
				return state == 1 ? -EIO : -EAGAIN;
			}
			*size = off + n;
			return 0;
		}
		off += n;
	}
}

int player_growing_check_resolution(const struct player_growing *g, uint32_t width, uint32_t height)
{
	if (g == NULL || (g->max_width == 0 && g->max_height == 0)) {
		return 0;
	}
	if (width == 0 || height == 0) {
		return -EINVAL;
	}
	if ((width <= g->max_width && height <= g->max_height) ||
	    (width <= g->max_height && height <= g->max_width)) {
		return 0;
	}
	printk("player: refusing %ux%u video, cast limit %ux%u\n", width, height,
	       g->max_width, g->max_height);
	return -E2BIG;
}

int player_growing_ready(const char *path, const struct player_growing *g)
{
	struct reader r;
	uint64_t size;
	int ret;

	if (g == NULL || g->avail == NULL) {
		return -EINVAL;
	}
	ret = pl_reader_open(&r, path, 4096);
	if (ret == 0) {
		pl_reader_growing(&r, g);
		ret = pl_reader_movie_size(&r, &size);
		if (ret == 0 && (g->max_width || g->max_height)) {
			struct mp4 movie;

			ret = mp4_open(&movie, pl_file_read, &r, size);
			if (ret == 0) {
				ret = player_growing_check_resolution(g, movie.video.width, movie.video.height);
				mp4_close(&movie);
			}
		}
		pl_reader_close(&r);
	}
	return ret;
}
