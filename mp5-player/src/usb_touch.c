/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The HID touch screen of the USB display: report descriptor, reports and a synthetic demo. */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "usbd_core.h"
#include "usbd_hid.h"
#include "usb_touch.h"

#define REPORT_ID               1
#define FEATURE_REPORT          3
#define CONTACT_BYTES           6       /* tip switch, id, x (2), y (2) */
#define REPORT_SIZE             (1 + USB_TOUCH_MAX_CONTACTS * CONTACT_BYTES + 1)

/* the panel size in 0.01 cm units: 154 x 86 mm */
#define PHYS_X                  1540
#define PHYS_Y                  860

/* one finger: tip switch, padding, contact id, X, Y */
#define FINGER_ITEMS                                                                                           \
	0x09, 0x22,                 /* Usage (Finger) */                                                           \
	0xA1, 0x02,                 /* Collection (Logical) */                                                     \
	0x09, 0x42,                 /*   Usage (Tip Switch) */                                                     \
	0x15, 0x00, 0x25, 0x01,     /*   Logical 0..1 */                                                           \
	0x75, 0x01, 0x95, 0x01,     /*   1 bit */                                                                  \
	0x81, 0x02,                 /*   Input (Data,Var,Abs) */                                                   \
	0x75, 0x07, 0x95, 0x01,     /*   7 bits of padding */                                                      \
	0x81, 0x03,                 /*   Input (Const,Var,Abs) */                                                  \
	0x09, 0x51,                 /*   Usage (Contact Identifier) */                                             \
	0x75, 0x08, 0x95, 0x01,                                                                                    \
	0x15, 0x00, 0x25, 0x7F,                                                                                    \
	0x81, 0x02,                                                                                                \
	0x05, 0x01,                 /*   Usage Page (Generic Desktop) */                                           \
	0x09, 0x30,                 /*   Usage (X) */                                                              \
	0x15, 0x00, 0x26, 0xFF, 0x7F,                                                                              \
	0x35, 0x00, 0x46, PHYS_X & 0xFF, PHYS_X >> 8,                                                              \
	0x75, 0x10, 0x95, 0x01,                                                                                    \
	0x81, 0x02,                                                                                                \
	0x09, 0x31,                 /*   Usage (Y) */                                                              \
	0x46, PHYS_Y & 0xFF, PHYS_Y >> 8,                                                                          \
	0x81, 0x02,                                                                                                \
	0x05, 0x0D,                 /*   Usage Page (Digitizer) */                                                 \
	0xC0                        /* End Collection */

static const uint8_t report_descriptor[] = {
	0x05, 0x0D,                 /* Usage Page (Digitizer) */
	0x09, 0x04,                 /* Usage (Touch Screen) */
	0xA1, 0x01,                 /* Collection (Application) */
	0x85, REPORT_ID,            /*   Report ID */
	0x65, 0x11,                 /*   Unit (centimetre) */
	0x55, 0x0E,                 /*   Unit Exponent (-2) */
	FINGER_ITEMS, FINGER_ITEMS, FINGER_ITEMS, FINGER_ITEMS, FINGER_ITEMS,
	0x05, 0x0D,
	0x09, 0x54,                 /*   Usage (Contact Count) */
	0x75, 0x08, 0x95, 0x01,
	0x15, 0x00, 0x25, 0x7F,
	0x81, 0x02,
	0x09, 0x55,                 /*   Usage (Contact Count Maximum) */
	0x25, USB_TOUCH_MAX_CONTACTS,
	0xB1, 0x02,                 /*   Feature (Data,Var,Abs) */
	0xC0                        /* End Collection */
};

