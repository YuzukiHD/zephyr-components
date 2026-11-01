/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MIRACAST_PLAYER_H_
#define MIRACAST_PLAYER_H_
#include <miracast/miracast.h>

/** Optional Sunxi H.264/LPCM player. Volume is a percentage from 0 to 100. */
int miracast_player_start(unsigned volume);
/** Receiver media callback; copies payloads into bounded decoder queues. */
int miracast_player_media(void *ctx, enum miracast_codec codec, const void *data, size_t len,
			  int64_t pts, bool discontinuity);
/** Stop the receiver first. Call player start/stop from the application thread. */
void miracast_player_stop(void);
unsigned miracast_player_frames(void);

#endif
