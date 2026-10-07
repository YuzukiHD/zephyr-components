/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Connects to an access point with WPA2-PSK, waits for the address from DHCP and runs an iperf3
 * upload and download against a server.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/printk.h>

#include "iperf3.h"

#define EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

static struct net_mgmt_event_callback wifi_cb, ipv4_cb;
static K_SEM_DEFINE(connected, 0, 1);
static K_SEM_DEFINE(got_ip, 0, 1);

static void wifi_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *s = cb->info;

		printk("connect: %s (%d)\n", s->status == 0 ? "ok" : "failed", s->status);
		if (s->status == 0) {
			k_sem_give(&connected);
		}
	} else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		printk("disconnected\n");
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
	struct wifi_connect_req_params p = {
		.ssid = CONFIG_SAMPLE_WIFI_SSID,
		.ssid_length = strlen(CONFIG_SAMPLE_WIFI_SSID),
		.psk = CONFIG_SAMPLE_WIFI_PSK,
		.psk_length = strlen(CONFIG_SAMPLE_WIFI_PSK),
		.security = WIFI_SECURITY_TYPE_PSK,
		.channel = CONFIG_SAMPLE_WIFI_CHANNEL ? CONFIG_SAMPLE_WIFI_CHANNEL : WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_UNKNOWN,
	};
	uint64_t bytes;

	if (iface == NULL) {
		printk("no WiFi interface\n");
		return 0;
	}
	net_mgmt_init_event_callback(&wifi_cb, wifi_event, EVENTS);
	net_mgmt_add_event_callback(&wifi_cb);
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);

	printk("connecting to %s\n", CONFIG_SAMPLE_WIFI_SSID);
	if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &p, sizeof(p)) != 0) {
		printk("connect request failed\n");
		return 0;
	}
	if (k_sem_take(&connected, K_SECONDS(45)) != 0 || k_sem_take(&got_ip, K_SECONDS(30)) != 0) {
		printk("no connection or no address\n");
		return 0;
	}

	if (strlen(CONFIG_SAMPLE_IPERF3_SERVER) == 0) {
		return 0;
	}
	iperf3_tcp(CONFIG_SAMPLE_IPERF3_SERVER, CONFIG_SAMPLE_IPERF3_PORT,
		   CONFIG_SAMPLE_IPERF3_SECONDS, false, &bytes);
	/* the server needs a moment to be ready for the next test */
	k_sleep(K_SECONDS(2));
	iperf3_tcp(CONFIG_SAMPLE_IPERF3_SERVER, CONFIG_SAMPLE_IPERF3_PORT,
		   CONFIG_SAMPLE_IPERF3_SECONDS, true, &bytes);
	printk("done\n");

	return 0;
}
