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
#include <zephyr/sys/printk.h>

#include "usbd_core.h"
#include "usbd_display.h"
#include "mp5.h"

extern uintptr_t usb_sunxi_otg_base(void);

#define DISPLAY_IN_EP	0x81
#define DISPLAY_OUT_EP	0x02
#define DISPLAY_EP_MPS	512
#define USBD_VID	0x303A
#define USBD_PID	0x2987
#define USBD_MAX_POWER	100
#define USB_CONFIG_SIZE	(9 + 9 + 7 + 7)

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
	USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, USBD_VID, USBD_PID, 0x0101, 0x01)
};
static const uint8_t config_descriptor[USB_CONFIG_SIZE] = {
	USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED,
				   USBD_MAX_POWER),
	USB_INTERFACE_DESCRIPTOR_INIT(0x00, 0x00, 0x02, 0xff, 0x00, 0x00, 0x00),
	USB_ENDPOINT_DESCRIPTOR_INIT(DISPLAY_IN_EP, 0x02, DISPLAY_EP_MPS, 0x00),
	USB_ENDPOINT_DESCRIPTOR_INIT(DISPLAY_OUT_EP, 0x02, DISPLAY_EP_MPS, 0x00),
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

K_THREAD_STACK_DEFINE(usb_stack, 8192);
static struct k_thread usb_thread;
static K_SEM_DEFINE(usb_done, 0, 1);

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
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

static int usb_start(void)
{
	struct display_capabilities caps;

	if (!device_is_ready(vdec_dev) || !device_is_ready(disp)) {
		return -ENODEV;
	}
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
	frames_ok = frames_bad = 0;
	quit = false;
	link = LINK_WAITING;
	k_thread_create(&usb_thread, usb_stack, K_THREAD_STACK_SIZEOF(usb_stack), usb_main, NULL,
			NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_name_set(&usb_thread, "usb_display");

	usbd_desc_register(0, &usb_descriptors);
	usbd_add_interface(0, usbd_display_init_intf(&display_intf, DISPLAY_OUT_EP, DISPLAY_IN_EP,
						    frame_pool, FRAME_COUNT));
	usbd_initialize(0, usb_sunxi_otg_base(), usbd_event_handler);
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
	ui_goto(SCR_HOME);
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

	cross = ui_icon_button(scr, LV_SYMBOL_CLOSE, 64);
	lv_obj_set_style_bg_opa(cross, LV_OPA_70, 0);
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
	display_sunxi_fb_enable(disp, true);
}

const struct screen_ops usb_ops = {
	.create = usb_create,
	.enter = usb_enter,
	.leave = usb_leave,
};
