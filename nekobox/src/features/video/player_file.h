/* SPDX-License-Identifier: Apache-2.0 */

#ifndef NEKOBOX_PLAYER_FILE_H_
#define NEKOBOX_PLAYER_FILE_H_

#include <zephyr/fs/fs.h>
#include "player.h"

/* Each FAT handle has its own size snapshot and read window. */
struct reader {
	struct fs_file_t f;
	uint8_t *win;
	size_t win_size;
	uint64_t win_off;
	size_t win_len;
	char path[256];
	struct player_growing grow;
	uint64_t known;
	int64_t wait_ms;
	uint32_t seq;
	bool opening;
};

int pl_file_read(void *ctx, uint64_t off, void *buf, size_t len);
void pl_reader_close(struct reader *r);
int pl_reader_open(struct reader *r, const char *path, size_t win_size);
void pl_reader_growing(struct reader *r, const struct player_growing *g);
int pl_reader_movie_size(struct reader *r, uint64_t *size);

#endif
