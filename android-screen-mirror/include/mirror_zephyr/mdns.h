/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/* Minimal DNS-SD browser: finds one instance of a service type on the local network */

#ifndef MIRROR_ZEPHYR_MDNS_H_
#define MIRROR_ZEPHYR_MDNS_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/net/net_ip.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mdns_service {
	char instance[64];
	uint16_t port;
	struct in_addr addr;
};

/**
 * @brief Look for a service instance
 *
 * Asks for @p type (like "_adb-tls-pairing._tcp.local") until an instance with a port and an
 * address shows up or the time is over.
 *
 * @param instance wanted instance name, NULL for any
 * @retval 0 found
 * @retval -ETIMEDOUT nothing found
 */
int mdns_find(const char *type, const char *instance, struct mdns_service *out, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
