/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr network interface of the AIC8800 station: an Ethernet L2 interface with the
 * WiFi management API (scan, connect, disconnect, status). The frames are handed to
 * and taken from the driver in net_al.c; the WiFi core is started on first use.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/printk.h>

#include "aic_pbuf.h"
#include "aic_zif.h"

#define CONNECT_STACK_SIZE	6144
#define CONNECT_PRIORITY	10
/* the join is reported once the keys are in place; a join that does not complete in this time failed */
#define JOIN_TIMEOUT_MS		40000

struct aicz_data {
	struct net_if *iface;
	struct k_work_q connect_q;
	struct k_work connect_work;
	struct k_sem joined;
	struct k_mutex lock;
	uint8_t ssid[33];
	uint8_t ssid_len;
	uint8_t psk[65];
	uint8_t psk_len;
	uint8_t channel;
	bool connecting;
	bool connected;
	scan_result_cb_t scan_cb;
};

static struct aicz_data aicz;
static K_THREAD_STACK_DEFINE(connect_stack, CONNECT_STACK_SIZE);

/* ---- seam to the driver ---- */

void *aicz_zif(void)
{
	return aicz.iface;
}

int aicz_zif_set_mac(void *zif, const uint8_t *mac)
{
	return net_if_set_link_addr(zif, (uint8_t *)mac, 6, NET_LINK_ETHERNET);
}

void aicz_zif_carrier(void *zif, bool on)
{
	if (on) {
		net_eth_carrier_on(zif);
	} else {
		net_eth_carrier_off(zif);
	}
}

int aicz_zif_rx(void *zif, const void *data, size_t len)
{
	struct net_pkt *pkt;

	pkt = net_pkt_rx_alloc_with_buffer(zif, len, AF_UNSPEC, 0, K_NO_WAIT);
	if (pkt == NULL) {
		return -ENOMEM;
	}
	if (net_pkt_write(pkt, data, len) != 0) {
		net_pkt_unref(pkt);
		return -EIO;
	}
	if (net_recv_data(zif, pkt) < 0) {
		net_pkt_unref(pkt);
		return -EIO;
	}

	return 0;
}

void aicz_zif_set_default(void *zif)
{
	net_if_set_default(zif);
}

int aicz_zif_dhcp_start(void *zif)
{
#if defined(CONFIG_NET_DHCPV4)
	net_dhcpv4_start(zif);
	return 0;
#else
	return -ENOTSUP;
#endif
}

void aicz_zif_dhcp_stop(void *zif)
{
#if defined(CONFIG_NET_DHCPV4)
	net_dhcpv4_stop(zif);
#endif
}

int aicz_zif_ip_assigned(void *zif)
{
	return net_if_ipv4_get_global_addr(zif, NET_ADDR_PREFERRED) != NULL ? 0 : -1;
}

int aicz_zif_ip_get(void *zif, uint32_t *ip, uint32_t *mask, uint32_t *gw)
{
	struct in_addr *addr = net_if_ipv4_get_global_addr(zif, NET_ADDR_PREFERRED);

	if (addr == NULL) {
		return -1;
	}
	if (ip != NULL) {
		*ip = addr->s_addr;
	}
	if (mask != NULL) {
		*mask = net_if_ipv4_get_netmask_by_addr(zif, addr).s_addr;
	}
	if (gw != NULL) {
		*gw = net_if_ipv4_get_gw(zif).s_addr;
	}

	return 0;
}

int aicz_zif_ip_set(void *zif, uint32_t ip, uint32_t mask, uint32_t gw)
{
	struct in_addr addr = {.s_addr = ip};
	struct in_addr nm = {.s_addr = mask};
	struct in_addr gateway = {.s_addr = gw};
	struct in_addr *old = net_if_ipv4_get_global_addr(zif, NET_ADDR_PREFERRED);

	/* one address per interface: setting one replaces the other */
	if (old != NULL) {
		struct in_addr o = *old;

		net_if_ipv4_addr_rm(zif, &o);
	}
	if (ip == 0) {
		return 0;
	}
	if (net_if_ipv4_addr_add(zif, &addr, NET_ADDR_MANUAL, 0) == NULL) {
		return -ENOMEM;
	}
	if (mask != 0) {
		net_if_ipv4_set_netmask_by_addr(zif, &addr, &nm);
	}
	if (gw != 0) {
		net_if_ipv4_set_gw(zif, &gateway);
	}

	return 0;
}

