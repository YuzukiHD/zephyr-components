/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Game Boy Advance emulator (mGBA core) for Zephyr
 *
 * One emulated console at a time. The ROM and the save file are read through
 * the Zephyr file system, the picture is RGB565 and the sound is signed 16 bit
 * stereo at the rate the caller asks for.
 */

#ifndef MGBA_ZEPHYR_GBA_H_
#define MGBA_ZEPHYR_GBA_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GBA_WIDTH  240
#define GBA_HEIGHT 160
/** Frame rate of the console, 16777216 / 280896 */
#define GBA_FPS_X1000 59727

/** Key bits for gba_set_keys() */
#define GBA_KEY_A      BIT(0)
#define GBA_KEY_B      BIT(1)
#define GBA_KEY_SELECT BIT(2)
#define GBA_KEY_START  BIT(3)
#define GBA_KEY_RIGHT  BIT(4)
#define GBA_KEY_LEFT   BIT(5)
#define GBA_KEY_UP     BIT(6)
#define GBA_KEY_DOWN   BIT(7)
#define GBA_KEY_R      BIT(8)
#define GBA_KEY_L      BIT(9)

/**
 * @brief Load a ROM and start the console
 *
 * The file extension picks the core: .gba is a Game Boy Advance, .gb and .gbc
 * are Game Boy and Game Boy Color.
 *
 * @param rom_path  .gba file
 * @param save_path battery save file, loaded when it exists (NULL: no save)
 * @param out_rate  sample rate gba_audio_read() delivers
 * @retval 0 on success, negative errno otherwise
 */
int gba_open(const char *rom_path, const char *save_path, unsigned int out_rate);

void gba_close(void);

/**
 * @brief Emulate until the next frame is complete
 *
 * @return 1 when the frame was drawn into the frame buffer, 0 when it was skipped
 */
int gba_run_frame(void);

/** Draw the following frames into @p buf (GBA_WIDTH x GBA_HEIGHT RGB565, row pitch GBA_WIDTH) */
void gba_set_frame_buffer(uint16_t *buf);

/**
 * Draw one frame, then skip @p skip frames, and so on: a skipped frame runs the emulation and the
 * sound but not the renderer, which makes it cheaper. 0 draws every frame.
 */
void gba_set_frameskip(int skip);

/**
 * The last finished frame, RGB565 with GBA_WIDTH pixels per row; the picture
 * itself is *width x *height (240x160, or 160x144 for a Game Boy)
 */
const uint16_t *gba_frame(unsigned int *width, unsigned int *height);

void gba_set_keys(uint32_t keys);

/**
 * @brief Take sound from the emulator
 *
 * @return number of stereo frames written to @p out (at most @p frames)
 */
size_t gba_audio_read(int16_t *out, size_t frames);

/** Stereo frames the emulator holds ready at the output rate */
size_t gba_audio_available(void);

/** Write the save file when the save memory changed; returns 1 when written */
int gba_save_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* MGBA_ZEPHYR_GBA_H_ */
