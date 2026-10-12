/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Game Boy Advance emulator (gpSP core) for Zephyr
 *
 * One emulated console at a time. The ROM and the save file are read through
 * the Zephyr file system, the picture is RGB565 and the sound is signed 16 bit
 * stereo at the rate the caller asks for.
 */

#ifndef GBSP_ZEPHYR_GBSP_H_
#define GBSP_ZEPHYR_GBSP_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GBSP_WIDTH  240
#define GBSP_HEIGHT 160
/**
 * Size of a frame buffer the core draws into: the picture and one more line, which the object
 * window pass uses as scratch. A buffer of GBSP_WIDTH x GBSP_HEIGHT only is overrun by 480 bytes.
 */
#define GBSP_FRAME_BYTES ((GBSP_WIDTH) * ((GBSP_HEIGHT) + 1) * 2)

/** Frame rate of the console, 16777216 / 280896 */
#define GBSP_FPS_X1000 59727

/** Key bits for gbsp_set_keys() */
#define GBSP_KEY_A      BIT(0)
#define GBSP_KEY_B      BIT(1)
#define GBSP_KEY_SELECT BIT(2)
#define GBSP_KEY_START  BIT(3)
#define GBSP_KEY_RIGHT  BIT(4)
#define GBSP_KEY_LEFT   BIT(5)
#define GBSP_KEY_UP     BIT(6)
#define GBSP_KEY_DOWN   BIT(7)
#define GBSP_KEY_R      BIT(8)
#define GBSP_KEY_L      BIT(9)

/**
 * @brief Load a ROM and start the console
 *
 * Only Game Boy Advance ROMs (.gba) are supported by this core.
 *
 * @param rom_path  .gba file
 * @param save_path battery save file, loaded when it exists (NULL: no save)
 * @param out_rate  sample rate gbsp_audio_read() delivers
 * @retval 0 on success, negative errno otherwise
 */
int gbsp_open(const char *rom_path, const char *save_path, unsigned int out_rate);

void gbsp_close(void);

/**
 * @brief Emulate until the next frame is complete
 *
 * @return 1 when the frame was drawn into the frame buffer, 0 when it was skipped
 */
int gbsp_run_frame(void);

/** Draw the following frames into @p buf (GBSP_WIDTH x GBSP_HEIGHT RGB565, row pitch GBSP_WIDTH) */
void gbsp_set_frame_buffer(uint16_t *buf);

/**
 * Draw one frame, then skip @p skip frames, and so on: a skipped frame runs the emulation and the
 * sound but not the renderer, which makes it cheaper. 0 draws every frame.
 */
void gbsp_set_frameskip(int skip);

/**
 * The last finished frame, RGB565 with GBSP_WIDTH pixels per row; the picture
 * itself is *width x *height (240x160, or 160x144 for a Game Boy)
 */
const uint16_t *gbsp_frame(unsigned int *width, unsigned int *height);

void gbsp_set_keys(uint32_t keys);

/**
 * @brief Take sound from the emulator
 *
 * @return number of stereo frames written to @p out (at most @p frames)
 */
size_t gbsp_audio_read(int16_t *out, size_t frames);

/** Stereo frames the emulator holds ready at the output rate */
size_t gbsp_audio_available(void);

/** Write the save file when the save memory changed; returns 1 when written */
int gbsp_save_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* GBSP_ZEPHYR_GBSP_H_ */
