/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "pocketjs/ui_qjs.h"

/* PSP button bits of the PocketJS frame contract */
#define BTN_SELECT	0x0001
#define BTN_START	0x0008
#define BTN_UP		0x0010
#define BTN_RIGHT	0x0020
#define BTN_DOWN	0x0040
#define BTN_LEFT	0x0080
#define BTN_LTRIGGER	0x0100
#define BTN_RTRIGGER	0x0200
#define BTN_TRIANGLE	0x1000
#define BTN_CIRCLE	0x2000
#define BTN_CROSS	0x4000
#define BTN_SQUARE	0x8000

#define INPUT_MAX_TOUCHES 5

/*
 * Starts the readers of the touch panel and of the console. The picture of
 * logical_w x logical_h is shown centred on the screen with its aspect kept.
 */
void input_init(unsigned int logical_w, unsigned int logical_h);

/*
 * The state for this tick: the fingers in logical coordinates, and the buttons of the
 * console keys plus the ones the touch gestures stand for (a swipe is a direction,
 * a tap is CROSS). touches[] must hold INPUT_MAX_TOUCHES entries.
 */
void input_sample(pocketjs_ui_input_t *input, pocketjs_ui_touch_t *touches);
