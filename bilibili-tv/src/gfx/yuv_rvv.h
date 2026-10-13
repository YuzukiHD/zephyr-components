/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BILI_YUV_RVV_H_
#define BILI_YUV_RVV_H_

#include <stdint.h>

/*
 * NV12 (luma plane, then interleaved Cb/Cr at half height) to RGB565, one output pixel per input
 * pixel. @p width must be even. The strides are in bytes (input) and in pixels (output).
 */
void yuv_nv12_to_rgb565_rvv(const uint8_t *y, int y_stride, const uint8_t *uv, int uv_stride, uint16_t *out,
			    int out_stride, int width, int height);

#endif /* BILI_YUV_RVV_H_ */
