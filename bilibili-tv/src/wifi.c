/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/printk.h>

#include "wifi.h"

static struct net_mgmt_event_callback wifi_cb, ipv4_cb;
static K_SEM_DEFINE(connected, 0, 1);
static K_SEM_DEFINE(got_ip, 0, 1);
static int connect_status;
static bool registered;

static void wifi_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *s = cb->info;

		connect_status = s->status;
		printk("wifi: connect %s (%d)\n", s->status == 0 ? "ok" : "failed", s->status);
		k_sem_give(&connected);
	} else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		printk("wifi: disconnected\n");
	}
}

static void ipv4_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	if (event == NET_EVENT_IPV4_ADDR_ADD) {
		k_sem_give(&got_ip);
	}
}

int wifi_join(const char *ssid, const char *psk, int timeout_s)
{
	struct net_if *iface = net_if_get_first_wifi();
	struct wifi_connect_req_params p = {
		.ssid = ssid,
		.ssid_length = strlen(ssid),
		.psk = psk,
		.psk_length = strlen(psk),
		.security = strlen(psk) ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE,
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_UNKNOWN,
	};

	if (iface == NULL) {
		return -ENODEV;
	}
	if (!registered) {
		net_mgmt_init_event_callback(&wifi_cb, wifi_event,
					     NET_EVENT_WIFI_CONNECT_RESULT |
					     NET_EVENT_WIFI_DISCONNECT_RESULT);
		net_mgmt_add_event_callback(&wifi_cb);
		net_mgmt_init_event_callback(&ipv4_cb, ipv4_event, NET_EVENT_IPV4_ADDR_ADD);
		net_mgmt_add_event_callback(&ipv4_cb);
		registered = true;
	}
	k_sem_reset(&connected);
	k_sem_reset(&got_ip);
	if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &p, sizeof(p)) != 0) {
		return -EIO;
	}
	if (k_sem_take(&connected, K_SECONDS(timeout_s)) != 0) {
		return -ETIMEDOUT;
	}
	if (connect_status != 0) {
		return -ECONNREFUSED;
	}
	if (k_sem_take(&got_ip, K_SECONDS(timeout_s)) != 0) {
		return -ENETUNREACH;
	}
	{
		char a[NET_IPV4_ADDR_LEN], m[NET_IPV4_ADDR_LEN], g[NET_IPV4_ADDR_LEN];
		struct net_if_ipv4 *v4 = iface->config.ip.ipv4;
		struct net_linkaddr *ll = net_if_get_link_addr(iface);

		net_addr_ntop(AF_INET, &v4->unicast[0].ipv4.address.in_addr, a, sizeof(a));
		net_addr_ntop(AF_INET, &v4->unicast[0].netmask, m, sizeof(m));
		net_addr_ntop(AF_INET, &v4->gw, g, sizeof(g));
		printk("wifi: mac %02x:%02x:%02x:%02x:%02x:%02x ip %s mask %s gw %s mtu %u\n", ll->addr[0],
		       ll->addr[1], ll->addr[2], ll->addr[3], ll->addr[4], ll->addr[5], a, m, g,
		       net_if_get_mtu(iface));
	}

	return 0;
}
