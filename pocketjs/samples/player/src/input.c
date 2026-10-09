/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Input of the player:
 *  - the touch panel (chosen zephyr,touch): fingers, one per slot of the controller
 *  - gestures on the first finger as buttons, for the apps that are made for a d-pad
 *  - the keys of the console UART as buttons (the terminal sends no release, a key
 *    holds its button for a moment)
 */

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>

#include "input.h"

#define HAS_TOUCH DT_NODE_EXISTS(DT_CHOSEN(zephyr_touch))

#define PULSE_MS	110
#define SWIPE_STEP	36	/* screen pixels of movement per direction pulse */
#define TAP_MAX_MS	450
#define TAP_MAX_MOVE	20

static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static unsigned int logical_w, logical_h;
static int screen_w, screen_h;

static struct k_spinlock lock;
static uint32_t pulse_until[16];

static void pulse(uint32_t bits)
{
	uint32_t until = k_uptime_get_32() + PULSE_MS;

	for (int i = 0; i < 16; i++) {
		if (bits & BIT(i)) {
			pulse_until[i] = until;
		}
	}
}

static uint32_t pulsed(void)
{
	uint32_t now = k_uptime_get_32(), bits = 0;

	for (int i = 0; i < 16; i++) {
		if (pulse_until[i] != 0U && (int32_t)(pulse_until[i] - now) > 0) {
			bits |= BIT(i);
		}
	}
	return bits;
}

#if HAS_TOUCH
static struct {
	int x, y;
	bool down;
} fingers[INPUT_MAX_TOUCHES];
static int cur_slot;

static void touch_event(struct input_event *evt, void *user_data)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	switch (evt->code) {
	case INPUT_ABS_MT_SLOT:
		cur_slot = CLAMP(evt->value, 0, INPUT_MAX_TOUCHES - 1);
		break;
	case INPUT_ABS_X:
		fingers[cur_slot].x = evt->value;
		break;
	case INPUT_ABS_Y:
		fingers[cur_slot].y = evt->value;
		break;
	case INPUT_BTN_TOUCH:
		fingers[cur_slot].down = evt->value != 0;
		break;
	}
	k_spin_unlock(&lock, key);
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_CHOSEN(zephyr_touch)), touch_event, NULL);
#endif /* HAS_TOUCH */

/* ---- console keys ------------------------------------------------------------- */

static uint32_t key_buttons(int c)
{
	switch (c) {
	case 'w': case 'W': return BTN_UP;
	case 's': case 'S': return BTN_DOWN;
	case 'a': case 'A': return BTN_LEFT;
	case 'd': case 'D': return BTN_RIGHT;
	case 'j': case 'J': case ' ': return BTN_CROSS;
	case 'k': case 'K': return BTN_CIRCLE;
	case 'u': case 'U': return BTN_SQUARE;
	case 'i': case 'I': return BTN_TRIANGLE;
	case 'q': case 'Q': return BTN_LTRIGGER;
	case 'e': case 'E': return BTN_RTRIGGER;
	case '\r': case '\n': return BTN_START;
	case '\t': case 'b': return BTN_SELECT;
	default: return 0;
	}
}

#define UART_NODE DT_CHOSEN(zephyr_console)

K_THREAD_STACK_DEFINE(uart_stack, 1024);
static struct k_thread uart_thread;

static void uart_main(void *a, void *b, void *c)
{
	const struct device *uart = DEVICE_DT_GET(UART_NODE);
	unsigned char ch;

	while (true) {
		if (uart_poll_in(uart, &ch) == 0) {
			uint32_t bits = key_buttons(ch);

			if (bits != 0U) {
				k_spinlock_key_t key = k_spin_lock(&lock);

				pulse(bits);
				k_spin_unlock(&lock, key);
			}
		} else {
			k_msleep(5);
		}
	}
}

void input_init(unsigned int w, unsigned int h)
{
	struct display_capabilities caps;

	logical_w = w;
	logical_h = h;
	display_get_capabilities(disp, &caps);
	screen_w = caps.x_resolution;
	screen_h = caps.y_resolution;
	if (device_is_ready(DEVICE_DT_GET(UART_NODE))) {
		k_thread_create(&uart_thread, uart_stack, K_THREAD_STACK_SIZEOF(uart_stack),
				uart_main, NULL, NULL, NULL, 7, 0, K_NO_WAIT);
		k_thread_name_set(&uart_thread, "keys");
	}
}

/* screen pixel -> logical pixel of the centred, aspect kept picture; false outside it */
static bool to_logical(int sx, int sy, int *lx, int *ly)
{
	/* the picture is scaled by the smaller factor, in 1/256 steps */
	int64_t sc = MIN((int64_t)screen_w * 256 / logical_w, (int64_t)screen_h * 256 / logical_h);
	int pw = logical_w * sc / 256, ph = logical_h * sc / 256;
	int ox = (screen_w - pw) / 2, oy = (screen_h - ph) / 2;

	if (sc == 0 || sx < ox || sy < oy || sx >= ox + pw || sy >= oy + ph) {
		return false;
	}
	*lx = CLAMP((int)((int64_t)(sx - ox) * 256 / sc), 0, (int)logical_w - 1);
	*ly = CLAMP((int)((int64_t)(sy - oy) * 256 / sc), 0, (int)logical_h - 1);
	return true;
}

#if HAS_TOUCH
/* swipes and taps of the first finger as buttons; called with the lock held */
static void gesture(bool down, int x, int y)
{
	static bool tracking, moved;
	static int ox, oy, sx, sy;
	static uint32_t t0;
	uint32_t now = k_uptime_get_32();

	if (down) {
		if (!tracking) {
			tracking = true;
			moved = false;
			ox = sx = x;
			oy = sy = y;
			t0 = now;
			return;
		}
		int dx = x - ox, dy = y - oy;

		if (MAX(abs(dx), abs(dy)) >= SWIPE_STEP) {
			if (abs(dx) >= abs(dy)) {
				pulse(dx > 0 ? BTN_RIGHT : BTN_LEFT);
			} else {
				pulse(dy > 0 ? BTN_DOWN : BTN_UP);
			}
			moved = true;
			ox = x;
			oy = y;
		}
	} else if (tracking) {
		tracking = false;
		if (!moved && now - t0 < TAP_MAX_MS &&
		    MAX(abs(x - sx), abs(y - sy)) <= TAP_MAX_MOVE) {
			pulse(BTN_CROSS);
		}
	}
}
#endif

void input_sample(pocketjs_ui_input_t *input, pocketjs_ui_touch_t *touches)
{
	size_t n = 0;
	uint32_t buttons;
	k_spinlock_key_t key = k_spin_lock(&lock);

#if HAS_TOUCH
	static int last_x, last_y;

	for (int f = 0; f < INPUT_MAX_TOUCHES; f++) {
		int lx, ly;

		if (fingers[f].down && to_logical(fingers[f].x, fingers[f].y, &lx, &ly)) {
			touches[n++] = (pocketjs_ui_touch_t){.id = f, .x = lx, .y = ly};
		}
	}
	if (fingers[0].down) {
		last_x = fingers[0].x;
		last_y = fingers[0].y;
	}
	gesture(fingers[0].down, last_x, last_y);
#endif
	buttons = pulsed();
	k_spin_unlock(&lock, key);

	input->buttons = buttons;
	input->touches = touches;
	input->touch_count = n;
}
