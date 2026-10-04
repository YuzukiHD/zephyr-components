/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_H_
#define APP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* video.c: RGB565 frame to the video plane */
int video_init(void);
/** The buffer the next frame is drawn into, and its hand-over to the display */
uint16_t *video_acquire(void);
void video_present(unsigned int width, unsigned int height);

/* audio.c: ring between the emulation and the codec */
int audio_init(unsigned int rate);
/** Put stereo frames in, returns how many were taken */
size_t audio_push(const int16_t *pcm, size_t frames);
/** Stereo frames waiting to be played, including the blocks in the codec */
size_t audio_queued(void);
unsigned int audio_underruns(void);
unsigned int audio_starved(void);
unsigned int audio_slab_waits(void);

/* pad.c: on-screen keys, NESEMU_KEY_* mask */
int pad_start(void);
uint32_t pad_keys(void);

#endif
