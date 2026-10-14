/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BILI_WIFI_H_
#define BILI_WIFI_H_

#include <stdbool.h>
#include <stdint.h>

/** Joins a network and waits for the address; @p psk may be empty. 0 or a negative errno */
int wifi_join(const char *ssid, const char *psk, int timeout_s);

struct wifi_ap {
	char ssid[33];
	int8_t rssi;
	bool secured;
};

/**
 * Looks for networks (blocks for up to 10 s). The list has every name once, the strongest first.
 * Returns how many were stored in @p out, or a negative errno.
 */
int wifi_scan(struct wifi_ap *out, int max);

/** The name of the network joined last; empty before the first successful join */
const char *wifi_current(void);

#endif /* BILI_WIFI_H_ */
