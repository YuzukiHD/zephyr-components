/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LV_G2D_H_
#define LV_G2D_H_

/** Adds the draw unit of the 2D accelerator to LVGL; call it after lv_init() */
void lv_g2d_init(void);

/** Prints how many rectangles the accelerator filled since the last call */
void lv_g2d_report(void);

#endif /* LV_G2D_H_ */
