/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * The station functions the Zephyr WiFi management interface needs (scan, join,
 * leave, status) on top of the interface calls of the WiFi core. All of them are
 * blocking and meant to be called from a thread of their own.
 */

#include <string.h>

#include "lmac_types.h"
#include "fhost_wpa.h"
#include "fhost_config.h"
#include "fhost_cntrl.h"
#include "fhost_api.h"
#include "rtos_al.h"
#include "net_al.h"
#include "fhost.h"
#include "mac.h"
#include "rwnx_utils.h"
#include "wifi.h"
#include "wlan_if.h"
#include "aic_log.h"
#include "aic_zif.h"

static bool wifi_started;
static uint8_t last_ssid[33];
static uint8_t last_ssid_len;

static void wifi_event(AIC_WIFI_EVENT event, aic_wifi_event_data *data)
{
    switch (event) {
    case JOIN_SUCCESS_EVENT:
        aicz_link_event(AICZ_LINK_CONNECTED);
        break;
    case STA_DISCONNECT_EVENT:
    case LEAVE_RESULT_EVENT:
        aicz_link_event(AICZ_LINK_DISCONNECTED);
        break;
    default:
        break;
    }
}

int aicz_wifi_start(void)
{
    int ret;

    if (wifi_started)
        return 0;

    ret = aic_wifi_init(WIFI_MODE_STA, 0, NULL);
    if (ret)
        return ret;

    aic_wifi_event_register(wifi_event);
    wifi_started = true;

    return 0;
}

const uint8_t *aicz_wifi_mac(void)
{
    return get_mac_address();
}

static uint8_t scan_security(const struct mac_scan_result *r)
{
    if (r->akm == 0)
        return 0;
    if (r->akm & (CO_BIT(MAC_AKM_SAE) | CO_BIT(MAC_AKM_FT_OVER_SAE)))
        return 3;
    if (r->akm & (CO_BIT(MAC_AKM_PSK) | CO_BIT(MAC_AKM_PSK_SHA256) | CO_BIT(MAC_AKM_FT_PSK)))
        return 2;
    if (r->akm & CO_BIT(MAC_AKM_PRE_RSN))
        return 1;

    return 4;
}

int aicz_wifi_scan(void (*cb)(const struct aicz_scan_ap *ap, void *arg), void *arg)
{
    struct fhost_cntrl_link *link;
    struct mac_scan_result result;
    int ret, idx = 0;

    ret = aicz_wifi_start();
    if (ret)
        return ret;

    link = fhost_cntrl_cfgrwnx_link_open();
    if (link == NULL) {
        aic_dbg("Failed to open link with control task\n");
        return -1;
    }

    ret = fhost_scan(link, 0, NULL);
    if (ret < 0) {
        fhost_cntrl_cfgrwnx_link_close(link);
        return ret;
    }

    while (fhost_get_scan_results(link, idx++, 1, &result)) {
        struct aicz_scan_ap ap = {0};

        ap.ssid_len = result.ssid.length > 32 ? 32 : result.ssid.length;
        memcpy(ap.ssid, result.ssid.array, ap.ssid_len);
        memcpy(ap.bssid, result.bssid.array, 6);
        if (result.chan) {
            ap.band = result.chan->band;
            ap.channel = phy_freq_to_channel(result.chan->band, result.chan->freq);
        }
        ap.rssi = result.rssi;
        ap.security = scan_security(&result);
        cb(&ap, arg);
    }
    fhost_cntrl_cfgrwnx_link_close(link);

    return 0;
}

int aicz_wifi_connect(const uint8_t *ssid, size_t ssid_len, const uint8_t *psk, size_t psk_len,
                      uint8_t channel)
{
    char ssid_str[33];
    char psk_str[65];
    struct vif_info_tag *vif;
    int ret;

    if (ssid_len == 0 || ssid_len > 32 || psk_len > 64)
        return -1;

    ret = aicz_wifi_start();
    if (ret)
        return ret;

    memcpy(ssid_str, ssid, ssid_len);
    ssid_str[ssid_len] = '\0';
    memcpy(last_ssid, ssid, ssid_len);
    last_ssid_len = ssid_len;
    if (psk_len) {
        memcpy(psk_str, psk, psk_len);
        psk_str[psk_len] = '\0';
    }

    /* after a leave the interface has to be set up again */
    vif = fhost_to_mac_vif(0);
    if (vif == NULL || vif->type != VIF_STA) {
        ret = wlan_start_sta((uint8_t *)ssid_str, (uint8_t *)"", -1);
        if (ret < 0)
            return ret;
    }

    set_sta_connect_chan_num(channel);

    return wlan_sta_connect((uint8_t *)ssid_str, psk_len ? (uint8_t *)psk_str : NULL, 0);
}

int aicz_wifi_disconnect(void)
{
    struct vif_info_tag *vif = fhost_to_mac_vif(0);

    if (!wifi_started || vif == NULL || vif->type != VIF_STA)
        return 0;

    return wlan_disconnect_sta(0);
}

int aicz_wifi_sta_info(struct aicz_sta_info *info)
{
    struct vif_info_tag *vif = wifi_started ? fhost_to_mac_vif(0) : NULL;

    memset(info, 0, sizeof(*info));
    if (vif == NULL || vif->type != VIF_STA || !vif->active)
        return 0;
    if (vif->u.sta.ap_id == INVALID_STA_IDX)
        return 0;

    info->connected = true;
    memcpy(info->ssid, last_ssid, last_ssid_len);
    info->ssid_len = last_ssid_len;
    memcpy(info->bssid, sta_info_tab[vif->u.sta.ap_id].mac_addr.array, 6);
    info->band = vif->chan.band;
    info->channel = phy_freq_to_channel(vif->chan.band, vif->chan.prim20_freq);

    return 0;
}
