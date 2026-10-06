/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CHUNKED_H_
#define CHUNKED_H_

#include <stdint.h>
#include <zephyr/device.h>
#include <litehtml_zephyr/lh.h>

/**
 * Show the page in the file piece by piece and scroll it. Returns only on an error.
 */
int chunked_run(const char *path, const struct lh_font *font, const struct device *disp,
		uint16_t *fbs[2]);

#endif /* CHUNKED_H_ */
