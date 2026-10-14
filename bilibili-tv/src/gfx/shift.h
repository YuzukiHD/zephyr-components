/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BILI_SHIFT_H_
#define BILI_SHIFT_H_

#include <stdbool.h>

/** Whether the picture can be moved (the accelerator and the frame buffer are there) */
bool shift_ready(void);

/**
 * Moves the rows [y, y + h) of the picture on the screen up by @p dy rows (down for a negative
 * @p dy), starting at the next vertical blank. The part that stays in the area is copied, the rows
 * that come in at the other side are filled with the colour of the page and have to be drawn by the
 * caller. 0 or a negative errno (then the picture may be half moved: draw all of it).
 */
int shift_rows(int y, int h, int dy);

/** Gives back the memory of the second buffer (when a video needs it) */
void shift_release(void);

#endif /* BILI_SHIFT_H_ */
