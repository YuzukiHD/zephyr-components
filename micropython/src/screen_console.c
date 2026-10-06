/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mirror of the MicroPython console on the LCD: everything the REPL prints is also drawn
 * into a character grid on the frame buffer plane (10x16 cells, green on black). The port's
 * stdout function is wrapped at link time (-Wl,--wrap=mp_hal_stdout_tx_strn), so the UART
 * keeps working as before and is still the keyboard.
 */

#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

#define CELL_W 10
#define CELL_H 16
#define FIRST_CHAR 32
#define LAST_CHAR 126

#define COLOR_FG 0x07e0 /* green */
#define COLOR_BG 0x0000

/* 10 columns of 2 bytes per glyph, least significant bit is the top row */
extern const uint8_t cfb_font_1016[LAST_CHAR - FIRST_CHAR + 1][20];

extern uintptr_t __real_mp_hal_stdout_tx_strn(const char *str, uintptr_t len);

#define MAX_COLS 128
#define MAX_ROWS 40

static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static K_MUTEX_DEFINE(lock);

static char grid[MAX_ROWS][MAX_COLS];
static uint16_t band[MAX_COLS * CELL_W * CELL_H] __aligned(64);
static uint64_t dirty; /* one bit per row */
static int cols, rows, cx, cy;
static bool ready, failed;
static enum { ST_NORMAL, ST_ESC, ST_CSI } state;
static int csi_arg;
static bool csi_have_arg;

static void draw_row(int r)
{
	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(uint16_t) * cols * CELL_W * CELL_H,
		.width = cols * CELL_W,
		.height = CELL_H,
		.pitch = cols * CELL_W,
	};
	int stride = cols * CELL_W;

	for (int c = 0; c < cols; c++) {
		unsigned char ch = grid[r][c];
		const uint8_t *g = (ch >= FIRST_CHAR && ch <= LAST_CHAR) ?
					   cfb_font_1016[ch - FIRST_CHAR] : NULL;
		bool cursor = (r == cy && c == cx);

		for (int x = 0; x < CELL_W; x++) {
			uint16_t bits = g ? (g[x * 2] | (g[x * 2 + 1] << 8)) : 0;

			for (int y = 0; y < CELL_H; y++) {
				bool on = (bits >> y) & 1;

				if (cursor) {
					on = !on;
				}
				band[y * stride + c * CELL_W + x] = on ? COLOR_FG : COLOR_BG;
			}
		}
	}
	display_write(disp, 0, r * CELL_H, &desc, band);
}

static void flush(void)
{
	for (int r = 0; r < rows; r++) {
		if (dirty & (1ULL << r)) {
			draw_row(r);
		}
	}
	dirty = 0;
}

static void clear_row(int r, int from)
{
	memset(&grid[r][from], ' ', cols - from);
	dirty |= 1ULL << r;
}

static void scroll(void)
{
	memmove(grid[0], grid[1], sizeof(grid[0]) * (rows - 1));
	clear_row(rows - 1, 0);
	dirty = (1ULL << rows) - 1;
}

static void newline(void)
{
	if (cy + 1 >= rows) {
		scroll();
	} else {
		cy++;
	}
}

static void csi_final(char f)
{
	int n = csi_have_arg ? csi_arg : 1;

	switch (f) {
	case 'D':
		cx = MAX(0, cx - n);
		break;
	case 'C':
		cx = MIN(cols - 1, cx + n);
		break;
	case 'K':
		clear_row(cy, cx);
		break;
	default:
		break;
	}
}

static void put(char c)
{
	switch (state) {
	case ST_ESC:
		if (c == '[') {
			state = ST_CSI;
			csi_arg = 0;
			csi_have_arg = false;
		} else {
			state = ST_NORMAL;
		}
		return;
	case ST_CSI:
		if (c >= '0' && c <= '9') {
			csi_arg = csi_arg * 10 + (c - '0');
			csi_have_arg = true;
		} else if (c == ';') {
			csi_arg = 0;
		} else {
			csi_final(c);
			state = ST_NORMAL;
		}
		return;
	default:
		break;
	}

	switch (c) {
	case 0x1b:
		state = ST_ESC;
		break;
	case '\r':
		cx = 0;
		break;
	case '\n':
		newline();
		break;
	case '\b':
		cx = MAX(0, cx - 1);
		break;
	case '\t':
		cx = MIN(cols - 1, (cx + 8) & ~7);
		break;
	default:
		if (c < FIRST_CHAR || c > LAST_CHAR) {
			break;
		}
		if (cx >= cols) {
			cx = 0;
			newline();
		}
		grid[cy][cx++] = c;
		dirty |= 1ULL << cy;
		break;
	}
}

static void screen_init(void)
{
	struct display_capabilities caps;

	if (!device_is_ready(disp)) {
		failed = true;
		return;
	}
	display_get_capabilities(disp, &caps);
	if (caps.current_pixel_format != PIXEL_FORMAT_RGB_565) {
		failed = true;
		return;
	}
	cols = MIN(caps.x_resolution / CELL_W, MAX_COLS);
	rows = MIN(caps.y_resolution / CELL_H, MAX_ROWS);
	for (int r = 0; r < rows; r++) {
		memset(grid[r], ' ', MAX_COLS);
	}
	dirty = (1ULL << rows) - 1;
	ready = true;
}

uintptr_t __wrap_mp_hal_stdout_tx_strn(const char *str, uintptr_t len)
{
	uintptr_t ret = __real_mp_hal_stdout_tx_strn(str, len);

	if (failed) {
		return ret;
	}
	k_mutex_lock(&lock, K_FOREVER);
	if (!ready) {
		screen_init();
	}
	if (ready) {
		int old_cy = cy;

		for (uintptr_t i = 0; i < len; i++) {
			put(str[i]);
		}
		dirty |= (1ULL << old_cy) | (1ULL << cy);
		flush();
	}
	k_mutex_unlock(&lock);
	return ret;
}
