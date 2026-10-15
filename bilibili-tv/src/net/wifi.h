/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BILI_WIFI_H_
#define BILI_WIFI_H_

#include <stdbool.h>
#include <stdint.h>

/** Where the connection to a network stands */
enum wifi_state {
	WIFI_DISCONNECTED,	/**< not connected (never was, left on purpose, or the link was lost) */
	WIFI_CONNECTING,	/**< a join is going on */
	WIFI_CONNECTED,		/**< joined and an address is there */
	WIFI_FAILED,		/**< the last join did not work (see wifi_last_error()) */
};

/**
 * Joins a network and waits for the address; @p psk may be empty. A network that is joined already
 * (the same or another one) is left first. 0 or a negative errno; a failed join leaves the interface
 * disconnected.
 */
int wifi_join(const char *ssid, const char *psk, int timeout_s);

/** Leaves the network (and stops a join that is going on); the saved network is not touched */
int wifi_disconnect(void);

enum wifi_state wifi_state(void);

/** Why the last join failed: -ETIMEDOUT no answer, -ECONNREFUSED refused (password), -ENETUNREACH no address */
int wifi_last_error(void);

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

/** The name of the network that is joined or being joined; empty when there is none */
const char *wifi_current(void);

#endif /* BILI_WIFI_H_ */
