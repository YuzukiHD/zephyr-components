/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The USB second screen: the board enumerates as a virtual display of the PC. The PC sends its
 * desktop as JPEG frames over a bulk endpoint, the video engine decodes them as a motion JPEG
 * stream and the pictures go to the video plane. The cross at the corner leaves the mode; the
 * USB device is stopped and the decoder is given back.
 */

#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/drivers/vdec.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/printk.h>

#include "usbd_core.h"
#include "usbd_display.h"
#include <zephyr/input/input.h>
#include "usb_touch.h"
#include "ui/ui.h"

extern uintptr_t usb_sunxi_otg_base(void);

#define DISPLAY_IN_EP	0x81
#define DISPLAY_OUT_EP	0x02
#define DISPLAY_EP_MPS	512
#define USBD_VID	0x303A
#define USBD_PID_TOUCH	0x2986	/* composite: the display on interface 0, a touch screen on 1 */
#define USBD_MAX_POWER	100
#define USB_CONFIG_SIZE	(9 + 9 + 7 + 7)
#define USB_CONFIG_SIZE_TOUCH	(USB_CONFIG_SIZE + USB_TOUCH_DESCRIPTOR_SIZE)
#define PANEL_W		1024
#define PANEL_H		768

/* a JPEG frame is at most 500 KiB; the class reads in steps of 16 KiB */
#define FRAME_BUF_SIZE	(34 * 16384)
#define FRAME_COUNT	2
#define FRAME_LIMIT_KB	500
#define JPEG_VBV_SIZE	(1024 * 1024)
#define REPORT_MS	5000
#define FPS_TOLD	60
#define QUALITY_TOLD	9

static const struct device *const vdec_dev = DEVICE_DT_GET(DT_NODELABEL(ve));
static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static char product_string[64];
static uint8_t device_descriptor[] = {
	USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, USBD_VID, USBD_PID_TOUCH, 0x0101, 0x01)
};
static uint8_t config_descriptor[USB_CONFIG_SIZE_TOUCH] = {
	USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE_TOUCH, 0x02, 0x01, USB_CONFIG_BUS_POWERED,
				   USBD_MAX_POWER),
	USB_INTERFACE_DESCRIPTOR_INIT(0x00, 0x00, 0x02, 0xff, 0x00, 0x00, 0x00),
	USB_ENDPOINT_DESCRIPTOR_INIT(DISPLAY_IN_EP, 0x02, DISPLAY_EP_MPS, 0x00),
	USB_ENDPOINT_DESCRIPTOR_INIT(DISPLAY_OUT_EP, 0x02, DISPLAY_EP_MPS, 0x00),
	/* the touch screen follows, filled in by usb_start() */
};
static const uint8_t device_quality_descriptor[] = {
	0x0a, USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00,
};
static const char *string_descriptors[] = {
	(const char[]){0x09, 0x04}, "CherryUSB", product_string, "2022123456",
};

static const uint8_t *device_descriptor_cb(uint8_t speed)
{
	return device_descriptor;
}

static const uint8_t *config_descriptor_cb(uint8_t speed)
{
	return config_descriptor;
}

static const uint8_t *quality_descriptor_cb(uint8_t speed)
{
	return device_quality_descriptor;
}

static const char *string_descriptor_cb(uint8_t speed, uint8_t index)
{
	return index < ARRAY_SIZE(string_descriptors) ? string_descriptors[index] : NULL;
}

static const struct usb_descriptor usb_descriptors = {
	.device_descriptor_callback = device_descriptor_cb,
	.config_descriptor_callback = config_descriptor_cb,
	.device_quality_descriptor_callback = quality_descriptor_cb,
	.string_descriptor_callback = string_descriptor_cb,
};

/* ---- the receiving side ---------------------------------------------------------------- */

enum link_state { LINK_OFF, LINK_WAITING, LINK_CONFIGURED, LINK_STREAMING };

static volatile enum link_state link = LINK_OFF;
static volatile bool quit;
static volatile uint32_t frames_ok, frames_bad;

static struct usbd_interface display_intf;
static struct usbd_display_frame frame_pool[FRAME_COUNT];