void aicz_link_event(enum aicz_link_event ev)
{
	struct aicz_data *d = &aicz;

	if (d->iface == NULL) {
		return;
	}
	switch (ev) {
	case AICZ_LINK_CONNECTED:
		d->connected = true;
		k_sem_give(&d->joined);
		wifi_mgmt_raise_connect_result_event(d->iface, WIFI_STATUS_CONN_SUCCESS);
		aicz_zif_dhcp_start(d->iface);
		break;
	case AICZ_LINK_DISCONNECTED:
		if (d->connected) {
			d->connected = false;
			aicz_zif_dhcp_stop(d->iface);
			wifi_mgmt_raise_disconnect_result_event(d->iface, 0);
		}
		break;
	}
}

/* ---- Ethernet side ---- */

static int aicz_send(const struct device *dev, struct net_pkt *pkt)
{
	size_t len = net_pkt_get_len(pkt);
	struct pbuf *p;

	p = pbuf_alloc(PBUF_RAW, len, PBUF_RAM);
	if (p == NULL) {
		return -ENOMEM;
	}
	net_pkt_cursor_init(pkt);
	if (net_pkt_read(pkt, p->payload, len) != 0) {
		pbuf_free(p);
		return -EIO;
	}

	return aicz_net_tx(p) == 0 ? 0 : -EIO;
}

static enum ethernet_hw_caps aicz_caps(const struct device *dev)
{
	return 0;
}

static void aicz_iface_init(struct net_if *iface)
{
	/* a locally administered address until the chip has told its own one */
	static const uint8_t mac[6] = {0x02, 0x00, 0x00, 0xa1, 0xc8, 0x00};

	aicz.iface = iface;
	net_if_set_link_addr(iface, (uint8_t *)mac, sizeof(mac), NET_LINK_ETHERNET);
	ethernet_init(iface);
	/* the Ethernet layer tells a WiFi interface from a wired one by this type */
	((struct ethernet_context *)net_if_l2_data(iface))->eth_if_type = L2_ETH_IF_TYPE_WIFI;
	/* no carrier until the station has joined a network */
	net_if_carrier_off(iface);
}

/* ---- WiFi management ---- */

static void scan_one(const struct aicz_scan_ap *ap, void *arg)
{
	struct aicz_data *d = arg;
	struct wifi_scan_result res = {0};

	res.ssid_length = ap->ssid_len;
	memcpy(res.ssid, ap->ssid, ap->ssid_len);
	res.band = ap->band ? WIFI_FREQ_BAND_5_GHZ : WIFI_FREQ_BAND_2_4_GHZ;
	res.channel = ap->channel;
	res.rssi = ap->rssi;
	memcpy(res.mac, ap->bssid, 6);
	res.mac_length = 6;
	switch (ap->security) {
	case 0:
		res.security = WIFI_SECURITY_TYPE_NONE;
		break;
	case 1:
		res.security = WIFI_SECURITY_TYPE_WPA_PSK;
		break;
	case 2:
		res.security = WIFI_SECURITY_TYPE_PSK;
		break;
	case 3:
		res.security = WIFI_SECURITY_TYPE_SAE;
		break;
	default:
		res.security = WIFI_SECURITY_TYPE_UNKNOWN;
		break;
	}
	d->scan_cb(d->iface, 0, &res);
}

static int aicz_scan(const struct device *dev, struct wifi_scan_params *params, scan_result_cb_t cb)
{
	struct aicz_data *d = &aicz;
	int ret;

	if (k_mutex_lock(&d->lock, K_NO_WAIT) != 0) {
		return -EBUSY;
	}
	d->scan_cb = cb;
	ret = aicz_wifi_scan(scan_one, d);
	if (ret == 0) {
		/* the end of the list */
		cb(d->iface, 0, NULL);
	}
	k_mutex_unlock(&d->lock);

	return ret == 0 ? 0 : -EIO;
}

