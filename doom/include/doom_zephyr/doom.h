/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Doom engine (doomgeneric) for Zephyr
 *
 * The game runs in the calling thread: doom_open() sets everything up (loads
 * the WAD, starts the first demo loop) and doom_run_frame() does one pass of the
 * engine loop. The picture is 320x200 RGB565.
 */

#ifndef DOOM_ZEPHYR_DOOM_H_
#define DOOM_ZEPHYR_DOOM_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOOM_WIDTH  320
#define DOOM_HEIGHT 200

/** Keys for doom_key(), the codes of the engine; letters and digits are their ASCII lower case */
#define DOOM_KEY_RIGHT   0xae
#define DOOM_KEY_LEFT    0xac
#define DOOM_KEY_UP      0xad
#define DOOM_KEY_DOWN    0xaf
#define DOOM_KEY_STRAFE_L 0xa0
#define DOOM_KEY_STRAFE_R 0xa1
#define DOOM_KEY_USE     0xa2
#define DOOM_KEY_FIRE    0xa3
#define DOOM_KEY_ESCAPE  27
#define DOOM_KEY_ENTER   13
#define DOOM_KEY_TAB     9
#define DOOM_KEY_SHIFT   (0x80 + 0x36)
#define DOOM_KEY_CTRL    (0x80 + 0x1d)
#define DOOM_KEY_ALT     (0x80 + 0x38)
#define DOOM_KEY_Y       'y'
#define DOOM_KEY_N       'n'

/**
 * @brief Load the game data and start the engine
 *
 * @param iwad_path WAD file (doom1.wad, doom.wad, doom2.wad, freedoom...)
 * @param base_dir  directory for the config file and the saved games (NULL: the
 *                  directory of the WAD)
 * @retval 0 on success, negative errno otherwise. Fatal errors of the engine
 *         (missing lumps) stop the system.
 */
int doom_open(const char *iwad_path, const char *base_dir);

/**
 * @brief Run the engine for one pass: input, the game tics that are due (it
 *        waits for the next one when there is none), sound, and the picture
 *
 * @return 1 when the frame buffer holds a new picture
 */
int doom_run_frame(void);

/** The following pictures are drawn into @p buf (DOOM_WIDTH x DOOM_HEIGHT RGB565, pitch DOOM_WIDTH) */
void doom_set_frame_buffer(uint16_t *buf);

/** Key press or release; can be called from any thread */
void doom_key(int pressed, unsigned char key);

/**
 * @brief Take mixed sound effects from the engine, 16 bit stereo at the rate of doom_audio_init()
 *
 * @return number of stereo frames written (all of @p frames: silence when nothing plays)
 */
size_t doom_audio_read(int16_t *out, size_t frames);

/** The sample rate of doom_audio_read(), set before doom_open() */
void doom_audio_init(unsigned int rate);

#ifdef __cplusplus
}
#endif

#endif /* DOOM_ZEPHYR_DOOM_H_ */