static struct vdec_stream *jpeg_stream;
static uint32_t jpeg_w, jpeg_h;
static int jpeg_hold = 3;
static struct vdec_frame shown, older;

/* ---- the touch panel as the touch screen of the PC ------------------------------------------- */

/*
 * A finger the panel loses for a moment would end a drag selection or a press and hold on the PC,
 * so a lift is reported only when the finger stays away for UP_DEBOUNCE_MS. While a finger is
 * down the report is repeated every BEAT_MS, as the PC expects from a touch screen.
 */
#define UP_DEBOUNCE_MS	50
#define BEAT_MS		20

static struct {
	uint16_t x, y;
	bool down;		/* as the PC knows it */
	bool lifted;		/* one more report with the tip switch off */
	uint32_t up_at;		/* the panel lost the finger: when to tell the PC, 0 when it is down */
} slot[USB_TOUCH_MAX_CONTACTS];
static int cur_slot;
static struct k_spinlock touch_lock;
static bool touch_q_started;
static struct k_work_q touch_q;
K_THREAD_STACK_DEFINE(touch_q_stack, 2048);
static void touch_beat(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(touch_work, touch_beat);

/*
 * Runs in a queue of its own: the input events are delivered in the system work queue, which must
 * not wait for the USB endpoint.
 */
static void touch_beat(struct k_work *work)
{
	struct usb_touch_point out[USB_TOUCH_MAX_CONTACTS] = {0};
	uint32_t now = k_uptime_get_32();
	bool any = false;
	int count = 0;
	k_spinlock_key_t key;

	ARG_UNUSED(work);
	key = k_spin_lock(&touch_lock);
	for (int i = 0; i < USB_TOUCH_MAX_CONTACTS; i++) {
		if (slot[i].down && slot[i].up_at != 0 && (int32_t)(now - slot[i].up_at) >= 0) {
			slot[i].down = false;
			slot[i].lifted = true;
			slot[i].up_at = 0;
		}
		out[i].id = i;
		if (slot[i].down || slot[i].lifted) {
			out[i].down = slot[i].down;
			/* the PC's screen is the picture turned by 180 degrees against the panel's axes */
			out[i].x = USB_TOUCH_MAX - MIN(slot[i].x, PANEL_W - 1) * USB_TOUCH_MAX / (PANEL_W - 1);
			out[i].y = USB_TOUCH_MAX - MIN(slot[i].y, PANEL_H - 1) * USB_TOUCH_MAX / (PANEL_H - 1);
			count = i + 1;
		}
		slot[i].lifted = false;
		any |= slot[i].down;
	}
	k_spin_unlock(&touch_lock, key);

	if (count != 0 && (link == LINK_CONFIGURED || link == LINK_STREAMING)) {
		for (int n = 0; usb_touch_send(out, count) == -EBUSY && n < 20; n++) {
			k_msleep(1);
		}
	}
	if (any) {
		k_work_schedule_for_queue(&touch_q, &touch_work, K_MSEC(BEAT_MS));
	}
}

static void touch_event(struct input_event *evt, void *user_data)
{
	k_spinlock_key_t key;

	ARG_UNUSED(user_data);
	if (link == LINK_OFF) {
		return;
	}
	key = k_spin_lock(&touch_lock);
	switch (evt->code) {
	case INPUT_ABS_MT_SLOT:
		cur_slot = CLAMP(evt->value, 0, USB_TOUCH_MAX_CONTACTS - 1);
		break;
	case INPUT_ABS_X:
		slot[cur_slot].x = evt->value;
		break;
	case INPUT_ABS_Y:
		slot[cur_slot].y = evt->value;
		break;
	case INPUT_BTN_TOUCH:
		if (evt->value) {
			slot[cur_slot].down = true;
			slot[cur_slot].up_at = 0;
		} else if (slot[cur_slot].down && slot[cur_slot].up_at == 0) {
			slot[cur_slot].up_at = k_uptime_get_32() + UP_DEBOUNCE_MS;
		}
		break;
	default:
		break;
	}
	k_spin_unlock(&touch_lock, key);
	if (evt->sync) {
		k_work_reschedule_for_queue(&touch_q, &touch_work, K_NO_WAIT);
	}
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_CHOSEN(zephyr_touch)), touch_event, NULL);

