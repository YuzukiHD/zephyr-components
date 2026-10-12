/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Vector versions of the blend functions of the LVGL software renderer for the ARGB8888 target.
 * LVGL includes this file in its blend sources (LV_DRAW_SW_ASM_CUSTOM_INCLUDE); a hook that
 * returns LV_RESULT_INVALID leaves the work to the scalar code of LVGL, so a case that is not
 * handled here is still correct.
 */

#ifndef LV_RVV_H_
#define LV_RVV_H_

#include <stdbool.h>
#include <stddef.h>

#include "misc/lv_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Switches the vector code on or off at run time (on by default); used by the self test */
void lv_rvv_enable(bool on);

/** Compares the vector blends with the scalar code of LVGL on random data; true when equal */
bool lv_rvv_selftest(void);

/* the return values are lv_result_t: LV_RESULT_OK when the blend was done, LV_RESULT_INVALID if not */
int lv_rvv_color_fill(lv_draw_sw_blend_fill_dsc_t *dsc);
int lv_rvv_color_opa(lv_draw_sw_blend_fill_dsc_t *dsc);
int lv_rvv_color_mask(lv_draw_sw_blend_fill_dsc_t *dsc);
int lv_rvv_color_mask_opa(lv_draw_sw_blend_fill_dsc_t *dsc);
int lv_rvv_argb_normal(lv_draw_sw_blend_image_dsc_t *dsc);
int lv_rvv_argb_opa(lv_draw_sw_blend_image_dsc_t *dsc);
int lv_rvv_argb_mask(lv_draw_sw_blend_image_dsc_t *dsc);
int lv_rvv_argb_mask_opa(lv_draw_sw_blend_image_dsc_t *dsc);

#ifdef CONFIG_MP5_GFX_RVV
#define LV_DRAW_SW_COLOR_BLEND_TO_ARGB8888(dsc)				lv_rvv_color_fill(dsc)
#define LV_DRAW_SW_COLOR_BLEND_TO_ARGB8888_WITH_OPA(dsc)		lv_rvv_color_opa(dsc)
#define LV_DRAW_SW_COLOR_BLEND_TO_ARGB8888_WITH_MASK(dsc)		lv_rvv_color_mask(dsc)
#define LV_DRAW_SW_COLOR_BLEND_TO_ARGB8888_MIX_MASK_OPA(dsc)		lv_rvv_color_mask_opa(dsc)
#define LV_DRAW_SW_ARGB8888_BLEND_NORMAL_TO_ARGB8888(dsc)		lv_rvv_argb_normal(dsc)
#define LV_DRAW_SW_ARGB8888_BLEND_NORMAL_TO_ARGB8888_WITH_OPA(dsc)	lv_rvv_argb_opa(dsc)
#define LV_DRAW_SW_ARGB8888_BLEND_NORMAL_TO_ARGB8888_WITH_MASK(dsc)	lv_rvv_argb_mask(dsc)
#define LV_DRAW_SW_ARGB8888_BLEND_NORMAL_TO_ARGB8888_MIX_MASK_OPA(dsc)	lv_rvv_argb_mask_opa(dsc)
#endif /* CONFIG_MP5_GFX_RVV */

#ifdef __cplusplus
}
#endif

#endif /* LV_RVV_H_ */
