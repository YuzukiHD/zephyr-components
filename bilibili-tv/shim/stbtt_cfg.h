/* Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * Included into the LVGL sources: the TrueType rasteriser uses assert() as the statement of an
 * if/else chain, which the assert of Zephyr (a block) breaks.
 */
#pragma once
#define STBTT_assert(x) ((void)0)
