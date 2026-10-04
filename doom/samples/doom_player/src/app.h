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

/* audio.c: sound effects of the engine to the codec */
int audio_init(unsigned int rate);
unsigned int audio_underruns(void);

/* pad.c: on-screen keys */
int pad_start(void);

#endif