K_THREAD_STACK_DEFINE(usb_stack, 8192);
static struct k_thread usb_thread;
static K_SEM_DEFINE(usb_done, 0, 1);

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
	printk("usb display: event %d\n", event);
	if (event == USBD_EVENT_CONFIGURED) {
		link = LINK_CONFIGURED;
	} else if (event == USBD_EVENT_DISCONNECTED || event == USBD_EVENT_RESET) {
		if (link != LINK_OFF) {
			link = LINK_WAITING;
		}
	}
}

static void stream_close(void)
{
	if (older.priv) {
		vdec_frame_release(vdec_dev, &older);
		older.priv = NULL;
	}
	if (shown.priv) {
		vdec_frame_release(vdec_dev, &shown);
		shown.priv = NULL;
	}
	if (jpeg_stream) {
		vdec_stream_close(vdec_dev, jpeg_stream);
	}
	jpeg_stream = NULL;
}

static int stream_open(uint32_t w, uint32_t h)
{
	int ret = -ENOMEM;

	for (jpeg_hold = 3; jpeg_hold >= 1; jpeg_hold--) {
		struct vdec_stream_config cfg = {
			.codec = VDEC_CODEC_JPEG,
			.format = VDEC_FORMAT_NV12,
			.buffer_size = JPEG_VBV_SIZE,
			.no_cache_ops = true,
			.holding_frames = jpeg_hold,
			.width = w,
			.height = h,
		};

		ret = vdec_stream_open(vdec_dev, &cfg, &jpeg_stream);
		if (ret == 0) {
			jpeg_w = w;
			jpeg_h = h;
			printk("usb display: %ux%u decoder, %d pictures kept\n", w, h, jpeg_hold);
			return 0;
		}
	}

	return ret;
}

static int show_jpeg(const void *jpeg, size_t len, uint32_t w, uint32_t h)
{
	struct vdec_frame f;
	struct display_sunxi_yuv yuv = {0};
	size_t used;
	int ret;

	if (jpeg_stream && (w != jpeg_w || h != jpeg_h)) {
		stream_close();
	}
	if (!jpeg_stream) {
		ret = stream_open(w, h);
		if (ret) {
			return ret;
		}
	}
	ret = vdec_stream_feed(vdec_dev, jpeg_stream, jpeg, len, 0, &used);
	if (ret) {
		return ret;
	}
	for (int tries = 0; tries < 20; tries++) {
		ret = vdec_stream_get_frame(vdec_dev, jpeg_stream, &f);
		if (ret != -EBUSY && ret != -EAGAIN) {
			break;
		}
		k_msleep(1);
	}
	if (ret) {
		return ret;
	}
	yuv.y = f.plane[0];
	yuv.uv = f.plane[1];
	yuv.width = f.width;
	yuv.height = f.height;
	yuv.stride_y = f.stride[0];
	yuv.stride_uv = f.stride[1];
	yuv.full_range = true;
	yuv.nonblock = jpeg_hold >= 2;
	ret = display_sunxi_show_yuv(disp, &yuv);
	if (jpeg_hold >= 2) {
		if (older.priv) {
			vdec_frame_release(vdec_dev, &older);
		}
		older = shown;
	} else if (shown.priv) {
		vdec_frame_release(vdec_dev, &shown);
	}
	shown = f;

	return ret;
}

static bool jpeg_complete(const uint8_t *d, uint32_t n)
{
	uint32_t e = n;

	while (e > 2 && d[e - 1] == 0) {
		e--;
	}

	return n >= 4 && d[0] == 0xff && d[1] == 0xd8 && d[e - 2] == 0xff && d[e - 1] == 0xd9;
}

static void handle_frame(struct usbd_display_frame *frame)
{
	struct usbd_disp_frame_header *h = (struct usbd_disp_frame_header *)frame->frame_buf;
	const uint8_t *d = frame->frame_buf + sizeof(*h);
	uint32_t n = frame->frame_size;

	if (frame->frame_format != USBD_DISPLAY_TYPE_JPG || n == 0) {
		return;
	}
	if (!jpeg_complete(d, n) || show_jpeg(d, n, h->width, h->height) != 0) {
		frames_bad++;
		return;
	}
	frames_ok++;
	link = LINK_STREAMING;
}

