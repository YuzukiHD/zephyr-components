/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BILI_WIFI_H_
#define BILI_WIFI_H_

/** Joins a network and waits for the address; @p psk may be empty. 0 or a negative errno */
int wifi_join(const char *ssid, const char *psk, int timeout_s);

#endif /* BILI_WIFI_H_ */
