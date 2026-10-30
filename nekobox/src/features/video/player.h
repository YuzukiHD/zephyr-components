/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NEKOBOX_PLAYER_H_
#define NEKOBOX_PLAYER_H_

#include <stdbool.h>
#include <stdint.h>

enum player_state {
	PLAYER_IDLE,		/**< nothing is open */
	PLAYER_PLAYING,
	PLAYER_PAUSED,
	PLAYER_ENDED,		/**< the end of the file was reached */
	PLAYER_ERROR,
};

struct player_info {
	uint32_t duration_ms;
	uint16_t width, height;
	bool has_audio;
	bool raw;	/**< an H.264 file without a container: the length is an estimate */
};

/** Opens an MP4 file and starts playing it; the picture goes to the video plane */
int player_open(const char *path, struct player_info *info);

/** A download publishes bytes only after syncing the file. ctx stays valid until player_close(). */
struct player_growing {
	uint64_t total; /**< Expected size, or 0 when unknown. */
	/** Readable bytes; state is 0 while downloading, 1 on completion, or a negative errno. */
	uint64_t (*avail)(void *ctx, int *state);
	void *ctx;
	uint16_t max_width, max_height; /**< Optional size limit; both 0 mean unlimited. Rotation is allowed. */
};

/** Validates dimensions against a stream's size limit; -E2BIG when oversized. */
int player_growing_check_resolution(const struct player_growing *g, uint32_t width, uint32_t height);

/** Nonblocking header/size check: -EAGAIN until moov arrives, -E2BIG when oversized. */
int player_growing_ready(const char *path, const struct player_growing *g);

/** Opens a growing MP4 paused; -EAGAIN if its header has not arrived. */
int player_open_growing(const char *path, const struct player_growing *g, struct player_info *info);

/** Stops, releases everything and takes the picture off the screen */
void player_close(void);

void player_play(void);
void player_pause(void);

/** Jumps to a position; a paused player shows the picture there and stays paused */
void player_seek(uint32_t ms);

enum player_state player_state(void);

/** Time stamp of the picture on the screen */
uint32_t player_position_ms(void);

/** Volume in percent, 0 is mute; may be called at any time */
void player_set_volume(int percent);
int player_volume(void);

#endif /* NEKOBOX_PLAYER_H_ */