static void usb_main(void *a, void *b, void *c)
{
	uint32_t last = k_uptime_get_32(), start_ok = 0;

	while (!quit) {
		struct usbd_display_frame *frame;
		uint32_t ms;

		if (usbd_display_dequeue(&frame, 100) >= 0) {
			handle_frame(frame);
			usbd_display_enqueue(frame);
		} else {
			k_msleep(10);
		}
		ms = k_uptime_get_32() - last;
		if (ms >= REPORT_MS) {
			uint32_t n = frames_ok - start_ok;

			printk("usb display: %u frames (%u fps), %u bad\n", n, n * 1000 / ms,
			       frames_bad);
			start_ok = frames_ok;
			last = k_uptime_get_32();
		}
	}
	k_sem_give(&usb_done);
}


/* free space of the heap: the chunks of 64 KiB that can be taken, and the largest single block */
static void heap_report(const char *tag)
{
	void **list = malloc(256 * sizeof(void *));
	unsigned int n = 0, big = 0;

	if (list == NULL) {
		return;
	}
	while (n < 256 && (list[n] = malloc(64 * 1024)) != NULL) {
		n++;
	}
	for (unsigned int i = 0; i < n; i++) {
		free(list[i]);
	}
	for (unsigned int kb = 16384; kb >= 64 && big == 0; kb /= 2) {
		void *p = malloc(kb * 1024);

		if (p != NULL) {
			free(p);
			big = kb;
		}
	}
	free(list);
	printk("usb display: heap %s: %u KiB free in 64 KiB blocks, largest block >= %u KiB\n", tag,
	       n * 64, big);
}

static int usb_start(void)
{
	struct display_capabilities caps;

	if (!device_is_ready(vdec_dev) || !device_is_ready(disp)) {
		return -ENODEV;
	}
	heap_report("before");
	display_get_capabilities(disp, &caps);
	snprintk(product_string, sizeof(product_string), "cherryusb_R%ux%u_Ejpg%u_Fps%u_Bl%u",
		 caps.x_resolution, caps.y_resolution, QUALITY_TOLD, FPS_TOLD, FRAME_LIMIT_KB);
	for (int i = 0; i < FRAME_COUNT; i++) {
		frame_pool[i].frame_buf = aligned_alloc(64, FRAME_BUF_SIZE);
		frame_pool[i].frame_bufsize = FRAME_BUF_SIZE;
		if (!frame_pool[i].frame_buf) {
			for (int j = 0; j < i; j++) {
				free(frame_pool[j].frame_buf);
			}
			return -ENOMEM;
		}
	}
	if (!touch_q_started) {
		k_work_queue_init(&touch_q);
		k_work_queue_start(&touch_q, touch_q_stack, K_THREAD_STACK_SIZEOF(touch_q_stack), 6,
				   NULL);
		touch_q_started = true;
	}
	{
		k_spinlock_key_t key = k_spin_lock(&touch_lock);

		memset(slot, 0, sizeof(slot));
		k_spin_unlock(&touch_lock, key);
	}
	frames_ok = frames_bad = 0;
	quit = false;
	link = LINK_WAITING;
	usb_touch_descriptor(config_descriptor + USB_CONFIG_SIZE, USB_TOUCH_INTERFACE);
	usbd_desc_register(0, &usb_descriptors);
	usbd_add_interface(0, usbd_display_init_intf(&display_intf, DISPLAY_OUT_EP, DISPLAY_IN_EP,
						    frame_pool, FRAME_COUNT));
	usb_touch_init(0);
	printk("usb display: starting the device\n");
	usbd_initialize(0, usb_sunxi_otg_base(), usbd_event_handler);
	printk("usb display: started, power %02x\n", sys_read8(usb_sunxi_otg_base() + 1));
	k_thread_create(&usb_thread, usb_stack, K_THREAD_STACK_SIZEOF(usb_stack), usb_main, NULL,
			NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_name_set(&usb_thread, "usb_display");
	printk("usb display: waiting for the host (%s)\n", product_string);

	return 0;
}

static void usb_stop(void)
{
	if (link == LINK_OFF) {
		return;
	}
	usbd_deinitialize(0);
	quit = true;
	k_sem_take(&usb_done, K_FOREVER);
	k_thread_join(&usb_thread, K_FOREVER);
	display_sunxi_hide_yuv(disp);
	stream_close();
	for (int i = 0; i < FRAME_COUNT; i++) {
		free(frame_pool[i].frame_buf);
		frame_pool[i].frame_buf = NULL;
	}
	link = LINK_OFF;
	heap_report("after");
	printk("usb display: stopped\n");
}

/* ---- the screen ------------------------------------------------------------------------ */

static lv_obj_t *card, *title_label, *text_label, *cross;
static lv_timer_t *status_timer;
static enum link_state shown_link = LINK_OFF;
static int start_error;

static void leave_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_goto("home");
}

