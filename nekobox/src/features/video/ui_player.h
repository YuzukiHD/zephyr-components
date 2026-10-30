/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NEKOBOX_FEATURES_VIDEO_UI_PLAYER_H_
#define NEKOBOX_FEATURES_VIDEO_UI_PLAYER_H_

#include <stdint.h>

struct player_growing;

/** Plays a video; the back button of the player returns to the screen named @p from */
void ui_player_open(const char *path, const char *from);

/** Opens a ready growing MP4 without adding its temporary file to the playback history. */
int ui_player_open_growing(const char *path, const struct player_growing *g, const char *from);

/** Length of the video on the player screen in ms (0 when none is open) */
uint32_t ui_player_duration_ms(void);

#endif /* NEKOBOX_FEATURES_VIDEO_UI_PLAYER_H_ */
