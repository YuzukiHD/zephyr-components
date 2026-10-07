/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * HCI transport setup of the Bluetooth part of the chip.
 *
 * The controller firmware is part of what the WiFi start-up sends over SDIO, so the chip has to
 * be started before the HCI UART is of any use. BT_RSTN is held in reset until then and released
 * here, when the H:4 driver opens the transport.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "aic_zif.h"

LOG_MODULE_DECLARE(aic8800, CONFIG_AIC8800_LOG_LEVEL);

#define AIC_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(aicsemi_aic8800)

#define BT_RESET_HOLD_MS	10
#define BT_BOOT_MS		500

static const struct gpio_dt_spec bt_reset = GPIO_DT_SPEC_GET_OR(AIC_NODE, bt_reset_gpios, {0});

/* the line is held in reset from the start, until the firmware is on the chip */
static int aic_bt_reset_hold(void)
{
	if (bt_reset.port == NULL || !gpio_is_ready_dt(&bt_reset)) {
		return 0;
	}
	return gpio_pin_configure_dt(&bt_reset, GPIO_OUTPUT_ACTIVE);
}
SYS_INIT(aic_bt_reset_hold, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);

int bt_hci_transport_setup(const struct device *uart)
{
	unsigned char c;
	int ret;

	/* sends the firmware of both parts, returns at once when the chip is already running */
	ret = aicz_wifi_start();
	if (ret != 0) {
		LOG_ERR("the chip did not start: %d", ret);
		return ret;
	}

	if (bt_reset.port != NULL) {
		gpio_pin_set_dt(&bt_reset, 1);
		k_msleep(BT_RESET_HOLD_MS);
		gpio_pin_set_dt(&bt_reset, 0);
	}
	k_msleep(BT_BOOT_MS);

	/* whatever the controller sent while it was starting is not an HCI packet */
	while (uart_poll_in(uart, &c) == 0) {
	}

	return 0;
}
