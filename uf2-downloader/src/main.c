/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * UF2 boot mode: the board shows up as a USB drive, a .uf2 file copied onto it is written
 * to the SPI NOR flash, and the board restarts into the application when the file is done.
 * The boot loader starts this program when the key is held or no application is in the flash.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#include "usbd_core.h"
#include "usbd_msc.h"

#include "uf2.h"

extern uintptr_t usb_sunxi_otg_base(void);

#define MSC_IN_EP  0x81
#define MSC_OUT_EP 0x02

#define USBD_VID	   0x1209
#define USBD_PID	   0xF101
#define USBD_MAX_POWER	   100
#define USB_CONFIG_SIZE	   (9 + MSC_DESCRIPTOR_LEN)

static const uint8_t device_descriptor[] = {
	USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, USBD_VID, USBD_PID, 0x0200, 0x01)
};

static const uint8_t config_hs[] = {
	USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED,
				   USBD_MAX_POWER),
	MSC_DESCRIPTOR_INIT(0x00, MSC_OUT_EP, MSC_IN_EP, USB_BULK_EP_MPS_HS, 0x02)
};

static const uint8_t config_fs[] = {
	USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED,
				   USBD_MAX_POWER),
	MSC_DESCRIPTOR_INIT(0x00, MSC_OUT_EP, MSC_IN_EP, USB_BULK_EP_MPS_FS, 0x02)
};

static const uint8_t qualifier[] = {USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, 0x01)};

static const uint8_t other_hs[] = {
	USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED,
					       USBD_MAX_POWER),
	MSC_DESCRIPTOR_INIT(0x00, MSC_OUT_EP, MSC_IN_EP, USB_BULK_EP_MPS_FS, 0x02)
};

static const uint8_t other_fs[] = {
	USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED,
					       USBD_MAX_POWER),
	MSC_DESCRIPTOR_INIT(0x00, MSC_OUT_EP, MSC_IN_EP, USB_BULK_EP_MPS_HS, 0x02)
};

static const char *strings[] = {
	(const char[]){0x09, 0x04},
	"Yuzuki",
	"F101 UF2 Bootloader",
	"F101BOOT0001",
};

static const uint8_t *device_cb(uint8_t speed)
{
	return device_descriptor;
}

static const uint8_t *config_cb(uint8_t speed)
{
	return speed == USB_SPEED_HIGH ? config_hs : speed == USB_SPEED_FULL ? config_fs : NULL;
}

static const uint8_t *qualifier_cb(uint8_t speed)
{
	return qualifier;
}

static const uint8_t *other_cb(uint8_t speed)
{
	return speed == USB_SPEED_HIGH ? other_hs : speed == USB_SPEED_FULL ? other_fs : NULL;
}

static const char *string_cb(uint8_t speed, uint8_t index)
{
	return index < ARRAY_SIZE(strings) ? strings[index] : NULL;
}

static const struct usb_descriptor descriptor = {
	.device_descriptor_callback = device_cb,
	.config_descriptor_callback = config_cb,
	.device_quality_descriptor_callback = qualifier_cb,
	.other_speed_descriptor_callback = other_cb,
	.string_descriptor_callback = string_cb,
};

static struct usbd_interface intf0;

static void event_handler(uint8_t busid, uint8_t event)
{
	if (event == USBD_EVENT_CONFIGURED) {
		printk("uf2-downloader: USB configured\n");
	}
}

int main(void)
{
	int ret = uf2_init();

	if (ret != 0) {
		printk("[E] uf2-downloader: flash not usable: %d\n", ret);
		return 0;
	}
	usbd_desc_register(0, &descriptor);
	usbd_add_interface(0, usbd_msc_init_intf(0, &intf0, MSC_OUT_EP, MSC_IN_EP));
	usbd_initialize(0, usb_sunxi_otg_base(), event_handler);
	printk("uf2-downloader: ready, copy a .uf2 file onto the F101BOOT drive\n");

	uint32_t last = 0;
	int quiet = 0, key = -1;
	const struct device *gpio = DEVICE_DT_GET(DT_NODELABEL(gpiod));

	gpio_pin_configure(gpio, 5, GPIO_INPUT | GPIO_PULL_UP);

	while (true) {
		k_msleep(20);
		int level = gpio_pin_get_raw(gpio, 5);

		if (level != key) {
			key = level;
			printk("uf2-downloader: key (PD5) = %d\n", key);
		}
		if (uf2_complete()) {
			/* the host still writes its file system tables */
			k_msleep(1000);
			printk("uf2-downloader: done, restarting\n");
			sys_reboot(SYS_REBOOT_COLD);
		}
		if (uf2_blocks_done() != last) {
			last = uf2_blocks_done();
			quiet = 0;
		} else if (++quiet == 250 && last != 0U) {
			printk("uf2-downloader: %u of %u blocks, %u errors\n", last, uf2_blocks_total(),
			       uf2_errors());
		}
	}
	return 0;
}
