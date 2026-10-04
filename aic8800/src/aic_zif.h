/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * The seam between the driver (net_al.c, net_al_wifi.c) and the Zephyr network
 * interface (aic_zephyr_if.c). The two sides cannot include each other's headers,
 * so only plain types cross it.
 */

#ifndef AIC_ZIF_H_
#define AIC_ZIF_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct pbuf;

/* ---- implemented by the driver side ---- */

/** Transmit one Ethernet frame held in @p p; the buffer is taken over (0 on success) */
int aicz_net_tx(struct pbuf *p);

struct aicz_scan_ap {
	uint8_t ssid[33];
	uint8_t ssid_len;
	uint8_t bssid[6];
	uint8_t channel;
	uint8_t band;		/* 0: 2.4 GHz, 1: 5 GHz */
	int8_t rssi;
	uint8_t security;	/* 0: open, 1: WEP, 2: WPA/WPA2 PSK, 3: WPA3 */
};

struct aicz_sta_info {
	bool connected;
	uint8_t ssid[33];
	uint8_t ssid_len;
	uint8_t bssid[6];
	uint8_t channel;
	uint8_t band;
	int8_t rssi;
};

/** Power up the chip and bring the station interface up; idempotent. */
int aicz_wifi_start(void);
int aicz_wifi_scan(void (*cb)(const struct aicz_scan_ap *ap, void *arg), void *arg);
/** Join a network; @p psk_len 0 means an open network. The result comes as a link event. */
int aicz_wifi_connect(const uint8_t *ssid, size_t ssid_len, const uint8_t *psk, size_t psk_len,
		      uint8_t channel);
int aicz_wifi_disconnect(void);
int aicz_wifi_sta_info(struct aicz_sta_info *info);
const uint8_t *aicz_wifi_mac(void);

/* ---- implemented by the Zephyr side ---- */

enum aicz_link_event {
	AICZ_LINK_CONNECTED,	/* keys are in place, the link carries data */
	AICZ_LINK_DISCONNECTED,
};

void aicz_link_event(enum aicz_link_event ev);

/** The Zephyr interface of the (one) station interface */
void *aicz_zif(void);
/** Set the link address; only allowed while the carrier is off */
int aicz_zif_set_mac(void *zif, const uint8_t *mac);
void aicz_zif_carrier(void *zif, bool on);
/** Hand a received Ethernet frame to the IP stack (the data is copied) */
int aicz_zif_rx(void *zif, const void *data, size_t len);
void aicz_zif_set_default(void *zif);
int aicz_zif_dhcp_start(void *zif);
void aicz_zif_dhcp_stop(void *zif);
/** 0 when an IPv4 address is assigned */
int aicz_zif_ip_assigned(void *zif);
/** Addresses are in network byte order; a NULL pointer is skipped */
int aicz_zif_ip_get(void *zif, uint32_t *ip, uint32_t *mask, uint32_t *gw);
int aicz_zif_ip_set(void *zif, uint32_t ip, uint32_t mask, uint32_t gw);

#endif /* AIC_ZIF_H_ */
