/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * NV12 to RGB565 with the vector unit, for a picture that is drawn at its own size. The result is
 * bit for bit the one of the scalar code in cover.c (full range BT.601 with the same integer
 * factors); the products are split so that they fit 16 bits where the scalar code uses 32.
 */

#include <riscv_vector.h>

#include "gfx/yuv_rvv.h"

void yuv_nv12_to_rgb565_rvv(const uint8_t *y, int y_stride, const uint8_t *uv, int uv_stride, uint16_t *out,
			    int out_stride, int width, int height)
{
	for (int row = 0; row < height; row++) {
		const uint8_t *yr = y + row * y_stride;
		const uint8_t *uvr = uv + (row / 2) * uv_stride;
		uint16_t *dst = out + row * out_stride;

		for (int x = 0; x < width;) {
			size_t vl = __riscv_vsetvl_e16m2((size_t)(width - x));
			vuint8m1_t id = __riscv_vid_v_u8m1(vl);
			vuint8m1_t uv8 = __riscv_vle8_v_u8m1(uvr + x, vl);
			vuint8m1_t u8 = __riscv_vrgather_vv_u8m1(uv8, __riscv_vand_vx_u8m1(id, 0xfe, vl), vl);
			vuint8m1_t v8 = __riscv_vrgather_vv_u8m1(uv8, __riscv_vor_vx_u8m1(id, 1, vl), vl);
			vint16m2_t yy = __riscv_vreinterpret_v_u16m2_i16m2(
				__riscv_vzext_vf2_u16m2(__riscv_vle8_v_u8m1(yr + x, vl), vl));
			vint16m2_t u = __riscv_vsub_vx_i16m2(
				__riscv_vreinterpret_v_u16m2_i16m2(__riscv_vzext_vf2_u16m2(u8, vl)), 128, vl);
			vint16m2_t v = __riscv_vsub_vx_i16m2(
				__riscv_vreinterpret_v_u16m2_i16m2(__riscv_vzext_vf2_u16m2(v8, vl)), 128, vl);
			/* (359 * v) >> 8 = v + ((103 * v) >> 8) */
			vint16m2_t r = __riscv_vadd_vv_i16m2(
				yy, __riscv_vadd_vv_i16m2(v, __riscv_vsra_vx_i16m2(__riscv_vmul_vx_i16m2(v, 103, vl), 8, vl),
							  vl), vl);
			vint32m4_t acc = __riscv_vwmul_vx_i32m4(u, 88, vl);
			vint16m2_t g;
			vint16m2_t b;
			vuint16m2_t o;

			acc = __riscv_vwmacc_vx_i32m4(acc, 183, v, vl);
			g = __riscv_vsub_vv_i16m2(yy, __riscv_vnsra_wx_i16m2(acc, 8, vl), vl);
			b = __riscv_vadd_vv_i16m2(
				yy, __riscv_vnsra_wx_i16m2(__riscv_vwmul_vx_i32m4(u, 454, vl), 8, vl), vl);
			r = __riscv_vmin_vx_i16m2(__riscv_vmax_vx_i16m2(r, 0, vl), 255, vl);
			g = __riscv_vmin_vx_i16m2(__riscv_vmax_vx_i16m2(g, 0, vl), 255, vl);
			b = __riscv_vmin_vx_i16m2(__riscv_vmax_vx_i16m2(b, 0, vl), 255, vl);
			o = __riscv_vsll_vx_u16m2(
				__riscv_vand_vx_u16m2(__riscv_vreinterpret_v_i16m2_u16m2(r), 0xf8, vl), 8, vl);
			o = __riscv_vor_vv_u16m2(
				o, __riscv_vsll_vx_u16m2(
					   __riscv_vand_vx_u16m2(__riscv_vreinterpret_v_i16m2_u16m2(g), 0xfc, vl), 3, vl),
				vl);
			o = __riscv_vor_vv_u16m2(
				o, __riscv_vsrl_vx_u16m2(__riscv_vreinterpret_v_i16m2_u16m2(b), 3, vl), vl);
			__riscv_vse16_v_u16m2(dst + x, o, vl);
			x += (int)vl;
		}
	}
}
