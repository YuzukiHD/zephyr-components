/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Wi-Fi and the network time */

#ifndef NEKOBOX_FEATURES_NET_NET_H_
#define NEKOBOX_FEATURES_NET_NET_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct net_ap {
	char ssid[33];
	int8_t rssi;
	bool secured;
};

/** One line about the Wi-Fi link and about the last time from the network, for the settings */
void app_net_status(char *buf, size_t size);
void app_net_sync_status(char *buf, size_t size);
/** Asks the network for the time and sets the clock with the answer */
void app_net_sync_time(void);

/** Starts a scan; the results come in while it runs */
void app_net_scan(void);
bool app_net_scanning(void);
/** Copies the access points found so far, strongest first; returns how many */
int app_net_scan_results(struct net_ap *out, int max);
/** The password saved for a network that was joined before; false when there is none */
bool app_net_saved_psk(const char *ssid, char *psk, size_t size);
/** Joins a network (psk may be empty for an open one) and saves it */
void app_net_connect(const char *ssid, const char *psk);
void app_net_disconnect(void);
bool app_net_connected(void);
/** True when the board has a Wi-Fi module that answered */
bool app_net_available(void);

#endif /* NEKOBOX_FEATURES_NET_NET_H_ */