/* HID interface, HID class descriptor, interrupt IN endpoint */
uint32_t usb_touch_descriptor(uint8_t *buf, uint8_t interface)
{
	const uint8_t d[USB_TOUCH_DESCRIPTOR_SIZE] = {
		0x09, USB_DESCRIPTOR_TYPE_INTERFACE, interface, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
		0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, sizeof(report_descriptor) & 0xFF, sizeof(report_descriptor) >> 8,
		0x07, USB_DESCRIPTOR_TYPE_ENDPOINT, USB_TOUCH_EP, 0x03, USB_TOUCH_EP_MPS & 0xFF, USB_TOUCH_EP_MPS >> 8,
#ifdef CONFIG_USB_HS
		0x04,                   /* 1 ms */
#else
		0x01,
#endif
	};

	memcpy(buf, d, sizeof(d));

	return sizeof(d);
}

static struct usbd_interface hid_intf;
static uint8_t report[REPORT_SIZE] __attribute__((aligned(64)));
static volatile bool tx_busy;

static void touch_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
	tx_busy = false;
}

static struct usbd_endpoint touch_ep = {
	.ep_addr = USB_TOUCH_EP,
	.ep_cb = touch_in
};

/* Windows asks how many fingers the screen reports (the feature report of the descriptor) */
void usbd_hid_get_report(uint8_t busid, uint8_t intf, uint8_t report_id, uint8_t report_type, uint8_t **data, uint32_t *len)
{
	static uint8_t feature[2];

	if (report_type == FEATURE_REPORT)
	{
		feature[0] = REPORT_ID;
		feature[1] = USB_TOUCH_MAX_CONTACTS;
		*data = feature;
		*len = sizeof(feature);
	}
	else
	{
		*data = report;
		*len = REPORT_SIZE;
	}
}

void usb_touch_init(uint8_t busid)
{
	/* a report that was on its way when the device was stopped never completes */
	tx_busy = false;
	usbd_add_interface(busid, usbd_hid_init_intf(busid, &hid_intf, report_descriptor, sizeof(report_descriptor)));
	usbd_add_endpoint(busid, &touch_ep);
}

int usb_touch_send(const struct usb_touch_point *points, int count)
{
	int i, active = 0;

	if (count > USB_TOUCH_MAX_CONTACTS)
		count = USB_TOUCH_MAX_CONTACTS;
	if (tx_busy)
		return -EBUSY;
	memset(report, 0, sizeof(report));
	report[0] = REPORT_ID;
	for (i = 0; i < count; i++)
	{
		uint8_t *c = &report[1 + i * CONTACT_BYTES];

		c[0] = points[i].down ? 1 : 0;
		c[1] = points[i].id;
		c[2] = points[i].x & 0xFF;
		c[3] = points[i].x >> 8;
		c[4] = points[i].y & 0xFF;
		c[5] = points[i].y >> 8;
		active++;
	}
	report[REPORT_SIZE - 1] = active;
	tx_busy = true;
	if (usbd_ep_start_write(0, USB_TOUCH_EP, report, REPORT_SIZE) != 0)
	{
		tx_busy = false;
		return -EIO;
	}

	return 0;
}

/* wait for the last report to leave */
static void wait_sent(void)
{
	int n = 100;

	while (tx_busy && n--) {
		k_msleep(1);
	}
}

void usb_touch_demo(int fingers)
{
	struct usb_touch_point p[2];
	int n = fingers > 1 ? 2 : 1, steps = 100;

	memset(p, 0, sizeof(p));
	for (int step = 0; step <= steps; step++) {
		for (int i = 0; i < n; i++) {
			p[i].id = i;
			p[i].down = true;
			p[i].x = (USB_TOUCH_MAX / 10) + (USB_TOUCH_MAX * 8 / 10) * step / steps;
			p[i].y = p[i].x;
			if (i == 1) {
				p[i].x = USB_TOUCH_MAX - p[i].x;
			}
		}
		while (usb_touch_send(p, n) == -EBUSY) {
			k_msleep(1);
		}
		k_msleep(20);
	}
	for (int i = 0; i < 2; i++) {
		p[i].down = false;
	}
	while (usb_touch_send(p, n) == -EBUSY) {
		k_msleep(1);
	}
	wait_sent();
	printk("usb touch: demo done (%d finger%s)\n", n, n > 1 ? "s" : "");
}
