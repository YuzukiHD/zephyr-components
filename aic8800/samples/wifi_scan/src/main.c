/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Scans for access points, prints them, and when an SSID is configured connects
 * to it with WPA2-PSK and waits for the address from DHCP.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/printk.h>

#define EVENTS (NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE | \
		NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

static struct net_mgmt_event_callback wifi_cb, ipv4_cb;
static K_SEM_DEFINE(scan_done, 0, 1);
static K_SEM_DEFINE(connected, 0, 1);
static K_SEM_DEFINE(got_ip, 0, 1);
static int found;

static void wifi_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	switch (event) {
	case NET_EVENT_WIFI_SCAN_RESULT: {
		const struct wifi_scan_result *r = cb->info;

		printk("%2d  ch %3d  rssi %4d  %-12s  %.*s\n", ++found, r->channel, r->rssi,
		       r->security == WIFI_SECURITY_TYPE_NONE ? "open" :
		       r->security == WIFI_SECURITY_TYPE_PSK ? "WPA2-PSK" : "secured",
		       r->ssid_length, r->ssid);
		break;
	}
	case NET_EVENT_WIFI_SCAN_DONE:
		k_sem_give(&scan_done);
		break;
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		const struct wifi_status *s = cb->info;

		printk("connect: %s (%d)\n", s->status == 0 ? "ok" : "failed", s->status);
		if (s->status == 0) {
			k_sem_give(&connected);
		}
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		printk("disconnected\n");
		break;
	}
}

static void ipv4_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	if (event == NET_EVENT_IPV4_ADDR_ADD) {
		char buf[NET_IPV4_ADDR_LEN];

		printk("address: %s\n",
		       net_addr_ntop(AF_INET, &iface->config.ip.ipv4->unicast[0].ipv4.address.in_addr,
				     buf, sizeof(buf)));
		k_sem_give(&got_ip);
	}
}

int main(void)
{
	struct net_if *iface = net_if_get_first_wifi();

	if (iface == NULL) {
		printk("no WiFi interface\n");
		return 0;
	}
	net_mgmt_init_event_callback(&wifi_cb, wifi_event, EVENTS);
	net_mgmt_add_event_callback(&wifi_cb);
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);

	printk("scanning\n");
	if (net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0) != 0) {
		printk("scan request failed\n");
		return 0;
	}
	k_sem_take(&scan_done, K_SECONDS(30));
	printk("%d access points\n", found);

	if (strlen(CONFIG_SAMPLE_WIFI_SSID) > 0) {
		struct wifi_connect_req_params p = {
			.ssid = CONFIG_SAMPLE_WIFI_SSID,
			.ssid_length = strlen(CONFIG_SAMPLE_WIFI_SSID),
			.psk = CONFIG_SAMPLE_WIFI_PSK,
			.psk_length = strlen(CONFIG_SAMPLE_WIFI_PSK),
			.security = strlen(CONFIG_SAMPLE_WIFI_PSK) ? WIFI_SECURITY_TYPE_PSK :
								     WIFI_SECURITY_TYPE_NONE,
			.channel = WIFI_CHANNEL_ANY,
			.band = WIFI_FREQ_BAND_UNKNOWN,
		};

		printk("connecting to %s\n", CONFIG_SAMPLE_WIFI_SSID);
		if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &p, sizeof(p)) != 0) {
			printk("connect request failed\n");
			return 0;
		}
		if (k_sem_take(&connected, K_SECONDS(30)) == 0 &&
		    k_sem_take(&got_ip, K_SECONDS(30)) != 0) {
			printk("no address from DHCP\n");
		}
	}

	return 0;
}
