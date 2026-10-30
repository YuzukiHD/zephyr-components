/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>

#include "dlna_priv.h"
#include "upnp.h"

static bool running;

struct ip_search {
	struct in_addr addr;
	bool found;
};

static void ip_cb(struct net_if *iface, void *data)
{
	struct ip_search *s = data;
	struct in_addr *a;

	if (s->found || !net_if_is_up(iface)) {
		return;
	}
	a = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
	if (a && a->s4_addr[0] != 127 && a->s4_addr[0] != 0) {
		s->addr = *a;
		s->found = true;
	}
}

bool dlna_local_ip(char ip[16])
{
	struct ip_search s = {.found = false};

	net_if_foreach(ip_cb, &s);
	if (!s.found) {
		return false;
	}
	return net_addr_ntop(AF_INET, &s.addr, ip, 16) != NULL;
}

void dlna_uuid_from_id(const uint8_t *id, size_t len, char out[37])
{
	uint8_t b[16];

	/* fold the id over 16 bytes, then mark version 5 and the RFC 4122 variant */
	memset(b, 0, sizeof(b));
	for (size_t i = 0; i < len; i++) {
		b[i % 16] ^= id[i] + (uint8_t)(i * 31);
	}
	b[6] = (b[6] & 0x0f) | 0x50;
	b[8] = (b[8] & 0x3f) | 0x80;
	snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		 b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12],
		 b[13], b[14], b[15]);
}

int dlna_start(const struct dlna_config *cfg)
{
	int ret;

	if (running) {
		return -EALREADY;
	}
	if (!cfg || !cfg->name || !cfg->uuid || !cfg->ops) {
		return -EINVAL;
	}
	memset(&upnp, 0, sizeof(upnp));
	upnp.cfg = cfg;

	ret = http_srv_start();
	if (ret < 0) {
		return ret;
	}
	ret = ssdp_start();
	if (ret < 0) {
		http_srv_stop();
		return ret;
	}
	running = true;
	return 0;
}

void dlna_stop(void)
{
	if (!running) {
		return;
	}
	ssdp_stop();	/* sends the byebye notices */
	http_srv_stop();
	running = false;
}

bool dlna_running(void)
{
	return running;
}