/* the join waits for the connection, so it runs in a thread of its own */
static void connect_work_fn(struct k_work *work)
{
	struct aicz_data *d = CONTAINER_OF(work, struct aicz_data, connect_work);
	int ret;

	k_sem_reset(&d->joined);
	ret = aicz_wifi_connect(d->ssid, d->ssid_len, d->psk_len ? d->psk : NULL, d->psk_len,
				d->channel);
	if (ret == 0 && k_sem_take(&d->joined, K_MSEC(JOIN_TIMEOUT_MS)) == 0) {
		d->connecting = false;
		return;
	}
	d->connecting = false;
	wifi_mgmt_raise_connect_result_event(d->iface, ret == 0 ? WIFI_STATUS_CONN_TIMEOUT :
							       WIFI_STATUS_CONN_FAIL);
}

static int aicz_connect(const struct device *dev, struct wifi_connect_req_params *params)
{
	struct aicz_data *d = &aicz;

	if (params->ssid_length == 0 || params->ssid_length > 32) {
		return -EINVAL;
	}
	switch (params->security) {
	case WIFI_SECURITY_TYPE_NONE:
		break;
	case WIFI_SECURITY_TYPE_PSK:
		if (params->psk_length < 8 || params->psk_length > 64) {
			return -EINVAL;
		}
		break;
	default:
		return -ENOTSUP;
	}
	if (d->connecting) {
		return -EBUSY;
	}

	memcpy(d->ssid, params->ssid, params->ssid_length);
	d->ssid_len = params->ssid_length;
	d->psk_len = params->security == WIFI_SECURITY_TYPE_NONE ? 0 : params->psk_length;
	memcpy(d->psk, params->psk, d->psk_len);
	d->channel = params->channel == WIFI_CHANNEL_ANY ? 0 : params->channel;
	d->connecting = true;
	k_work_submit_to_queue(&d->connect_q, &d->connect_work);

	return 0;
}

static int aicz_disconnect(const struct device *dev)
{
	struct aicz_data *d = &aicz;
	int ret = aicz_wifi_disconnect();

	if (d->connected) {
		d->connected = false;
		aicz_zif_dhcp_stop(d->iface);
		wifi_mgmt_raise_disconnect_result_event(d->iface, 0);
	}

	return ret == 0 ? 0 : -EIO;
}

static int aicz_iface_status(const struct device *dev, struct wifi_iface_status *status)
{
	struct aicz_sta_info info;

	memset(status, 0, sizeof(*status));
	aicz_wifi_sta_info(&info);
	status->iface_mode = WIFI_MODE_INFRA;
	status->link_mode = WIFI_LINK_MODE_UNKNOWN;
	status->security = WIFI_SECURITY_TYPE_UNKNOWN;
	if (!info.connected) {
		status->state = aicz.connecting ? WIFI_STATE_ASSOCIATING : WIFI_STATE_DISCONNECTED;
		return 0;
	}
	status->state = WIFI_STATE_COMPLETED;
	status->ssid_len = info.ssid_len;
	memcpy(status->ssid, info.ssid, info.ssid_len);
	memcpy(status->bssid, info.bssid, 6);
	status->band = info.band ? WIFI_FREQ_BAND_5_GHZ : WIFI_FREQ_BAND_2_4_GHZ;
	status->channel = info.channel;
	status->rssi = info.rssi;

	return 0;
}

static const struct wifi_mgmt_ops aicz_mgmt = {
	.scan = aicz_scan,
	.connect = aicz_connect,
	.disconnect = aicz_disconnect,
	.iface_status = aicz_iface_status,
};

static const struct net_wifi_mgmt_offload aicz_api = {
	.wifi_iface.iface_api.init = aicz_iface_init,
	.wifi_iface.send = aicz_send,
	.wifi_iface.get_capabilities = aicz_caps,
	.wifi_mgmt_api = &aicz_mgmt,
};

static int aicz_init(const struct device *dev)
{
	struct aicz_data *d = &aicz;

	k_mutex_init(&d->lock);
	k_sem_init(&d->joined, 0, 1);
	k_work_init(&d->connect_work, connect_work_fn);
	k_work_queue_init(&d->connect_q);
	k_work_queue_start(&d->connect_q, connect_stack, K_THREAD_STACK_SIZEOF(connect_stack),
			   CONNECT_PRIORITY, NULL);
	k_thread_name_set(&d->connect_q.thread, "aic_connect");

	return 0;
}

ETH_NET_DEVICE_INIT(aic8800_wlan, "wlan0", aicz_init, NULL, &aicz, NULL,
		    CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &aicz_api, NET_ETH_MTU);
