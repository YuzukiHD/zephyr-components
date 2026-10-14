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
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/printk.h>

#include "net/wifi.h"

static struct net_mgmt_event_callback wifi_cb, ipv4_cb;
static K_SEM_DEFINE(connected, 0, 1);
static K_SEM_DEFINE(got_ip, 0, 1);
static int connect_status;
static bool registered;
static char current[33];

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
	snprintf(current, sizeof(current), "%s", ssid);
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

const char *wifi_current(void)
{
	return current;
}

/* ---- the scan ------------------------------------------------------------------------------ */

static struct net_mgmt_event_callback scan_cb;
static K_SEM_DEFINE(scan_done, 0, 1);
static struct wifi_ap *scan_out;
static int scan_max, scan_n;

static void scan_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	if (event == NET_EVENT_WIFI_SCAN_RESULT) {
		const struct wifi_scan_result *r = cb->info;
		int i;

		if (r->ssid_length == 0 || r->ssid_length >= sizeof(scan_out[0].ssid)) {
			return;
		}
		for (i = 0; i < scan_n; i++) {
			if (strlen(scan_out[i].ssid) == r->ssid_length && memcmp(scan_out[i].ssid, r->ssid, r->ssid_length) == 0) {
				break;
			}
		}
		if (i == scan_n) {
			if (scan_n >= scan_max) {
				return;
			}
			scan_n++;
			scan_out[i].rssi = INT8_MIN;
			memcpy(scan_out[i].ssid, r->ssid, r->ssid_length);
			scan_out[i].ssid[r->ssid_length] = '\0';
		}
		if (r->rssi > scan_out[i].rssi) {
			scan_out[i].rssi = r->rssi;
		}
		scan_out[i].secured = r->security != WIFI_SECURITY_TYPE_NONE;
	} else if (event == NET_EVENT_WIFI_SCAN_DONE) {
		k_sem_give(&scan_done);
	}
}

int wifi_scan(struct wifi_ap *out, int max)
{
	struct net_if *iface = net_if_get_first_wifi();
	int ret = 0;

	if (iface == NULL) {
		return -ENODEV;
	}
	scan_out = out;
	scan_max = max;
	scan_n = 0;
	k_sem_reset(&scan_done);
	net_mgmt_init_event_callback(&scan_cb, scan_event, NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE);
	net_mgmt_add_event_callback(&scan_cb);
	if (net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0) != 0) {
		ret = -EIO;
	} else if (k_sem_take(&scan_done, K_SECONDS(10)) != 0) {
		ret = -ETIMEDOUT;
	}
	net_mgmt_del_event_callback(&scan_cb);
	if (ret != 0 && scan_n == 0) {
		return ret;
	}
	/* strongest first */
	for (int i = 1; i < scan_n; i++) {
		struct wifi_ap t = out[i];
		int j = i;

		while (j > 0 && out[j - 1].rssi < t.rssi) {
			out[j] = out[j - 1];
			j--;
		}
		out[j] = t;
	}

	return scan_n;
}
