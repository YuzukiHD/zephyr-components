/* SPDX-License-Identifier: Apache-2.0 */
/*
 * The G2D accelerator behind the hooks of the RGB565 renderer: big opaque fills are G2D
 * fills, A8 coverage blends are expanded to ARGB8888 (vector unit) and composed by the G2D.
 * Small operations stay with the CPU, the hardware round trip costs more than they do.
 */
#pragma once

#include "pocketjs/render_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pocketjs_g2d pocketjs_g2d_t;

/** Minimum areas (pixels) for the G2D; 0 takes the values of Kconfig. */
typedef struct {
	uint32_t min_fill_pixels;
	uint32_t min_blend_pixels;
} pocketjs_g2d_config_t;

pocketjs_g2d_t *pocketjs_g2d_create(const pocketjs_g2d_config_t *config);
const pocketjs_rgb565_accelerator_t *pocketjs_g2d_accelerator(pocketjs_g2d_t *g2d);

/** Pixel count at or above which fills / blends are worth the G2D (see config). */
uint32_t pocketjs_g2d_min_fill(const pocketjs_g2d_t *g2d);
uint32_t pocketjs_g2d_min_blend(const pocketjs_g2d_t *g2d);

#ifdef __cplusplus
}
#endif
