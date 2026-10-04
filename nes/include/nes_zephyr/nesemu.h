/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief NES / Famicom emulator (nofrendo core) for Zephyr
 *
 * One emulated console at a time. The ROM (iNES) and the battery save are
 * read through the Zephyr file system, the picture is RGB565 and the sound is
 * signed 16 bit stereo at the rate the caller asks for.
 */

#ifndef NES_ZEPHYR_NESEMU_H_
#define NES_ZEPHYR_NESEMU_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NESEMU_WIDTH  256
/** Height of the picture of an NTSC console (the 8 overscan lines on top and bottom are cut) */
#define NESEMU_HEIGHT 224

/** Key bits for nesemu_set_keys() */
#define NESEMU_KEY_A      BIT(0)
#define NESEMU_KEY_B      BIT(1)
#define NESEMU_KEY_SELECT BIT(2)
#define NESEMU_KEY_START  BIT(3)
#define NESEMU_KEY_UP     BIT(4)
#define NESEMU_KEY_DOWN   BIT(5)
#define NESEMU_KEY_LEFT   BIT(6)
#define NESEMU_KEY_RIGHT  BIT(7)

/**
 * @brief Load a ROM and start the console
 *
 * @param rom_path  .nes file
 * @param save_path battery save file, loaded when it exists (NULL: no save)
 * @param out_rate  sample rate nesemu_audio_read() delivers
 * @retval 0 on success, negative errno otherwise
 */
int nesemu_open(const char *rom_path, const char *save_path, unsigned int out_rate);

void nesemu_close(void);

/**
 * @brief Emulate until the next frame is complete
 *
 * @return 1 when the frame was drawn into the frame buffer, 0 when it was skipped
 */
int nesemu_run_frame(void);

/** Draw the following frames into @p buf (NESEMU_WIDTH x picture height RGB565, row pitch NESEMU_WIDTH) */
void nesemu_set_frame_buffer(uint16_t *buf);

/** Draw one frame, then skip @p skip frames (0 draws every frame) */
void nesemu_set_frameskip(int skip);

/** Visible lines of the picture: 224 for NTSC, 240 for PAL */
unsigned int nesemu_height(void);

/** Frame rate of the console times 1000 (60099 NTSC, 50007 PAL) */
unsigned int nesemu_fps_x1000(void);

void nesemu_set_keys(uint32_t keys);

/** Reset the console (soft reset, the battery RAM is kept) */
void nesemu_reset(void);

/**
 * @brief Take sound from the emulator
 *
 * @return number of stereo frames written to @p out (at most @p frames)
 */
size_t nesemu_audio_read(int16_t *out, size_t frames);

/** Write the save file when the battery RAM changed; returns 1 when written */
int nesemu_save_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* NES_ZEPHYR_NESEMU_H_ */
