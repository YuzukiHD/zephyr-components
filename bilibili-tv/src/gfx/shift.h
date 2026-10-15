/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BILI_SHIFT_H_
#define BILI_SHIFT_H_

#include <stdbool.h>
#include <stdint.h>

/** Whether the picture can be moved (the accelerator and the frame buffer are there) */
bool shift_ready(void);

/**
 * Moves the rows [y, y + h) of the picture on the screen up by @p dy rows (down for a negative
 * @p dy), starting at the next vertical blank. The part that stays in the area is copied, the rows
 * that come in at the other side are filled with the colour of the page and have to be drawn by the
 * caller. 0 or a negative errno (then the picture may be half moved: draw all of it).
 */
int shift_rows(int y, int h, int dy);

/**
 * Moves the columns of the rectangle (x, y, w, h) of the picture to the left by @p dx pixels (dx > 0),
 * starting at the next vertical blank. The part that stays in the rectangle is copied, the @p dx
 * columns that come in at the right are made transparent and have to be drawn by the caller.
 * With @p sync false the move does not wait for the blank (a few pixels of a moving picture may show a
 * seam for one frame, which a small step does not make visible).
 * -ENOTSUP when the accelerator does not copy to the left inside a buffer correctly (checked once).
 */
int shift_cols(int x, int y, int w, int h, int dx, bool sync);

/** Counts and times (microseconds) of shift_cols() since the last call: calls, in the wait for the blank, in all */
void shift_cols_times(uint32_t *n, uint32_t *wait_us, uint32_t *total_us);

/** Gives back the memory of the second buffer (when a video needs it) */
void shift_release(void);

#endif /* BILI_SHIFT_H_ */
