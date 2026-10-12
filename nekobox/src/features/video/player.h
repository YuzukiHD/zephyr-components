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
