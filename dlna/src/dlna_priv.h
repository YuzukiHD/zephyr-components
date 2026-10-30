/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DLNA_PRIV_H_
#define DLNA_PRIV_H_

#include <stdbool.h>
#include <stdint.h>

#include <dlna/dlna.h>

/** Own IPv4 address as text (not loopback); false while the network has none */
bool dlna_local_ip(char ip[16]);

int ssdp_start(void);
void ssdp_stop(void);
int http_srv_start(void);
void http_srv_stop(void);

/** Server header text of all answers */
#define DLNA_SERVER "Zephyr UPnP/1.0 F101/1.0"

#endif /* DLNA_PRIV_H_ */