static void status_update(lv_timer_t *t)
{
	enum link_state l = link;

	ARG_UNUSED(t);
	if (l == shown_link) {
		return;
	}
	shown_link = l;
	if (l == LINK_STREAMING) {
		lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	lv_obj_remove_flag(card, LV_OBJ_FLAG_HIDDEN);
	lv_label_set_text(text_label, l == LINK_CONFIGURED ? "Connected, waiting for the first picture" :
				      "Connect this board to a PC with the USB cable.\n"
				      "The PC sees a second screen.");
}

static lv_obj_t *usb_create(void)
{
	lv_obj_t *scr = ui_screen_new(true);

	card = lv_obj_create(scr);
	lv_obj_remove_style_all(card);
	lv_obj_set_size(card, 640, 260);
	lv_obj_center(card);
	lv_obj_set_style_radius(card, 32, 0);
	lv_obj_set_style_bg_color(card, lv_color_hex(C_SURFACE), 0);
	lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
	lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);

	title_label = lv_label_create(card);
	lv_obj_set_style_text_font(title_label, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(title_label, lv_color_hex(C_TEXT), 0);
	lv_label_set_text(title_label, LV_SYMBOL_USB "  USB Display");
	lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 36);

	text_label = lv_label_create(card);
	lv_obj_set_style_text_font(text_label, &lv_font_montserrat_20, 0);
	lv_obj_set_style_text_color(text_label, lv_color_hex(C_DIM), 0);
	lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(text_label, 560);
	lv_obj_align(text_label, LV_ALIGN_TOP_MID, 0, 110);

	cross = ui_icon_button(scr, LV_SYMBOL_CLOSE, 88);
	lv_obj_set_style_bg_opa(cross, LV_OPA_70, 0);
	lv_obj_set_ext_click_area(cross, 32);
	lv_obj_align(cross, LV_ALIGN_TOP_RIGHT, -24, 24);
	lv_obj_add_event_cb(cross, leave_clicked, LV_EVENT_CLICKED, NULL);

	return scr;
}

static void usb_enter(void *arg)
{
	ARG_UNUSED(arg);
	start_error = usb_start();
	shown_link = LINK_STREAMING;	/* forces the first update */
	if (start_error) {
		lv_obj_remove_flag(card, LV_OBJ_FLAG_HIDDEN);
		lv_label_set_text_fmt(text_label, "Cannot start the USB device (%d)", start_error);
		return;
	}
	if (status_timer == NULL) {
		status_timer = lv_timer_create(status_update, 200, NULL);
	}
	lv_timer_resume(status_timer);
	status_update(NULL);
}

static void usb_leave(void)
{
	if (status_timer != NULL) {
		lv_timer_pause(status_timer);
	}
	usb_stop();
	ui_overlay_set(true);
}

NEKOBOX_SCREEN(usb, "usb", usb_create, usb_enter, usb_leave);

NEKOBOX_HOME_TILE(usb, .order = 40, .symbol = LV_SYMBOL_USB, .title = "USB Display",
	      .subtitle = "Second screen for a PC", .color_top = 0xFF6C9B, .color_bottom = 0xD443A8,
	      .screen = "usb");
