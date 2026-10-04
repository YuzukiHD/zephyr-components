/*
 * Driver interface for RWNX platform
 * Copyright (C) RivieraWaves 2017-2019
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "driver.h"
#include "eloop.h"
#include "cfgrwnx.h"
#include "mac_frame.h"
#include "fhost.h"
#include "fhost_cntrl.h"
#include "fhost_wpa.h"
#include "common/ieee802_11_common.h"
#include "rwnx_config.h"
#include "wifi.h"
#include "rwnx_ieee80211.h"
#include "fhost_config.h"

#ifdef CONFIG_AP
#define NB_TX_FRAME NX_REMOTE_STA_MAX
#else
#define NB_TX_FRAME 1
#endif

#define TX_FRAME_TO_MS 300

struct wpa_rwnx_tx_frame {
	// pointer to drv data
	struct wpa_rwnx_driver_data *drv;
	// frame data
	u8 *data;
	// frame data length
	size_t data_len;
	// eapol frame
	bool eapol;
	// dst addr (only for eapol)
	u8 dst_addr[ETH_ALEN];
};

struct wpa_rwnx_driver_data {
	// WPA_supplicant context
	void *ctx;
	// Index, at FHOST level, of the interface
	int fhost_vif_idx;
	// Initial interface type
	enum mac_vif_type vif_init_type;
	// List of scan results
	struct dl_list scan_res;
	// Driver status
	int status;
	// MAC address of the AP we are connected to
	u8 bssid[ETH_ALEN];
	// SSID of the AP we are connected to
	u8 *ssid;
	// SSID length
	u8 ssid_len;
	// Next authentication alg to try (used when connect with several algos)
	int next_auth_alg;
	// cntrl link parameters
	struct fhost_cntrl_link *link;
	// pending TX frames
	struct wpa_rwnx_tx_frame tx_frame[NB_TX_FRAME];
	// TX frame semaphore
	rtos_semaphore tx_frame_sem;
	// unicast key type
	u32 pairwise_ciphers;
    //
	u32 key_mgmt_suites;
	//indicate a wpa3 sta is active
	u32 sta_wpa3_active;
	// filter ssids
	u8 filter_ssid[SSID_MAX_LEN];
	u16 filter_ssid_len;
	unsigned int pending_remain_on_chan:1;
	// Pending EAPOL frame (held until association event is processed)
	struct {
		bool valid;
		uint8_t addr[ETH_ALEN];
		uint8_t *payload;
		uint16_t length;
	} pending_eapol;
};

struct wpa_rwnx_driver_scan_res {
	struct dl_list list;
	struct wpa_scan_res *res;
};

enum wpa_rwnx_driver_status {
	RWNX_ASSOCIATED = BIT(0),
	RWNX_DISASSOC_PENDING = BIT(1),
	RWNX_COMPLETED = BIT(2),
	RWNX_AP_STARTED = BIT(3),
	RWNX_EXITING = BIT(4),
};

// For STA only accept action frames
#define STA_MGMT_RX_FILTER ~CO_BIT(WLAN_FC_STYPE_ACTION)

// For AP accept everyting except beacon
#define AP_MGMT_RX_FILTER CO_BIT(WLAN_FC_STYPE_BEACON)

int wpa_rwnx_driver_send(void *msg);
/******************************************************************************
 * Hostapd to cfgrwnx type conversion and utils functions
 *****************************************************************************/
static void rwnx_to_hostapd_channel(struct mac_chan_def *rwnx,
				    struct hostapd_channel_data *hostapd,
				    uint8_t max_bw)
{
	u8 channel;
	uint16_t bw_flags;

	memset(hostapd, 0, sizeof(*hostapd));

	hostapd->freq = rwnx->freq;
	if (ieee80211_freq_to_chan(hostapd->freq, &channel) != NUM_HOSTAPD_MODES)
		hostapd->chan = channel;
	hostapd->flag = 0;
	hostapd->dfs_cac_ms = 0;
	hostapd->max_tx_power = rwnx->tx_power;
	hostapd->allowed_bw = HOSTAPD_CHAN_WIDTH_20;

	if (rwnx->flags & CHAN_NO_IR) {
		hostapd->flag |= HOSTAPD_CHAN_NO_IR;
		hostapd->flag |= HOSTAPD_CHAN_RADAR | HOSTAPD_CHAN_DFS_USABLE;
		hostapd->dfs_cac_ms = 60000;
	}
	if (rwnx->flags & CHAN_DISABLED) {
		hostapd->flag |= HOSTAPD_CHAN_DISABLED;
	}

	dl_list_init(&hostapd->survey_list);

	if (max_bw < PHY_CHNL_BW_40)
		return;

	bw_flags = fhost_chan_bw_flags_get_by_freq(rwnx->freq);

	if (bw_flags & CHAN_HT40P)
	{
		hostapd->flag |= HOSTAPD_CHAN_HT40PLUS;
		hostapd->allowed_bw |= HOSTAPD_CHAN_WIDTH_40P;
	}
	if (bw_flags & CHAN_HT40M)
	{
		hostapd->flag |= HOSTAPD_CHAN_HT40MINUS;
		hostapd->allowed_bw |= HOSTAPD_CHAN_WIDTH_40M;
	}

	if (max_bw < PHY_CHNL_BW_80)
		return;

	if (bw_flags & CHAN_VHT80_10_70)
	{
		hostapd->flag |= HOSTAPD_CHAN_VHT_10_70;
		hostapd->allowed_bw |= HOSTAPD_CHAN_WIDTH_80;
	}
	else if (bw_flags & CHAN_VHT80_30_50)
	{
		hostapd->flag |= HOSTAPD_CHAN_VHT_30_50;
		hostapd->allowed_bw |= HOSTAPD_CHAN_WIDTH_80;
	}
	else if (bw_flags & CHAN_VHT80_50_30)
	{
		hostapd->flag |= HOSTAPD_CHAN_VHT_50_30;
		hostapd->allowed_bw |= HOSTAPD_CHAN_WIDTH_80;
	}
	else if (bw_flags & CHAN_VHT80_70_10)
	{
		hostapd->flag |= HOSTAPD_CHAN_VHT_70_10;
		hostapd->allowed_bw |= HOSTAPD_CHAN_WIDTH_80;
	}
}

static void hostapd_to_rwnx_op_channel(struct hostapd_freq_params *hostapd,
				       struct mac_chan_op *rwnx)
{
	rwnx->band = (hostapd->mode == HOSTAPD_MODE_IEEE80211A) ? PHY_BAND_5G : PHY_BAND_2G4;
	rwnx->prim20_freq = hostapd->freq;
	rwnx->center1_freq = hostapd->center_freq1;
	rwnx->center2_freq = hostapd->center_freq2;
	switch (hostapd->bandwidth)
	{
	case 160:
		rwnx->type = PHY_CHNL_BW_160;
		break;
	case 80:
		if (rwnx->center2_freq)
			rwnx->type = PHY_CHNL_BW_80P80;
		else
			rwnx->type = PHY_CHNL_BW_80;
		break;
	case 40:
		rwnx->type = PHY_CHNL_BW_40;
		break;
	default:
		rwnx->type = PHY_CHNL_BW_20;
		break;
	}
	rwnx->tx_power = 20;
	rwnx->flags = 0;
}

static int hostapd_to_rwnx_cipher(enum wpa_alg alg, size_t key_len)
{
	switch (alg) {
	case WPA_ALG_WEP:
		if (key_len == 5)
			return MAC_CIPHER_WEP40;
		return MAC_CIPHER_WEP104;
	case WPA_ALG_TKIP:
		return MAC_CIPHER_TKIP;
	case WPA_ALG_CCMP:
		return MAC_CIPHER_CCMP;
	case WPA_ALG_IGTK:
		return MAC_CIPHER_BIP_CMAC_128;
	case WPA_ALG_SMS4:
		return MAC_CIPHER_WPI_SMS4;
	case WPA_ALG_GCMP:
	case WPA_ALG_CCMP_256:
	case WPA_ALG_GCMP_256:
	case WPA_ALG_BIP_GMAC_128:
	case WPA_ALG_BIP_GMAC_256:
	case WPA_ALG_BIP_CMAC_256:
	case WPA_ALG_KRK:
	case WPA_ALG_NONE:
	case WPA_ALG_PMK:
		return MAC_CIPHER_INVALID;
	}

	return MAC_CIPHER_INVALID;
}

#define MAC_AUTH_ALGO_INVALID 0xffff
static int hostapd_to_rwnx_auth_alg(int auth_alg)
{
	switch (auth_alg)
	{
	case WPA_AUTH_ALG_OPEN:
		return  MAC_AUTH_ALGO_OPEN;
	case WPA_AUTH_ALG_SHARED:
		return MAC_AUTH_ALGO_SHARED;
	case WPA_AUTH_ALG_FT:
		return MAC_AUTH_ALGO_FT;
	case WPA_AUTH_ALG_SAE:
		return MAC_AUTH_ALGO_SAE;
	case WPA_AUTH_ALG_LEAP:
	default:
		return MAC_AUTH_ALGO_INVALID;
	}
}

static struct wpa_rwnx_tx_frame *
wpa_rwnx_driver_init_tx_frame(struct wpa_rwnx_driver_data *drv, const u8 *data,
			      size_t data_len, const u8 *dst_addr)
{
	struct wpa_rwnx_tx_frame *tx_frame = NULL;
	int i;

	if (rtos_semaphore_wait(drv->tx_frame_sem, TX_FRAME_TO_MS))
		return NULL;

	for (i = 0, tx_frame = drv->tx_frame ; i < NB_TX_FRAME; i++, tx_frame++) {
		if (!tx_frame->data)
			break;
	}

	if (i == NB_TX_FRAME) {
		rtos_semaphore_signal(drv->tx_frame_sem, false);
		TRACE_FHOST("[WPA] got TX semaphore but no frame available\r\n");
		return NULL;
	}

	tx_frame->data = os_malloc(data_len);
	if (!tx_frame->data) {
		rtos_semaphore_signal(drv->tx_frame_sem, false);
		TRACE_FHOST("[WPA] Failed to allocate frame buffer\r\n");
		return NULL;
	}

	if (dst_addr) {
		tx_frame->eapol = true;
		os_memcpy(tx_frame->dst_addr, dst_addr, ETH_ALEN);
	} else {
		tx_frame->eapol = false;
	}

	os_memcpy(tx_frame->data, data, data_len);
	tx_frame->data_len = data_len;
	tx_frame->drv = drv;

	return tx_frame;
}

static int wpa_rwnx_driver_wait_tx_frame(struct wpa_rwnx_driver_data *drv)
{
	int i, res = 0;

	// Wait each pending TX_FRAME up to TX_FRAME_TO_MS
	for (i = 0; i < NB_TX_FRAME - rtos_semaphore_get_count(drv->tx_frame_sem); i++) {
		if (rtos_semaphore_wait(drv->tx_frame_sem, TX_FRAME_TO_MS))
			res++;
	}

	return res;
}

/******************************************************************************
 * Event processing functions
 *****************************************************************************/
static void wpa_rwnx_driver_process_scan_result(struct wpa_rwnx_driver_data *drv)
{
	struct ieee80211_mgmt *mgmt;
	struct cfgrwnx_scan_result res;
	struct wpa_rwnx_driver_scan_res *drv_res, *prev_res = NULL;
	struct wpa_scan_res *wpa_res;
	u16 fc;
	u8 *ie, *dst, *prev_src;
	bool is_beacon = false;
	int len = 0, ie_len;

	if ((fhost_cntrl_cfgrwnx_event_get(drv->link, &res, sizeof(res)) < 0) ||
	    !res.payload)
		return;

	if (res.length < offsetof(struct ieee80211_mgmt, u.beacon.variable))
		goto free_payload;

	mgmt = (struct ieee80211_mgmt *)res.payload;
	fc = le_to_host16(mgmt->frame_control);

	if (WLAN_FC_GET_TYPE(fc) != WLAN_FC_TYPE_MGMT)
		goto free_payload;
	if (WLAN_FC_GET_STYPE(fc) == WLAN_FC_STYPE_PROBE_RESP) {
		is_beacon = false;
		ie = mgmt->u.probe_resp.variable;
		ie_len = res.length - offsetof(struct ieee80211_mgmt, u.probe_resp.variable);
	} else if (WLAN_FC_GET_STYPE(fc) == WLAN_FC_STYPE_BEACON) {
		is_beacon = true;
		ie = mgmt->u.beacon.variable;
		ie_len = res.length - offsetof(struct ieee80211_mgmt, u.beacon.variable);
	} else {
		goto free_payload;
	}

	/* TODO: Add suport for filter option in scan request */
	const u8 *ssid_ie = get_ie(ie, ie_len, WLAN_EID_SSID);
	if(ssid_ie) {
		if (0 != memcmp(drv->filter_ssid, &(ssid_ie[2]), drv->filter_ssid_len)) {
			goto free_payload;
		}
	}

	/* Check if result for this bssid is already present */
	dl_list_for_each(drv_res, &drv->scan_res,
			 struct wpa_rwnx_driver_scan_res, list) {
		if (MAC_ADDR_CMP_PACKED(mgmt->bssid, drv_res->res->bssid)) {
			prev_res = drv_res;
			break;
		}
	}

	if (prev_res) {
		if ((is_beacon && prev_res->res->beacon_ie_len) ||
		    (!is_beacon && prev_res->res->ie_len)) {
			/* assume content didn't change */
			goto free_payload;
		} else if (is_beacon) {
			len = prev_res->res->ie_len;
		} else {
			len = prev_res->res->beacon_ie_len;
		}
		prev_src = (u8 *)prev_res->res + sizeof(struct wpa_scan_res);
	}
	len += sizeof(struct wpa_scan_res) + ie_len;

	drv_res = os_malloc(sizeof(struct wpa_rwnx_driver_scan_res));
	if (!drv_res)
		goto free_payload;

	wpa_res = os_malloc(len);
	if (!wpa_res) {
		os_free(drv_res);
		goto free_payload;
	}

	wpa_res->flags = WPA_SCAN_QUAL_INVALID | WPA_SCAN_NOISE_INVALID | WPA_SCAN_LEVEL_DBM;
	os_memcpy(wpa_res->bssid, mgmt->bssid, ETH_ALEN);
	wpa_res->freq = res.freq;
	if (is_beacon) {
		u64 *tsf = (u64 *)mgmt->u.beacon.timestamp;
		wpa_res->tsf = le_to_host64(*tsf);
		wpa_res->beacon_int = le_to_host16(mgmt->u.beacon.beacon_int);
		wpa_res->caps = le_to_host16(mgmt->u.beacon.capab_info);
	} else {
		u64 *tsf = (u64 *)mgmt->u.probe_resp.timestamp;
		wpa_res->tsf = le_to_host64(*tsf);
		wpa_res->beacon_int = le_to_host16(mgmt->u.probe_resp.beacon_int);
		wpa_res->caps = le_to_host16(mgmt->u.probe_resp.capab_info);
	}
	wpa_res->level = res.rssi;
	wpa_res->age = 0; /* TODO */
	wpa_res->est_throughput = 0;
	wpa_res->snr = 0;

	dst = (u8 *)wpa_res + sizeof(struct wpa_scan_res);
	if (is_beacon) {
		wpa_res->beacon_ie_len = ie_len;
		if (prev_res) {
			wpa_res->ie_len = prev_res->res->ie_len;
			os_memcpy(dst, prev_src, wpa_res->ie_len);
			dst += wpa_res->ie_len;
		} else {
			wpa_res->ie_len = 0;
		}
		os_memcpy(dst, ie, wpa_res->beacon_ie_len);
	} else {
		wpa_res->ie_len = ie_len;
		os_memcpy(dst, ie, wpa_res->ie_len);
		if (prev_res) {
			dst += wpa_res->ie_len;
			wpa_res->beacon_ie_len = prev_res->res->beacon_ie_len;
			os_memcpy(dst, prev_src, wpa_res->beacon_ie_len);
		} else {
			wpa_res->beacon_ie_len = 0;
		}
	}

	drv_res->res = wpa_res;
	dl_list_add(&drv->scan_res, &drv_res->list);

	if (prev_res) {
		dl_list_del(&prev_res->list);
		os_free(prev_res->res);
		os_free(prev_res);
	}

free_payload:
	rtos_free(res.payload);
}
extern aic_wifi_event_cb g_aic_wifi_event_cb;

static void wpa_rwnx_driver_flush_pending_eapol(struct wpa_rwnx_driver_data *drv);

static void wpa_rwnx_driver_process_connect_event(struct wpa_rwnx_driver_data *drv)
{
	union wpa_event_data data;
	struct cfgrwnx_connect_event event;

	if (fhost_cntrl_cfgrwnx_event_get(drv->link, &event, sizeof(event)) < 0)
		return;

	memset(&data, 0, sizeof(union wpa_event_data));

	if (event.status_code != WLAN_STATUS_SUCCESS) {
		data.assoc_reject.bssid = (u8 *)&event.bssid;
		data.assoc_reject.status_code = event.status_code;

		if ((data.assoc_reject.status_code == WLAN_STATUS_UNSPECIFIED_FAILURE) &&
		    (event.assoc_resp_ie_len == 0)) {
			data.assoc_reject.resp_ies = NULL ;
			data.assoc_reject.resp_ies_len = 0;
			data.assoc_reject.timed_out = 1;
		} else {
			data.assoc_reject.resp_ies = (event.req_resp_ies +
						      event.assoc_req_ie_len);
			data.assoc_reject.resp_ies_len = event.assoc_resp_ie_len;
			data.assoc_reject.timed_out = 0;
		}

		if (drv->next_auth_alg &&
		    (event.status_code == WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG)) {
			// If several authentication algs were specified (i.e.
			// next_auth_alg), then we can remove the one we just
			// tried (MSB) from the list
			drv->next_auth_alg &= ~(1 << (31 - co_clz(drv->next_auth_alg)));
		}

		wpa_supplicant_event(drv->ctx, EVENT_ASSOC_REJECT, &data);
	} else {
		const u8 *ssid_ie;
		data.assoc_info.reassoc = 0;
		data.assoc_info.req_ies = event.req_resp_ies;
		data.assoc_info.req_ies_len = event.assoc_req_ie_len;
		data.assoc_info.resp_ies = event.req_resp_ies + event.assoc_req_ie_len;
		data.assoc_info.resp_ies_len = event.assoc_resp_ie_len;
		data.assoc_info.beacon_ies = NULL;
		data.assoc_info.beacon_ies_len = 0;
		data.assoc_info.freq = event.freq;
		data.assoc_info.wmm_params.info_bitmap = 0;
		data.assoc_info.addr = (u8 *)&event.bssid;
		data.assoc_info.subnet_status = 0;

		aic_dbg("!!! CONNECT event: set RWNX_ASSOCIATED\r\n");
			drv->status |= RWNX_ASSOCIATED;
		memcpy(drv->bssid, &event.bssid, ETH_ALEN);
		ssid_ie = get_ie(event.req_resp_ies, event.assoc_req_ie_len, WLAN_EID_SSID);
		if (ssid_ie) {
			drv->ssid = os_malloc(ssid_ie[1]);
			if (drv->ssid) {
				drv->ssid_len = ssid_ie[1];
				memcpy(drv->ssid, &ssid_ie[2], ssid_ie[1]);
			}
		}
		wpa_supplicant_event(drv->ctx, EVENT_ASSOC, &data);
		wpa_rwnx_driver_flush_pending_eapol(drv);
	}
	if (g_aic_wifi_event_cb) {
		aic_wifi_event_data enData = {0};
		enData.data.join_data.reserved[0] = event.status_code;
		g_aic_wifi_event_cb(STA_ASSOC_EVENT, &enData);
	}
	if (event.req_resp_ies)
		rtos_free(event.req_resp_ies);
}

static void wpa_rwnx_driver_process_disconnect_event(struct wpa_rwnx_driver_data *drv)
{
	union wpa_event_data data;
	struct cfgrwnx_disconnect_event event;

	if (fhost_cntrl_cfgrwnx_event_get(drv->link, &event, sizeof(event)) < 0)
		return;

	data.disassoc_info.addr = drv->bssid;
	data.disassoc_info.reason_code = event.reason_code;
	aic_dbg("!!! DISCONNECT reason_code=%d locally_generated=%d\r\n",
		event.reason_code, !!(drv->status & RWNX_DISASSOC_PENDING));
	data.disassoc_info.ie = NULL;
	data.disassoc_info.ie_len = 0;
	data.disassoc_info.locally_generated = !!(drv->status & RWNX_DISASSOC_PENDING);

	drv->status &= ~(RWNX_ASSOCIATED | RWNX_DISASSOC_PENDING);
	if (drv->ssid) {
		os_free(drv->ssid);
		drv->ssid = NULL;
		drv->ssid_len = 0;
	}
	if (g_aic_wifi_event_cb) {
		aic_wifi_event_data enData = {0};
		g_aic_wifi_event_cb(STA_DISCONNECT_EVENT, &enData);
	}
	if (drv->pending_eapol.payload) {
		rtos_free(drv->pending_eapol.payload);
		drv->pending_eapol.payload = NULL;
	}
	drv->pending_eapol.valid = false;
	wpa_supplicant_event(drv->ctx, EVENT_DISASSOC, &data);
}


static void wpa_rwnx_driver_process_mic_failure_event(struct wpa_rwnx_driver_data *drv)
{
	union wpa_event_data data;
	struct cfgrwnx_mic_failure_event event;

	if (fhost_cntrl_cfgrwnx_event_get(drv->link, &event, sizeof(event)) < 0)
		return;

	data.michael_mic_failure.src = (u8 *)&event.addr;
	data.michael_mic_failure.unicast = event.ga ? 0 : 1;

	wpa_supplicant_event(drv->ctx, EVENT_MICHAEL_MIC_FAILURE, &data);
}

void remove_sec_hdr_mgmt_frame(union wpa_event_data *data,u16 pairwise_ciphers)
{
    u8 hdr_len = 24;
    struct ieee80211_mgmt *mgmt;
    u8 mgmt_header[24] = {0};
    u16 fc;
    bool rx_mc,rx_bc;

    mgmt = (struct ieee80211_mgmt *)data->rx_mgmt.frame;
    rx_bc = is_broadcast_ether_addr(mgmt->da);
    rx_mc = is_multicast_ether_addr(mgmt->da);
    if((!rx_bc) && (!rx_mc)){
        fc = le_to_host16(mgmt->frame_control);
        if((WLAN_FC_GET_TYPE(fc) == WLAN_FC_TYPE_MGMT) && ((fc & WLAN_FC_ISWEP) != 0)){
            if(pairwise_ciphers == WPA_CIPHER_CCMP){
                memcpy(mgmt_header,(void *)mgmt,hdr_len);
                memcpy((void *)(data->rx_mgmt.frame + MAC_IV_LEN + MAC_EIV_LEN),mgmt_header,hdr_len);
                data->rx_mgmt.frame_len -= (MAC_IV_LEN + MAC_EIV_LEN);
                data->rx_mgmt.frame += (MAC_IV_LEN + MAC_EIV_LEN);
            }
            else {
                AIC_LOG_PRINTF("unsupport wpa3 cipher:%x\n",pairwise_ciphers);
            }
        }
    }
}

void wpa_rwnx_driver_process_rx_mgmt_event(struct wpa_rwnx_driver_data *drv, struct cfgrwnx_rx_mgmt_event *event)
{
	union wpa_event_data data;
	struct ieee80211_mgmt *mgmt;

	mgmt = (struct ieee80211_mgmt *)event->payload;

	if((drv->key_mgmt_suites == WPA_KEY_MGMT_SAE) && ((mgmt->frame_control & MAC_FCTRL_PROTECTEDFRAME) == 0)) {
		if (((WLAN_FC_GET_STYPE(le_to_host16(mgmt->frame_control)) == WLAN_FC_STYPE_DEAUTH) ||
		(WLAN_FC_GET_STYPE(le_to_host16(mgmt->frame_control)) == WLAN_FC_STYPE_DISASSOC)) &&
		(mgmt->u.deauth.reason_code == WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA ||
		mgmt->u.deauth.reason_code == WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA)) {
			u8 staid = vif_mgmt_get_staid(fhost_to_mac_vif(0),(const struct mac_addr *)mgmt->sa);
			if(drv->sta_wpa3_active & CO_BIT(staid%32)){
				rtos_free(event->payload);
				event->payload = NULL;
				return;
			}
		}
	}

	data.rx_mgmt.frame = event->payload;
	data.rx_mgmt.frame_len = event->length;
	remove_sec_hdr_mgmt_frame(&data,drv->pairwise_ciphers);

	data.rx_mgmt.datarate = 0;
	data.rx_mgmt.drv_priv = drv;
	data.rx_mgmt.freq = event->freq;
	data.rx_mgmt.ssi_signal = event->rssi;

	if ((WLAN_FC_GET_STYPE(le_to_host16(mgmt->frame_control)) == WLAN_FC_STYPE_AUTH) &&
	    (le_to_host16(mgmt->u.auth.auth_alg) == WLAN_AUTH_SAE)) {
		// Since SAE authentication takes a lot of time to process ignore probe
		// request to avoid overflowed the event socket.
		// Since we are using external authentication in STA mode, wpa_supplicant
		// will call the send_external_auth_status callback when SAE authentication
		// is done even in AP mode, so filters are reset in this function.
		fhost_wpa_set_mgmt_rx_filter(drv->fhost_vif_idx,
					     AP_MGMT_RX_FILTER |
					     CO_BIT(WLAN_FC_STYPE_PROBE_REQ));

	}

	wpa_supplicant_event(drv->ctx, EVENT_RX_MGMT, &data);
	rtos_free(event->payload);
	event->payload = NULL;
}

static void wpa_rwnx_driver_process_external_auth_event(struct wpa_rwnx_driver_data *drv)
{
	union wpa_event_data data;
	struct cfgrwnx_external_auth_event event;

	if ((fhost_cntrl_cfgrwnx_event_get(drv->link, &event, sizeof(event)) < 0) ||
	    (event.fhost_vif_idx != drv->fhost_vif_idx))
		return;

	data.external_auth.action = EXT_AUTH_START;
	data.external_auth.key_mgmt_suite = event.akm;
	data.external_auth.bssid = (u8 *)event.bssid.array;
	data.external_auth.ssid = (u8 *)event.ssid.array;
	data.external_auth.ssid_len = event.ssid.length;

	// Need to forward Authentication frame for external authentication procedure
	fhost_wpa_set_mgmt_rx_filter(drv->fhost_vif_idx,
				     STA_MGMT_RX_FILTER ^ CO_BIT(WLAN_FC_STYPE_AUTH));
	wpa_supplicant_event(drv->ctx, EVENT_EXTERNAL_AUTH, &data);
}

#if 0
static void wpa_rwnx_driver_process_tx_status_event(struct wpa_rwnx_driver_data *drv)
{
	union wpa_event_data data;
	struct cfgrwnx_tx_status_event event;
	struct wpa_rwnx_tx_frame *tx_frame;
	enum wpa_event_type wpa_event;
	uint8_t *data_buf;

	if (fhost_cntrl_cfgrwnx_event_get(drv->link, &event, sizeof(event)) < 0)
		return;

	tx_frame = (struct wpa_rwnx_tx_frame *)event.data;

	if (tx_frame->eapol)
	{
		data.eapol_tx_status.dst = tx_frame->dst_addr;
		data.eapol_tx_status.data = tx_frame->data;
		data.eapol_tx_status.data_len = tx_frame->data_len;
		data.eapol_tx_status.ack = event.acknowledged;
		wpa_event = EVENT_EAPOL_TX_STATUS;
	}
	else
	{
		data.tx_status.type = WLAN_FC_GET_TYPE(tx_frame->data[0]);
		data.tx_status.stype = WLAN_FC_GET_STYPE(tx_frame->data[0]);
		data.tx_status.dst = ((struct ieee80211_hdr *)tx_frame->data)->addr1;
		data.tx_status.data = tx_frame->data;
		data.tx_status.data_len = tx_frame->data_len;
		data.tx_status.ack = event.acknowledged;
		wpa_event = EVENT_TX_STATUS;
	}

	// Release before calling wpa_supplicant_event which may lead to get a new tx_frame
	data_buf = tx_frame->data;
	tx_frame->data = NULL;
	wpa_supplicant_event(drv->ctx, wpa_event, &data);
	os_free(data_buf);
}
#else
static void wpa_rwnx_driver_process_tx_status_event(struct wpa_rwnx_driver_data *drv, struct cfgrwnx_tx_status_event *event)
{
	union wpa_event_data data;
	//struct cfgrwnx_tx_status_event event;
	struct wpa_rwnx_tx_frame *tx_frame;
	enum wpa_event_type wpa_event;
	uint8_t *data_buf;

	//if (fhost_cntrl_cfgrwnx_event_get(drv->link, &event, sizeof(event)) < 0)
	//	return;

	tx_frame = (struct wpa_rwnx_tx_frame *)event->data;

	if (tx_frame->eapol)
	{
		data.eapol_tx_status.dst = tx_frame->dst_addr;
		data.eapol_tx_status.data = tx_frame->data;
		data.eapol_tx_status.data_len = tx_frame->data_len;
		data.eapol_tx_status.ack = event->acknowledged;
		wpa_event = EVENT_EAPOL_TX_STATUS;
	}
	else
	{
		data.tx_status.type = WLAN_FC_GET_TYPE(tx_frame->data[0]);
		data.tx_status.stype = WLAN_FC_GET_STYPE(tx_frame->data[0]);
		data.tx_status.dst = ((struct ieee80211_hdr *)tx_frame->data)->addr1;
		data.tx_status.data = tx_frame->data;
		data.tx_status.data_len = tx_frame->data_len;
		data.tx_status.ack = event->acknowledged;
		wpa_event = EVENT_TX_STATUS;
	}

	// Release before calling wpa_supplicant_event which may lead to get a new tx_frame
	data_buf = tx_frame->data;
	tx_frame->data = NULL;
	wpa_supplicant_event(drv->ctx, wpa_event, &data);
	os_free(data_buf);
}
#endif
void wpa_rwnx_driver_tx_status(uint32_t frame_id, bool acknowledged, void *arg)
{
	struct wpa_rwnx_tx_frame *tx_frame = arg;
	struct wpa_rwnx_driver_data *drv = tx_frame->drv;

	rtos_semaphore_signal(drv->tx_frame_sem, false);
	if (drv->status & RWNX_EXITING) {
		os_free(tx_frame->data);
		tx_frame->data = NULL;
		return;
	}

    #if 1
	struct wpa_rwnx_driver_msg *event;

    event = rtos_malloc(sizeof(struct wpa_rwnx_driver_msg));
	event->id = CFGRWNX_TX_STATUS_EVENT;
	event->msg.tx_status_event.data = (uint8_t *)tx_frame;
	event->msg.tx_status_event.acknowledged = acknowledged;

	if (wpa_rwnx_driver_send(&event)) {
		os_free(tx_frame->data);
		tx_frame->data = NULL;

        rtos_free(event);
        event = NULL;
	}

    #else
	struct cfgrwnx_tx_status_event event;
	// Remember this callback is called in the WIFI task context, so we cannot call
	// wpa_supplicant_event as this may call another driver interface.
	// Instead defer its processing by sending an event to the wpa_supplicant task.
	event.hdr.id = CFGRWNX_TX_STATUS_EVENT;
	event.hdr.len = sizeof(event);
	event.data = (uint8_t *)tx_frame;
	event.acknowledged = acknowledged;

	if (fhost_cntrl_cfgrwnx_event_send(&event.hdr, drv->link->sock_send))
	{
		os_free(tx_frame->data);
		tx_frame->data = NULL;
	}
    #endif
}

static void wpa_rwnx_driver_tx_cfm_callback(uint32_t frame_id, bool acknowledged, void *arg)
{
    do {
        tx_cfm_callback_t *tx_cfm = os_malloc(sizeof(tx_cfm_callback_t));
        if (!tx_cfm) {
            break;
        }
        tx_cfm->cb_func = wpa_rwnx_driver_tx_status;
        tx_cfm->frame_id = frame_id;
        tx_cfm->acknowledged = acknowledged;
        tx_cfm->arg = arg;
        if (fhost_cntrl_txcfm_cb_msg_send(tx_cfm)) {
            break;
        }
        return;
    } while (0);
    TRACE_FHOST("[WPA] cfm cb err\r\n");
    wpa_rwnx_driver_tx_status(frame_id, acknowledged, arg);
}

static void wpa_rwnx_driver_process_ch_switch(struct wpa_rwnx_driver_data *drv)
{
	union wpa_event_data data = {0};
	struct cfgrwnx_ch_switch_event event;

	if (fhost_cntrl_cfgrwnx_event_get(drv->link, &event, sizeof(event)) < 0)
		return;

	data.ch_switch.freq = event.freq;
	data.ch_switch.ht_enabled = event.ht_enabled;
	data.ch_switch.ch_offset = event.ch_offset;
	data.ch_switch.ch_width = event.ch_width;
	data.ch_switch.cf1 = event.cf1;
	data.ch_switch.cf2 = event.cf2;
	wpa_supplicant_event(drv->ctx, EVENT_CH_SWITCH, &data);
}

static void wpa_rwnx_driver_cancel_roc_event(struct wpa_rwnx_driver_data *drv, int cancel_event)
{
	union wpa_event_data data;
	struct cfgrwnx_roc_event event;

	if (fhost_cntrl_cfgrwnx_event_get(drv->link, &event, sizeof(event)) < 0)
		return;

	memset(&data, 0, sizeof(union wpa_event_data));
	data.remain_on_channel.freq = event.center_freq;
	data.remain_on_channel.duration = event.duration;

	if (cancel_event)
		drv->pending_remain_on_chan = 0;

	wpa_supplicant_event(drv->ctx, cancel_event ?
						EVENT_CANCEL_REMAIN_ON_CHANNEL :
						EVENT_REMAIN_ON_CHANNEL, &data);
}
#ifdef CONFIG_RWNX_RADAR
static void wpa_rwnx_driver_dfs_cac_finished_event(struct wpa_rwnx_driver_data *drv)
{
	union wpa_event_data data;
	struct cfgrwnx_dfs_event event;

	if (fhost_cntrl_cfgrwnx_event_get(drv->link, &event, sizeof(event)) < 0)
		return;

	memset(&data, 0, sizeof(union wpa_event_data));
	data.dfs_event.freq = event.center_freq;
	data.dfs_event.ht_enabled = event.ht_enabled;
	data.dfs_event.chan_offset = event.chan_offset;
	data.dfs_event.chan_width = event.chan_width;
	data.dfs_event.cf1 = event.cf1;
	data.dfs_event.cf2 = event.cf2;

	wpa_supplicant_event(drv->ctx, EVENT_DFS_CAC_FINISHED, &data);
}

static void wpa_rwnx_driver_dfs_cac_aborted_event(struct wpa_rwnx_driver_data *drv)
{
	union wpa_event_data data;
	struct cfgrwnx_dfs_event event;

	if (fhost_cntrl_cfgrwnx_event_get(drv->link, &event, sizeof(event)) < 0)
		return;

	memset(&data, 0, sizeof(union wpa_event_data));
	data.dfs_event.freq = event.center_freq;
	//data.dfs_event.duration = event.duration;

	wpa_supplicant_event(drv->ctx, EVENT_DFS_CAC_ABORTED, &data);
}

static void wpa_rwnx_driver_dfs_radar_detected_event(struct wpa_rwnx_driver_data *drv)
{
	union wpa_event_data data;
	struct cfgrwnx_dfs_event event;

	if (fhost_cntrl_cfgrwnx_event_get(drv->link, &event, sizeof(event)) < 0)
		return;

	memset(&data, 0, sizeof(union wpa_event_data));
	data.dfs_event.freq = event.center_freq;
	data.dfs_event.chan_width = event.chan_width;
	data.dfs_event.chan_offset = event.chan_offset;
	data.dfs_event.ht_enabled = event.ht_enabled;

	wpa_supplicant_event(drv->ctx, EVENT_DFS_RADAR_DETECTED, &data);
}
#endif
/******************************************************************************
 * Send / Receive functions
 *****************************************************************************/
static void wpa_rwnx_driver_event(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct wpa_rwnx_driver_data *drv = eloop_ctx;
	struct cfgrwnx_msg_hdr msg_hdr;
	//void *msg_payload;

	if (fhost_cntrl_cfgrwnx_event_peek_header(drv->link, &msg_hdr) < 0)
		return;

	switch (msg_hdr.id) {
	case CFGRWNX_SCAN_RESULT_EVENT:
		wpa_rwnx_driver_process_scan_result(drv);
		break;
	case CFGRWNX_SCAN_DONE_EVENT:
		fhost_cntrl_cfgrwnx_event_discard(drv->link, &msg_hdr);
		wpa_supplicant_event(drv->ctx, EVENT_SCAN_RESULTS, NULL);
		break;
	case CFGRWNX_CONNECT_EVENT:
		wpa_rwnx_driver_process_connect_event(drv);
		break;
	case CFGRWNX_DISCONNECT_EVENT:
		wpa_rwnx_driver_process_disconnect_event(drv);
		break;
	case CFGRWNX_MIC_FAILURE_EVENT:
		wpa_rwnx_driver_process_mic_failure_event(drv);
		break;
	//case CFGRWNX_RX_MGMT_EVENT:
	//	wpa_rwnx_driver_process_rx_deauth_event(drv);
		break;
	case CFGRWNX_EXTERNAL_AUTH_EVENT:
		wpa_rwnx_driver_process_external_auth_event(drv);
		break;
	//case CFGRWNX_TX_STATUS_EVENT:
	//	wpa_rwnx_driver_process_tx_status_event(drv);
	//	break;
	case CFGRWNX_CH_SWITCH_EVENT:
		wpa_rwnx_driver_process_ch_switch(drv);
		break;
	case CFGRWNX_REMAIN_ON_CHANNEL_EVENT:
		wpa_rwnx_driver_cancel_roc_event(drv, 0);
		break;
	case CFGRWNX_CANCEL_REMAIN_ON_CHANNEL_EVENT:
		wpa_rwnx_driver_cancel_roc_event(drv, 1);
		break;
	#ifdef CONFIG_RWNX_RADAR
	case CFGRWNX_DFS_CAC_FINISHED_EVENT:
		wpa_rwnx_driver_dfs_cac_finished_event(drv);
		break;
	case CFGRWNX_DFS_CAC_ABORTED_EVENT:
		wpa_rwnx_driver_dfs_cac_aborted_event(drv);
		break;
	case CFGRWNX_DFS_RADAR_DETECTED_EVENT:
		wpa_rwnx_driver_dfs_radar_detected_event(drv);
		break;
	#endif /* CONFIG_RWNX_RADAR */
	default:
		fhost_cntrl_cfgrwnx_event_discard(drv->link, &msg_hdr);
		break;
	}
}
char format_cmd[128];
char *fhost_wpa_format(const char *fmt, ...)
{
    va_list args;
    unsigned int cmd_len = 0;
    // Format command
    va_start(args, fmt);
    cmd_len = dbg_vsnprintf(format_cmd, sizeof(format_cmd), fmt, args);
    va_end(args);

    return format_cmd;
}
static void wpa_rwnx_driver_process_disassoc_event(struct wpa_rwnx_driver_data *drv, struct cfgrwnx_disassoc_event *event)
{
	extern int ap_ctrl_iface_sta_disassociate(void *wpa_s, const char *txtaddr);

	uint8_t *mac_addr = (uint8_t *)event->addr;
    uint8_t *str = fhost_wpa_format("%02x:%02x:%02x:%02x:%02x:%02x reason=4", mac_addr[0], mac_addr[1],mac_addr[2],mac_addr[3],mac_addr[4],mac_addr[5]);

    ap_ctrl_iface_sta_disassociate(NULL, str);

    aic_dbg("Disassociate %x:%x:%x:%x:%x:%x\n", mac_addr[0], mac_addr[1],mac_addr[2],mac_addr[3],mac_addr[4],mac_addr[5]);

    return;
}

static void wpa_rwnx_driver_process_rx_eapol_event(struct wpa_rwnx_driver_data *drv, struct cfgrwnx_rx_eapol_event *event)
{
	if (!(drv->status & RWNX_ASSOCIATED)) {
		aic_dbg("!!! EAPOL buffered status=0x%x\r\n", drv->status);
		
		if (drv->pending_eapol.payload)
			rtos_free(drv->pending_eapol.payload);
		drv->pending_eapol.payload = rtos_malloc(event->length);
		if (drv->pending_eapol.payload) {
			memcpy(drv->pending_eapol.payload, event->payload, event->length);
			memcpy(drv->pending_eapol.addr, event->addr, ETH_ALEN);
			drv->pending_eapol.length = event->length;
			drv->pending_eapol.valid = true;
		}
		rtos_free(event->payload);
		event->payload = NULL;
		return;
	}

	extern void wpa_supplicant_rx_eapol(void *ctx, const u8 *src_addr, const u8 *buf, size_t len);
	aic_dbg("!!! EAPOL calling wpa_supplicant_rx_eapol (direct)\r\n");
		wpa_supplicant_rx_eapol(NULL, (uint8_t*)event->addr, event->payload, event->length);
	rtos_free(event->payload);
	event->payload = NULL;
    return;
}

static void wpa_rwnx_driver_flush_pending_eapol(struct wpa_rwnx_driver_data *drv)
{
	if (drv->pending_eapol.valid && drv->pending_eapol.payload) {
		aic_dbg("!!! FLUSH pending EAPOL\r\n");
		extern void wpa_supplicant_rx_eapol(void *ctx, const u8 *src_addr, const u8 *buf, size_t len);
		wpa_supplicant_rx_eapol(NULL, drv->pending_eapol.addr,
				drv->pending_eapol.payload, drv->pending_eapol.length);
		rtos_free(drv->pending_eapol.payload);
		drv->pending_eapol.payload = NULL;
		drv->pending_eapol.valid = false;
	}
}

int drv_link_send_sock = -1;
struct wpa_rwnx_driver_data *g_drv;

/// task handle
static rtos_task_handle wpa_rwnx_driver_task_handle = NULL;
/// Master message queue
static rtos_queue wpa_rwnx_driver_queue = NULL;
/// Master message queue size
#define WPA_RWNX_DRIVER_QUEUE_SIZE (60)
static void wpa_rwnx_driver_task(void *param)
{
    struct wpa_rwnx_driver_msg *event;

    for( ;; )
    {
        rtos_queue_read(wpa_rwnx_driver_queue, &event, -1, false);
        switch (event->id)
        {
            case CFGRWNX_RX_MGMT_EVENT:
                //aic_dbg("Task wpa driver message received %d\n", event->id);
                wpa_rwnx_driver_process_rx_mgmt_event(g_drv, &event->msg.rx_mgmt_event);
                break;
            case CFGRWNX_TX_STATUS_EVENT:
                //aic_dbg("Task wpa driver message received %d\n", event->id);
                wpa_rwnx_driver_process_tx_status_event(g_drv, &event->msg.tx_status_event);
                break;
            case CFGRWNX_DISASSOC_EVENT:
                //aic_dbg("Task wpa driver message received %d\n", event->id);
                wpa_rwnx_driver_process_disassoc_event(g_drv, &event->msg.disassoc_event);
                break;
            case CFGRWNX_RX_EAPOL_EVENT:
                //aic_dbg("Task wpa driver message received %d\n", event->id);
                wpa_rwnx_driver_process_rx_eapol_event(g_drv, &event->msg.rx_eapol_event);
                break;
            default:
                aic_dbg("Task wpa driver: unsupported message received %d\n",
                     event->id);
                break;
        }
        rtos_free(event);
    }
}
int wpa_rwnx_driver_send(void *msg)
{
    int err;

    err = rtos_queue_write(wpa_rwnx_driver_queue, msg, 10, 0);

    return err;
}
/******************************************************************************
 * Drivers interface implemenation
 *****************************************************************************/
static void *wpa_rwnx_driver_init(void *ctx, const char *ifname)
{
	struct wpa_rwnx_driver_data *drv;
	//struct sockaddr_in addr;
	struct fhost_vif_status vif_status;
	int fhost_vif_idx;
	fhost_vif_idx = fhost_vif_idx_from_name(ifname);
	if (fhost_vif_idx < 0)
		return NULL;

	if (fhost_get_vif_status(fhost_vif_idx, &vif_status) ||
	    ((vif_status.type != VIF_STA) && (vif_status.type != VIF_AP)))
		return NULL;

	drv = os_zalloc(sizeof(struct wpa_rwnx_driver_data));
	if (drv == NULL)
		return NULL;

	drv->ctx = ctx;
	drv->fhost_vif_idx = fhost_vif_idx;
	drv->vif_init_type = vif_status.type;
	dl_list_init(&drv->scan_res);
	drv->status = 0;
	drv->ssid = NULL;
	drv->ssid_len = 0;

	// Open link with cntrl task to send cfgrwnx commands and retrieve events
	drv->link = fhost_cntrl_cfgrwnx_link_open();
	if (drv->link == NULL)
		goto err;

	// Semaphore for TX frame
	if (rtos_semaphore_create(&drv->tx_frame_sem, "drv->tx_frame_sem", NB_TX_FRAME, NB_TX_FRAME))
		goto err;

	// Configure default RX filters (whatever initial interface type)
	fhost_wpa_set_mgmt_rx_filter(fhost_vif_idx, STA_MGMT_RX_FILTER);

	if(eloop_register_read_sock(drv->link->sock_recv, wpa_rwnx_driver_event, drv, NULL)) {
        goto err;
    }
	drv_link_send_sock = drv->link->sock_send;
    #if 1
    extern uint32_t wpa_rwnx_driver_stack_size;
    extern uint32_t wpa_rwnx_driver_priority;

    if (rtos_queue_create(sizeof(void *), WPA_RWNX_DRIVER_QUEUE_SIZE,
                          &wpa_rwnx_driver_queue, "wpa_rwnx_driver_queue")) {
        goto err;
    }

    if (rtos_task_create(wpa_rwnx_driver_task, "wpa_rwnx_driver_task", WPA_DRIVER_TASK,
                         wpa_rwnx_driver_stack_size*3, NULL, wpa_rwnx_driver_priority,
                         &wpa_rwnx_driver_task_handle)) {
        goto err;
    }
    g_drv = drv;
    #endif
	return drv;

err:
    AIC_LOG_PRINTF("ERR: %s , Line %d\r\n", __func__, __LINE__);
	if (drv) {
		if (drv->link)
			fhost_cntrl_cfgrwnx_link_close(drv->link);
		os_free(drv);
	}

	return NULL;
}

static void wpa_rwnx_driver_deinit(void *priv)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct wpa_rwnx_driver_scan_res *cur, *next;

	//drv->status |= RWNX_EXITING;

	wpa_rwnx_driver_wait_tx_frame(drv);
	rtos_semaphore_delete(drv->tx_frame_sem);
	fhost_cntrl_cfgrwnx_link_close(drv->link);

	dl_list_for_each_safe(cur, next, &drv->scan_res,
			      struct wpa_rwnx_driver_scan_res, list) {
		dl_list_del(&cur->list);
		os_free(cur->res);
		os_free(cur);
	}
	drv_link_send_sock = -1;
	os_free(drv);

    rtos_queue_delete(wpa_rwnx_driver_queue);
    wpa_rwnx_driver_queue = NULL;
    rtos_task_delete(wpa_rwnx_driver_task_handle);
    wpa_rwnx_driver_task_handle  = NULL;
}

static int *rwnx_init_rates(int *num)
{
	int leg_rate[] = {10, 20, 55, 110, 60, 90, 120, 180, 240, 360, 480, 540};
	int *rates;

	/* Assume all legacy rates are supported */
	rates = os_malloc(sizeof(leg_rate));
	if (!rates)
		return NULL;

	os_memcpy(rates, leg_rate, sizeof(leg_rate));
	*num = sizeof(leg_rate) / sizeof(int);
	return rates;
}

//#ifdef USE_5G
static int *rwnx_init_rates_5g(int *num)
{
	int leg_rate[] = {60, 90, 120, 180, 240, 360, 480, 540};
	int *rates;

	/* Assume all legacy rates are supported */
	rates = os_malloc(sizeof(leg_rate));
	if (!rates)
		return NULL;

	os_memcpy(rates, leg_rate, sizeof(leg_rate));
	*num = sizeof(leg_rate) / sizeof(int);
	return rates;
}
//#endif
static void wpa_rwnx_msg_hdr_init(struct wpa_rwnx_driver_data *drv,
				  struct cfgrwnx_msg_hdr *msg_hdr,
				  uint16_t id, uint16_t len)
{
	msg_hdr->len        = len;
	msg_hdr->id         = id;
	msg_hdr->resp_queue = drv->link->queue;
}

#ifdef CFG_SOFTAP
static uint8_t *wpa_rwnx_build_bcn(struct wpa_driver_ap_params *params, int *bcn_len,
				   int *tim_oft, int *tim_len)
{
	uint8_t *bcn_start, *bcn;

	*bcn_len = params->head_len + params->tail_len + MAC_TIM_MIN_LEN;
	bcn_start = os_malloc(*bcn_len);
	if (!bcn_start)
		return NULL;

	bcn = bcn_start;
	memcpy(bcn, params->head, params->head_len);
	bcn += params->head_len;
	// TIM element
	bcn[0] = MAC_ELTID_TIM;
	bcn[1] = MAC_TIM_MIN_LEN - 2;
	bcn[2] = 0;
	// TODO:
	if(params->dtim_period) {
		bcn[3] = (uint8_t)params->dtim_period;
	} else {
		bcn[3] = 1;
	}
	bcn[4] = 0;
	bcn[5] = 0;
	bcn += MAC_TIM_MIN_LEN;
	// TAIL
	memcpy(bcn, params->tail, params->tail_len);

	*tim_oft = params->head_len;
	*tim_len = MAC_TIM_MIN_LEN;

	return bcn_start;
}
static int wpa_rwnx_driver_update_bcn(struct wpa_rwnx_driver_data *drv,
				      struct wpa_driver_ap_params *params, uint8_t *csa_oft)
{
	struct cfgrwnx_bcn_update cmd = {0};
	struct cfgrwnx_resp resp;
	int res = 0;

	wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_BCN_UPDATE_CMD, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_BCN_UPDATE_RESP, sizeof(resp));

	cmd.fhost_vif_idx = drv->fhost_vif_idx;
	if (csa_oft) {
		int i;
		for (i = 0; i < BCN_MAX_CSA_CPT; i++) {
			cmd.csa_oft[i] = csa_oft[i];
		}
	}
	cmd.bcn = wpa_rwnx_build_bcn(params, &cmd.bcn_len, &cmd.tim_oft, &cmd.tim_len);
	if (!cmd.bcn)
		return -1;

	if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) && (resp.status != CFGRWNX_SUCCESS))
		res = -1;

	os_free(cmd.bcn);
	return res;
}
#endif

static struct hostapd_hw_modes *wpa_rwnx_driver_get_hw_feature_data(void *priv,
								    u16 *num_modes,
								    u16 *flags, u8 *dfs)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct cfgrwnx_msg cmd;
	struct cfgrwnx_hw_feature feat;
	struct hostapd_hw_modes *modes;
	struct hostapd_channel_data *chan;
	struct mac_chan_def *chan_tag;
	int i, mode_idx = 0;
	int temp_chan2G4_cnt = 0;

	wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_HW_FEATURE_CMD, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &feat.hdr, CFGRWNX_HW_FEATURE_RESP, sizeof(feat));

	*flags = 0;
	*dfs = 0;

	if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &feat.hdr))
		return NULL;

	/* Don't create mode in B */
	if ((feat.chan->chan2G4_cnt == 14) && feat.chan->chan5G_cnt) {
		modes = os_zalloc(3 * sizeof(struct hostapd_hw_modes));
		*num_modes = 3;
	}else if ((feat.chan->chan2G4_cnt == 14) || (feat.chan->chan2G4_cnt && feat.chan->chan5G_cnt)) {
		modes = os_zalloc(2 * sizeof(struct hostapd_hw_modes));
		*num_modes = 2;
	} else {
		modes = os_zalloc(sizeof(struct hostapd_hw_modes));
		*num_modes = 1;
	}
	if (!modes)
		return NULL;

	if (feat.chan->chan2G4_cnt) {
		modes[mode_idx].mode = HOSTAPD_MODE_IEEE80211G;
		temp_chan2G4_cnt = (feat.chan->chan2G4_cnt == 14) ? 13: feat.chan->chan2G4_cnt;
		modes[mode_idx].num_channels = temp_chan2G4_cnt;
		modes[mode_idx].channels = os_malloc(temp_chan2G4_cnt *
						     sizeof(struct hostapd_channel_data));
		if (!modes[mode_idx].channels)
			goto err;

		chan = modes[mode_idx].channels;
		chan_tag = feat.chan->chan2G4;
		for (i = 0 ; i < temp_chan2G4_cnt; i++, chan++, chan_tag++) {
			rwnx_to_hostapd_channel(chan_tag, chan, co_min(feat.me_config.phy_bw_max, PHY_CHNL_BW_40));
		}

		modes[mode_idx].rates = rwnx_init_rates(&modes[mode_idx].num_rates);
		if (!modes[mode_idx].rates)
			goto err;

		if (feat.me_config.ht_supp) {
			modes[mode_idx].flags |= HOSTAPD_MODE_FLAG_HT_INFO_KNOWN;
			modes[mode_idx].flags |= HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET;
			modes[mode_idx].ht_capab = feat.me_config.ht_cap.ht_capa_info;
			modes[mode_idx].a_mpdu_params = feat.me_config.ht_cap.a_mpdu_param;
			os_memcpy(modes[mode_idx].mcs_set, feat.me_config.ht_cap.mcs_rate,
				  sizeof(modes[mode_idx].mcs_set));

			if (feat.me_config.vht_supp) {
				modes[mode_idx].flags |= HOSTAPD_MODE_FLAG_VHT_INFO_KNOWN;
				modes[mode_idx].vht_capab = feat.me_config.vht_cap.vht_capa_info;
				os_memcpy(modes[mode_idx].vht_mcs_set, (u8 *)&feat.me_config.vht_cap.rx_mcs_map,
					  sizeof(modes[mode_idx].vht_mcs_set));
			}
		}
		if (feat.me_config.he_supp) {
			modes[mode_idx].he_capab[2].he_supported = 1;
			memcpy(modes[mode_idx].he_capab[2].phy_cap, feat.me_config.he_cap.phy_cap_info, MAC_HE_PHY_CAPA_LEN);
			memcpy(modes[mode_idx].he_capab[2].mac_cap, feat.me_config.he_cap.mac_cap_info, MAC_HE_MAC_CAPA_LEN);
			memcpy(modes[mode_idx].he_capab[2].mcs, &(feat.me_config.he_cap.mcs_supp), sizeof(struct mac_he_mcs_nss_supp));
			memcpy(modes[mode_idx].he_capab[2].ppet, feat.me_config.he_cap.ppe_thres, MAC_HE_PPE_THRES_MAX_LEN);
		}
		mode_idx++;
	}

	if (feat.chan->chan2G4_cnt == 14) {
		modes[mode_idx].mode = HOSTAPD_MODE_IEEE80211B;
		modes[mode_idx].num_channels = 1;
		modes[mode_idx].channels = os_malloc(sizeof(struct hostapd_channel_data));
		if (!modes[mode_idx].channels)
			goto err;

		chan = modes[mode_idx].channels;
		chan_tag = &(feat.chan->chan2G4[MAC_DOMAINCHANNEL_24G_MAX - 1]);
		rwnx_to_hostapd_channel(chan_tag, chan, PHY_CHNL_BW_20);

		modes[mode_idx].rates = rwnx_init_rates(&modes[mode_idx].num_rates);
		if (!modes[mode_idx].rates)
			goto err;

		if (feat.me_config.ht_supp) {
			modes[mode_idx].flags |= HOSTAPD_MODE_FLAG_HT_INFO_KNOWN;
			modes[mode_idx].ht_capab = feat.me_config.ht_cap.ht_capa_info;
			modes[mode_idx].a_mpdu_params = feat.me_config.ht_cap.a_mpdu_param;
			os_memcpy(modes[mode_idx].mcs_set, feat.me_config.ht_cap.mcs_rate,
				  sizeof(modes[mode_idx].mcs_set));
		}
		mode_idx++;
	}

//#ifdef USE_5G
	if (feat.chan->chan5G_cnt) {
		modes[mode_idx].mode = HOSTAPD_MODE_IEEE80211A;
		modes[mode_idx].num_channels = feat.chan->chan5G_cnt;
		modes[mode_idx].channels = os_malloc(feat.chan->chan5G_cnt *
						     sizeof(struct hostapd_channel_data));
		if (!modes[mode_idx].channels)
			goto err;

		chan = modes[mode_idx].channels;
		chan_tag = feat.chan->chan5G;
		for (i = 0 ; i < feat.chan->chan5G_cnt ; i++, chan++, chan_tag++) {
			rwnx_to_hostapd_channel(chan_tag, chan, feat.me_config.phy_bw_max);
		}

		modes[mode_idx].rates = rwnx_init_rates_5g(&modes[mode_idx].num_rates);
		if (!modes[mode_idx].rates)
			goto err;

		if (feat.me_config.ht_supp) {
			modes[mode_idx].flags |= HOSTAPD_MODE_FLAG_HT_INFO_KNOWN;
			modes[mode_idx].ht_capab = feat.me_config.ht_cap.ht_capa_info;
			modes[mode_idx].a_mpdu_params = feat.me_config.ht_cap.a_mpdu_param;
			os_memcpy(modes[mode_idx].mcs_set, feat.me_config.ht_cap.mcs_rate,
				  sizeof(modes[mode_idx].mcs_set));

			if (feat.me_config.vht_supp) {
				modes[mode_idx].flags |= HOSTAPD_MODE_FLAG_VHT_INFO_KNOWN;
				modes[mode_idx].vht_capab = feat.me_config.vht_cap.vht_capa_info;
				os_memcpy(modes[mode_idx].vht_mcs_set, (u8 *)&feat.me_config.vht_cap.rx_mcs_map,
					  sizeof(modes[mode_idx].vht_mcs_set));
			}
		}
		if (feat.me_config.he_supp) {
			modes[mode_idx].he_capab[2].he_supported = 1;
			memcpy(modes[mode_idx].he_capab[2].phy_cap, feat.me_config.he_cap.phy_cap_info, MAC_HE_PHY_CAPA_LEN);
			memcpy(modes[mode_idx].he_capab[2].mac_cap, feat.me_config.he_cap.mac_cap_info, MAC_HE_MAC_CAPA_LEN);
			memcpy(modes[mode_idx].he_capab[2].mcs, &(feat.me_config.he_cap.mcs_supp), sizeof(struct mac_he_mcs_nss_supp));
			memcpy(modes[mode_idx].he_capab[2].ppet, feat.me_config.he_cap.ppe_thres, MAC_HE_PPE_THRES_MAX_LEN);
		}
		mode_idx++;
	}
//#endif
	return modes;

err:
	for (i = 0 ; i < *num_modes; i++) {
		if (modes[i].channels)
			os_free(modes[i].channels);
		if (modes[i].rates)
			os_free(modes[i].rates);
	}

	os_free(modes);
	return NULL;
}

static int wpa_rwnx_driver_get_capa(void *priv, struct wpa_driver_capa *capa)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct cfgrwnx_msg cmd;
	struct cfgrwnx_msg resp;

	wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_GET_CAPA_CMD, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_GET_CAPA_RESP, sizeof(resp));

	os_memset(capa, 0, sizeof(*capa));

	capa->key_mgmt = WPA_DRIVER_CAPA_KEY_MGMT_WPA |
			 WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK |
			 WPA_DRIVER_CAPA_KEY_MGMT_WPA2 |
			 WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK |
			 WPA_DRIVER_CAPA_KEY_MGMT_SUITE_B |
			 WPA_DRIVER_CAPA_KEY_MGMT_SUITE_B_192;
	capa->enc = WPA_DRIVER_CAPA_ENC_WEP40 |
		    WPA_DRIVER_CAPA_ENC_WEP104 |
		    WPA_DRIVER_CAPA_ENC_TKIP |
#if NX_MFP
		    WPA_DRIVER_CAPA_ENC_BIP |
#endif
		    WPA_DRIVER_CAPA_ENC_CCMP;

        capa->auth = WPA_DRIVER_AUTH_OPEN |
                     WPA_DRIVER_AUTH_SHARED |
                     WPA_DRIVER_AUTH_LEAP;

	capa->flags = WPA_DRIVER_FLAGS_SET_KEYS_AFTER_ASSOC_DONE |
		      WPA_DRIVER_FLAGS_HT_2040_COEX |
		      WPA_DRIVER_FLAGS_SANE_ERROR_CODES;

#if NX_BEACONING
	capa->flags |= WPA_DRIVER_FLAGS_AP |
		       WPA_DRIVER_FLAGS_EAPOL_TX_STATUS |
		       WPA_DRIVER_FLAGS_DEAUTH_TX_STATUS |
		       WPA_DRIVER_FLAGS_AP_MLME |
		       WPA_DRIVER_FLAGS_AP_UAPSD |
			   #ifdef CONFIG_RWNX_RADAR
		       WPA_DRIVER_FLAGS_AP_CSA |
			   WPA_DRIVER_FLAGS_RADAR ;
			   #else
			   WPA_DRIVER_FLAGS_AP_CSA;
			   #endif
#endif
#if NX_CRYPTOLIB
	capa->flags |= WPA_DRIVER_FLAGS_SAE;
#endif
#if NX_P2P
	capa->flags |= WPA_DRIVER_FLAGS_P2P_CONCURRENT;
	capa->flags |= WPA_DRIVER_FLAGS_P2P_CAPABLE;
#ifdef CONFIG_OFFCHANNEL
	capa->flags |= WPA_DRIVER_FLAGS_OFFCHANNEL_TX;
#endif
#endif
#if NX_TDLS
	capa->flags |= WPA_DRIVER_FLAGS_TDLS_SUPPORT;
#endif
#if RW_MESH_EN
	capa->flags |= WPA_DRIVER_FLAGS_MESH;
#endif

	capa->wmm_ac_supported = 0;
	//capa->mac_addr_rand_scan_supported = 0;
	//capa->mac_addr_rand_sched_scan_supported = 0;
	capa->max_scan_ssids = 1;
	//capa->max_sched_scan_ssids = 0;
	//capa->max_sched_scan_plans = 0;
	//capa->max_sched_scan_plan_interval = 0;
	//capa->max_sched_scan_plan_iterations = 0;
	//capa->sched_scan_supported = 0;
	//capa->max_match_sets = 0;
	//capa->max_remain_on_chan = 100;
	capa->max_stations = NX_REMOTE_STA_MAX;
	//capa->probe_resp_offloads;
	//capa->max_acl_mac_addrs;
	capa->num_multichan_concurrent = 2;
	//capa->extended_capa = NULL;
	//capa->extended_capa_mask = NULL;
	//capa->extended_capa_len = 0;
	//capa->wowlan_triggers;
	//capa->rrm_flags = 0;
	//capa->conc_capab = 0;
	//capa->max_conc_chan_2_4 = 0;
	//capa->max_conc_chan_5_0 = 0;
	capa->max_csa_counters = 2;

	fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr);
	return 0;
}

static int wpa_rwnx_driver_set_key(const char *ifname, void *priv, enum wpa_alg alg,
				   const u8 *addr, int key_idx, int set_tx,
				   const u8 *seq, size_t seq_len,
				   const u8 *key, size_t key_len)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct cfgrwnx_set_key cmd;
	struct cfgrwnx_resp resp;

	wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_SET_KEY_CMD, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_SET_KEY_RESP, sizeof(resp));

	cmd.fhost_vif_idx = drv->fhost_vif_idx;
	cmd.addr = (const struct mac_addr *)addr;
	if (alg == WPA_ALG_NONE) {
		cmd.cipher_suite = MAC_CIPHER_INVALID;
	} else {
		cmd.cipher_suite = hostapd_to_rwnx_cipher(alg, key_len);
		if (cmd.cipher_suite == MAC_CIPHER_INVALID)
			return -1;
	}
	cmd.key_idx = key_idx;
	cmd.key = key;
	cmd.key_len = key_len;
	cmd.seq = seq;
	cmd.seq_len = seq_len;

	if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) || (resp.status != CFGRWNX_SUCCESS))
		return -1;

	return 0;
}

static int wpa_rwnx_driver_scan2(void *priv, struct wpa_driver_scan_params *params)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct cfgrwnx_scan cmd;
	struct cfgrwnx_resp resp;

	wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_SCAN_CMD, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_SCAN_RESP, sizeof(resp));

	if (params->num_ssids > SCAN_SSID_MAX)
		return -1;

	cmd.fhost_vif_idx = drv->fhost_vif_idx;
	cmd.ssids = (struct cfgrwnx_scan_ssid *)params->ssids;
	cmd.ssid_cnt = (int)params->num_ssids;
	cmd.extra_ies = params->extra_ies;
	cmd.extra_ies_len = (int)params->extra_ies_len;
	cmd.freqs = params->freqs;
	cmd.no_cck = (bool)params->p2p_probe;
	cmd.bssid = params->bssid;
	cmd.sock = drv->link->sock_send;

	if (params->filter_ssids) {
		memcpy(drv->filter_ssid, params->filter_ssids->ssid, params->filter_ssids->ssid_len);
		drv->filter_ssid_len = params->filter_ssids->ssid_len;
	}

	if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) || (resp.status != CFGRWNX_SUCCESS))
		return -1;

	return 0;
}

static struct wpa_scan_results * wpa_rwnx_driver_get_scan_results2(void *priv)
{
	struct wpa_rwnx_driver_data *drv = priv;
	int nb_res = dl_list_len(&drv->scan_res);
	struct wpa_scan_results *res;
	struct wpa_rwnx_driver_scan_res *cur, *next;
	int i = 0;

	if (!nb_res) {
		return NULL;
	}

	res = os_malloc(sizeof(struct wpa_scan_results));
	if (!res) {
		return NULL;
	}

	res->res = os_malloc(sizeof(struct wpa_scan_res *) * nb_res);
	if (!res->res) {
		os_free(res);
		return NULL;
	}

	res->num = nb_res;
	dl_list_for_each_safe(cur, next, &drv->scan_res,
			      struct wpa_rwnx_driver_scan_res, list) {
		if (!cur->res->ie_len) {
			cur->res->ie_len = cur->res->beacon_ie_len;
			cur->res->beacon_ie_len = 0;
		}
		res->res[i] = cur->res;
		dl_list_del(&cur->list);
		os_free(cur);
		i++;
	}

	os_get_reltime(&res->fetch_time);

	return res;
}

static int wpa_rwnx_driver_associate_ap(struct wpa_rwnx_driver_data *drv,
					struct wpa_driver_associate_params *params)
{
	// struct cfgrwnx_set_vif_type cmd;
	// struct cfgrwnx_resp resp;

	// // Simply change interface type to AP
	// wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_SET_VIF_TYPE_CMD, sizeof(cmd));
	// wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_SET_VIF_TYPE_RESP, sizeof(resp));

	if (params->uapsd == -1)
		params->uapsd = 1;

	// cmd.fhost_vif_idx = drv->fhost_vif_idx;
	// cmd.type = VIF_AP;
	// cmd.p2p = params->p2p;

	// if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) || (resp.status != CFGRWNX_SUCCESS))
	// 	return -1;

	fhost_wpa_set_mgmt_rx_filter(drv->fhost_vif_idx, AP_MGMT_RX_FILTER);

	return 0;
}

static int wpa_rwnx_driver_associate(void *priv,
				     struct wpa_driver_associate_params *params)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct cfgrwnx_connect cmd;
	struct cfgrwnx_resp resp;
	if (params->mode == IEEE80211_MODE_AP)
		return wpa_rwnx_driver_associate_ap(priv, params);

	wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_CONNECT_CMD, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_CONNECT_RESP, sizeof(resp));

	if (!params->bssid)
		return -1;
	cmd.bssid = params->bssid;
	cmd.ssid.ssid = params->ssid;
	cmd.ssid.len = params->ssid_len;

	cmd.chan.freq = params->freq.freq;
	if (params->freq.freq < 5000)
		cmd.chan.band = PHY_BAND_2G4;
	else
		cmd.chan.band = PHY_BAND_5G;
	cmd.chan.flags = 0;
	cmd.chan.tx_power = 20;
	cmd.flags = CONTROL_PORT_HOST;
	if ((params->pairwise_suite == WPA_CIPHER_WEP40) ||
	    (params->pairwise_suite == WPA_CIPHER_TKIP) ||
	    (params->pairwise_suite == WPA_CIPHER_WEP104))
		cmd.flags |= DISABLE_HT;
	if (params->wpa_proto)
		cmd.flags |= WPA_WPA2_IN_USE;
	if (params->key_mgmt_suite == WPA_KEY_MGMT_IEEE8021X_NO_WPA &&
	    (params->pairwise_suite == WPA_CIPHER_NONE ||
	     params->pairwise_suite == WPA_CIPHER_WEP104 ||
	     params->pairwise_suite == WPA_CIPHER_WEP40))
		cmd.flags |= CONTROL_PORT_NO_ENC;

	if (params->mgmt_frame_protection == MGMT_FRAME_PROTECTION_REQUIRED)
		cmd.flags |= MFP_IN_USE;

	cmd.ctrl_port_ethertype = htons(ETH_P_PAE);

	// Only consider authentication algo that are supported
	params->auth_alg &= (WPA_AUTH_ALG_OPEN | WPA_AUTH_ALG_SHARED |
			     WPA_AUTH_ALG_FT | WPA_AUTH_ALG_SAE);

	if (params->auth_alg == 0)
		return -1;

	cmd.auth_alg = hostapd_to_rwnx_auth_alg(params->auth_alg);
	if (cmd.auth_alg == MAC_AUTH_ALGO_INVALID)  {
		// Multiple Authentication algos (as we already filter out unsupported algo).
		int auth_alg;

		if (drv->next_auth_alg & params->auth_alg)
			params->auth_alg &= drv->next_auth_alg;
		else
			drv->next_auth_alg = params->auth_alg;

		// drv->next_auth_alg contains the list of auth algs. Try with
		// the first one (i.e. with the MSB) and if it is not supported
		// it will be removed in wpa_rwnx_driver_process_connect_event
		auth_alg = (1 << (31 - co_clz(params->auth_alg)));
		cmd.auth_alg = hostapd_to_rwnx_auth_alg(auth_alg);
	}

	cmd.fhost_vif_idx = drv->fhost_vif_idx;

	/* for now only support station role */
	if (params->mode != IEEE80211_MODE_INFRA)
		return -1;
	cmd.uapsd = params->uapsd;

	cmd.ie = params->wpa_ie;
	cmd.ie_len = params->wpa_ie_len;

	cmd.sock = drv->link->sock_send;

	if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) || (resp.status != CFGRWNX_SUCCESS))
		return -1;

	return 0;
}

static int wpa_rwnx_driver_get_bssid(void *priv, u8 *bssid)
{
	struct wpa_rwnx_driver_data *drv = priv;

	if (drv->status & RWNX_ASSOCIATED) {
		memcpy(bssid, drv->bssid, ETH_ALEN);
	} else {
		memset(bssid, 0, ETH_ALEN);
	}

	return 0;
}

static int wpa_rwnx_driver_get_ssid(void *priv, u8 *ssid)
{
	struct wpa_rwnx_driver_data *drv = priv;
	int ret = 0;

	if (drv->status & RWNX_ASSOCIATED) {
		if (drv->ssid) {
			memcpy(ssid, drv->ssid, drv->ssid_len);
			ret = drv->ssid_len;
		} else {
			ret = -1;
		}
	}

	return ret;
}

static int wpa_rwnx_driver_set_supp_port(void *priv, int authorized)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct cfgrwnx_ctrl_port cmd;
	struct cfgrwnx_resp resp;

	if (!(drv->status & RWNX_ASSOCIATED))
		return 0;

	wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_CTRL_PORT_CMD, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_CTRL_PORT_RESP, sizeof(resp));

	cmd.fhost_vif_idx = drv->fhost_vif_idx;
	cmd.authorized = authorized;

	if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) || (resp.status != CFGRWNX_SUCCESS))
		return -1;

	if (drv->status & RWNX_COMPLETED) {

        if (fhost_reconnect_dhcp_callback)
            fhost_reconnect_dhcp_callback(WIFI_MAC_STATUS_CONNECTED);

        if (fhost_mac_status_get_callback)
            fhost_mac_status_get_callback(WIFI_MAC_STATUS_CONNECTED);
    }
	return 0;
}

static int wpa_rwnx_driver_deauthenticate(void *priv, const u8 *addr, u16 reason_code)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct cfgrwnx_disconnect cmd;
	struct cfgrwnx_resp resp;
	if (!(drv->status & RWNX_ASSOCIATED))
		return -1;

	if (memcmp(addr, drv->bssid, ETH_ALEN))
		return -1;

	wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_DISCONNECT_CMD, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_DISCONNECT_RESP, sizeof(resp));

	cmd.fhost_vif_idx = drv->fhost_vif_idx;
	cmd.reason_code = reason_code;

	if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) || (resp.status != CFGRWNX_SUCCESS))
		return -1;

	drv->status |= RWNX_DISASSOC_PENDING;

	return 0;
}

static int wpa_rwnx_driver_set_operstate(void *priv, int state)
{
	struct wpa_rwnx_driver_data *drv = priv;


	if (state == 1) {
		drv->status |= RWNX_COMPLETED;
		drv->next_auth_alg = 0;
		fhost_wpa_send_event(FHOST_WPA_CONNECTED, NULL, 0, drv->fhost_vif_idx);
	} else if (drv->status & RWNX_COMPLETED) {
		// set_operstate is called with state = 0 when wpa state machine
		// enters WPA_ASSOCIATING, WPA_ASSOCIATED or WPA_DISCONNECTED
		// We just want to send disconnected when WPA_DISCONNECTED state is entered
		// (i.e. when WPA_COMPLETED was first entered)
		drv->status &= ~RWNX_COMPLETED;
		fhost_wpa_send_event(FHOST_WPA_DISCONNECTED, NULL, 0, drv->fhost_vif_idx);
	}

	return 0;
}

extern struct rwnx_hw *cntrl_rwnx_hw;

static int wpa_rwnx_driver_send_mlme(void *priv, const u8 *data, size_t data_len,
				     int noack, unsigned int freq, const u16 *csa_offs,
				     size_t csa_offs_len)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct wpa_rwnx_tx_frame *tx_frame = NULL;
	cb_fhost_tx cb = NULL;

	if (freq || csa_offs_len) {
		TRACE_FHOST("[WPA] TODO: support freq/csa_offs_len in send_mlme, %d\r\n", csa_offs_len);
	}

	if (!noack) {
		tx_frame = wpa_rwnx_driver_init_tx_frame(drv, data, data_len, NULL);
		if (!tx_frame){
			TRACE_FHOST("[WPA] send_mlme tx_frame null\r\n");
			return -1;
		}
		//cb = wpa_rwnx_driver_tx_status;
		cb = wpa_rwnx_driver_tx_cfm_callback;
	}
	#if 0
	/* Check that a RoC is already pending */
    if (cntrl_rwnx_hw->roc_elem) {
		if (!cntrl_rwnx_hw->roc_elem->chan->center_freq || (drv->fhost_vif_idx != 3)) {
			printk("mgmt rx chan invalid: %d, %d", cntrl_rwnx_hw->roc_elem->chan->center_freq);
			return -EINVAL;
		}
	} else {
		int error;
		printk("mgmt rx remain on chan\n");
		/* Start a ROC procedure for 30ms */
		error = wpa_rwnx_driver_remain_on_channel(priv, freq, 30);
		if (error) {
			printk("mgmt rx chan err\n");
			return error;
		}
		/* Need to keep in mind that RoC has been launched internally in order to
		 * avoid to call the cfg80211 callback once expired */
		cntrl_rwnx_hw->roc_elem->mgmt_roc = true;
	}
	#endif
	if (fhost_send_80211_frame(drv->fhost_vif_idx, data, data_len, cb, tx_frame) == 0) {
		TRACE_FHOST("[WPA] send_mlme fail\r\n");
		return -1;
	}
	return 0;
}

static int wpa_rwnx_driver_send_external_auth_status(void *priv,
						     struct external_auth *params)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct cfgrwnx_external_auth_status resp;

	if (drv->status & RWNX_AP_STARTED) {
		// Now that SAE processing is done we can re-start processing probe request
		fhost_wpa_set_mgmt_rx_filter(drv->fhost_vif_idx, AP_MGMT_RX_FILTER);
		return 0;
	}

	fhost_wpa_set_mgmt_rx_filter(drv->fhost_vif_idx, STA_MGMT_RX_FILTER);
	wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_EXTERNAL_AUTH_STATUS_RESP, sizeof(resp));
	resp.fhost_vif_idx = drv->fhost_vif_idx;
	resp.status = params->status;

	if (fhost_cntrl_cfgrwnx_cmd_send(&resp.hdr, NULL))
		return -1;

	return 0;
}

#ifdef CFG_SOFTAP
static int wpa_rwnx_driver_set_ap(void *priv, struct wpa_driver_ap_params *params)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct cfgrwnx_start_ap cmd;
	struct cfgrwnx_resp resp;
	int res = -1;

	if (drv->status & RWNX_AP_STARTED)
		return wpa_rwnx_driver_update_bcn(drv, params, NULL);

	memset(&cmd, 0, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_START_AP_CMD, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_START_AP_RESP, sizeof(resp));
	drv->pairwise_ciphers = params->pairwise_ciphers;
	drv->key_mgmt_suites = (u32)params->key_mgmt_suites;
	drv->sta_wpa3_active = 0;
	cmd.fhost_vif_idx = drv->fhost_vif_idx;
	cmd.basic_rates.length = 0;
	if (params->basic_rates) {
		int i = 0;
		while (params->basic_rates[i] != -1) {
			cmd.basic_rates.array[i] = ((uint8_t)(params->basic_rates[i] / 5) |
						    MAC_BASIC_RATE);
			i++;
		}
		cmd.basic_rates.length = i;
	}
	hostapd_to_rwnx_op_channel(params->freq, &cmd.chan);

	cmd.bcn = wpa_rwnx_build_bcn(params, &cmd.bcn_len, &cmd.tim_oft, &cmd.tim_len);
	if (!cmd.bcn)
		return -1;

	cmd.bcn_int = params->beacon_int;
	cmd.flags = CONTROL_PORT_HOST;
	if (params->key_mgmt_suites & WPA_KEY_MGMT_IEEE8021X_NO_WPA &&
	    (!params->pairwise_ciphers ||
	     params->pairwise_ciphers & (WPA_CIPHER_WEP104 | WPA_CIPHER_WEP40)))
		cmd.flags |= CONTROL_PORT_NO_ENC;
	if (params->wpa_version)
		cmd.flags |= WPA_WPA2_IN_USE;
	cmd.ctrl_ethertype = htons(ETH_P_PAE);
	cmd.sock = drv->link->sock_send;

	if (!fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) &&
	    (resp.status == CFGRWNX_SUCCESS)) {
		res = 0;
		drv->status |= RWNX_AP_STARTED;
	}

	os_free(cmd.bcn);
	return res;
}

static int wpa_rwnx_driver_stop_ap(void *priv)
{
	struct wpa_rwnx_driver_data *drv = priv;
	drv->status |= RWNX_EXITING;

	fhost_wpa_send_event(FHOST_WPA_DISCONNECTED, NULL, 0, drv->fhost_vif_idx);

	return 0;
}

static int wpa_rwnx_driver_deinit_ap(void *priv)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct cfgrwnx_resp resp;
	struct cfgrwnx_set_vif_type cmd;

	// Always reset to STA filter whatever initial interface type
	fhost_wpa_set_mgmt_rx_filter(drv->fhost_vif_idx, STA_MGMT_RX_FILTER);

	if (drv->status & RWNX_AP_STARTED) {
		struct cfgrwnx_stop_ap stop;
		drv->status &= ~RWNX_AP_STARTED;
		wpa_rwnx_msg_hdr_init(drv, &stop.hdr, CFGRWNX_STOP_AP_CMD, sizeof(stop));
		wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_STOP_AP_RESP, sizeof(resp));
		stop.fhost_vif_idx = drv->fhost_vif_idx;

		wpa_rwnx_driver_wait_tx_frame(drv);
		if (fhost_cntrl_cfgrwnx_cmd_send(&stop.hdr, &resp.hdr) ||
		    (resp.status != CFGRWNX_SUCCESS))
			return -1;
	}

	// switch back to initial interface type
	wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_SET_VIF_TYPE_CMD, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_SET_VIF_TYPE_RESP, sizeof(resp));
	cmd.fhost_vif_idx = drv->fhost_vif_idx;
	cmd.type = VIF_UNKNOWN;//drv->vif_init_type;
	cmd.p2p = false;

	if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) || (resp.status != CFGRWNX_SUCCESS))
		return -1;

	return 0;
}

static void wpa_rwnx_driver_ch_switch(struct wpa_rwnx_driver_data *drv, struct hostapd_freq_params *frequency)
{
	struct cfgrwnx_ch_switch_event event;

	event.hdr.id  = CFGRWNX_CH_SWITCH_EVENT;
	event.hdr.len = sizeof(event);
    event.freq    = frequency->freq;
    event.ht_enabled    = frequency->ht_enabled;
    event.ch_offset    = frequency->sec_channel_offset;
    event.ch_width    = frequency->bandwidth;
    event.cf1    = frequency->center_freq1;
    event.cf2    = frequency->center_freq2;

	if (fhost_cntrl_cfgrwnx_event_send(&event.hdr, drv->link->sock_send))
	{
	    AIC_LOG_PRINTF("CFGRWNX_CH_SWITCH_STARTED_EVENT fail\r\n");
	}
}

static int wpa_rwnx_driver_switch_channel(void *priv, struct csa_settings *settings)
{
	struct wpa_rwnx_driver_data *drv = priv;
    struct wpa_driver_ap_params params;
	int csa_off_len = 0;
    u16 csa_oft[BCN_MAX_CSA_CPT];
    u8 i = 0;

    AIC_LOG_PRINTF("Channel switch request (cs_count=%u block_tx=%u freq=%d width=%d cf1=%d cf2=%d) \r\n",
		   settings->cs_count, settings->block_tx,
		   settings->freq_params.freq, settings->freq_params.bandwidth,
		   settings->freq_params.center_freq1,
		   settings->freq_params.center_freq2);
    /* Remove empty counters, assuming Probe Response and Beacon frame
	 * counters match. This implementation assumes that there are only two
	 * counters.
	 */
	if (settings->counter_offset_beacon[0] &&
	    !settings->counter_offset_beacon[1]) {
		csa_off_len = 1;
	} else if (settings->counter_offset_beacon[1] &&
		   !settings->counter_offset_beacon[0]) {
		csa_off_len = 1;
		settings->counter_offset_beacon[0] =
			settings->counter_offset_beacon[1];
		settings->counter_offset_presp[0] =
			settings->counter_offset_presp[1];
	} else if (settings->counter_offset_beacon[1] &&
		   settings->counter_offset_beacon[0]) {
		csa_off_len = 2;
	} else {
		AIC_LOG_PRINTF("nl80211: No CSA counters provided\r\n");
		return -1;
	}
	if (!settings->beacon_csa.tail)
		return -1;

	for (i = 0; i < csa_off_len; i++) {
		u16 csa_c_off_bcn = settings->counter_offset_beacon[i];
		u16 csa_c_off_presp = settings->counter_offset_presp[i];

		if ((settings->beacon_csa.tail_len <= csa_c_off_bcn) ||
			(settings->beacon_csa.tail[csa_c_off_bcn] !=
			 settings->cs_count))
			return -1;

		if (settings->beacon_csa.probe_resp &&
			((settings->beacon_csa.probe_resp_len <=
			 csa_c_off_presp) ||
			 (settings->beacon_csa.probe_resp[csa_c_off_presp] !=
			 settings->cs_count)))
			return -1;
	}

	/* If count is set to 0 (i.e anytime after this beacon) force it to 2 */
	if (settings->cs_count == 0) {
		settings->cs_count = 2;
		for (i = 0; i < csa_off_len; i++)
		{
		    settings->beacon_csa.tail[settings->counter_offset_beacon[i]] = 2;
		}
	}

	memset(csa_oft, 0, sizeof(csa_oft));
	for (i = 0; i < csa_off_len; i++)
	{
		csa_oft[i] = settings->counter_offset_beacon[i] + settings->beacon_csa.head_len + MAC_TIM_MIN_LEN;
	}
	params.head = settings->beacon_csa.head;
	params.head_len = settings->beacon_csa.head_len;
	params.tail = settings->beacon_csa.tail;
	params.tail_len = settings->beacon_csa.tail_len;

	if(!wpa_rwnx_driver_update_bcn(drv, &params, (uint8_t *)csa_oft)) {
		// notify EVENT_CH_SWITCH
		wpa_rwnx_driver_ch_switch(drv, &settings->freq_params);

		struct fhost_vif_tag *vif = NULL;
		vif = &fhost_env.vif[drv->fhost_vif_idx];
		rtos_semaphore_create(&vif->mac_vif->u.ap.csa_semaphore, "vif->mac_vif->u.ap.csa_semaphore", 1, 0);
		rtos_semaphore_wait(vif->mac_vif->u.ap.csa_semaphore, -1);
		rtos_semaphore_delete(vif->mac_vif->u.ap.csa_semaphore);
		params.head = settings->beacon_after.head;
		params.head_len = settings->beacon_after.head_len;
		params.tail = settings->beacon_after.tail;
		params.tail_len = settings->beacon_after.tail_len;
		wpa_rwnx_driver_update_bcn(drv, &params, NULL);
	}

	return 0;
}

#if 0
static int wpa_rwnx_driver_set_tx_queue_params(void *priv, int queue, int aifs, int cw_min,
					       int cw_max, int burst_time)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct cfgrwnx_set_edca cmd;
	struct cfgrwnx_resp resp;

	wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_SET_EDCA_CMD, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_SET_EDCA_RESP, sizeof(resp));

	cmd.fhost_vif_idx = drv->fhost_vif_idx;
	cmd.aci = queue;
	cmd.aifsn = aifs;
	cmd.cwmin = cw_min;
	cmd.cwmax = cw_max;
	cmd.txop = (burst_time * 100 + 16) / 32;

	if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) || (resp.status != CFGRWNX_SUCCESS))
		return -1;

	return 0;
}
#endif
static int wpa_rwnx_driver_hapd_send_eapol(void *priv, const u8 *addr, const u8 *data,
					   size_t data_len, int encrypt,
					   const u8 *own_addr, u32 flags)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct wpa_rwnx_tx_frame *tx_frame;
	bool ack = 0;

	aic_dbg("hapd_send_eapol %d(%d)\r\n", data_len, rtos_now(0));

	if (net_l2_send(fhost_to_net_if(drv->fhost_vif_idx), data, data_len, ETH_P_PAE,
			addr, &ack))
		return -1;

	tx_frame = wpa_rwnx_driver_init_tx_frame(drv, data, data_len, addr);
	if (!tx_frame)
		return -1;

    #if 1
	struct wpa_rwnx_driver_msg *event;

    event = rtos_malloc(sizeof(struct wpa_rwnx_driver_msg));
	event->id = CFGRWNX_TX_STATUS_EVENT;
	event->msg.tx_status_event.data = (uint8_t *)tx_frame;
	event->msg.tx_status_event.acknowledged = ack;

	rtos_semaphore_signal(drv->tx_frame_sem, false);
	if (wpa_rwnx_driver_send(&event)) {
		os_free(tx_frame->data);
		tx_frame->data = NULL;
        aic_dbg("wpa_rwnx_driver_send fail\r\n");

        rtos_free(event);
        event = NULL;
	}
    #else
	struct cfgrwnx_tx_status_event event;
	event.hdr.id = CFGRWNX_TX_STATUS_EVENT;
	event.hdr.len = sizeof(event);
	event.data = (uint8_t *)tx_frame;
	event.acknowledged = ack;

	rtos_semaphore_signal(drv->tx_frame_sem, false);
	if (fhost_cntrl_cfgrwnx_event_send(&event.hdr, drv->link->sock_send))
	{
		os_free(tx_frame->data);
		tx_frame->data = NULL;
	}
    #endif
	return 0;
}

static int wpa_rwnx_driver_sta_add(void *priv, struct hostapd_sta_add_params *params)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct cfgrwnx_sta_add cmd;
	struct cfgrwnx_resp resp;

	if (params->set) {
		TRACE_FHOST("[WPA] TODO: support set in sta_add");
		return -1;
	}

	memset(&cmd, 0, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_STA_ADD_CMD, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_STA_ADD_RESP, sizeof(resp));

	cmd.fhost_vif_idx = drv->fhost_vif_idx;
	cmd.aid = params->aid;
	cmd.addr = (const struct mac_addr *)params->addr;

	cmd.rate_set.length = params->supp_rates_len;
	if (cmd.rate_set.length > MAC_RATESET_LEN)
		cmd.rate_set.length = MAC_RATESET_LEN;
	os_memcpy(cmd.rate_set.array, params->supp_rates, cmd.rate_set.length);
	if (params->ht_capabilities) {
		cmd.flags |= STA_HT_CAPA;
		os_memcpy(&cmd.ht_cap, params->ht_capabilities, sizeof(cmd.ht_cap));
	}
	if (params->vht_capabilities) {
		cmd.flags |= STA_VHT_CAPA;
		os_memcpy(&cmd.vht_cap, params->vht_capabilities, sizeof(cmd.vht_cap));
	}
	if (params->he_capab) {
		cmd.flags |= STA_HE_CAPA;
		os_memcpy(&cmd.he_cap.mac_cap_info, params->he_capab->he_mac_capab_info,
			  sizeof(cmd.he_cap.mac_cap_info));
		os_memcpy(&cmd.he_cap.phy_cap_info, params->he_capab->he_phy_capab_info,
			  sizeof(cmd.he_cap.phy_cap_info));
		struct mac_he_mcs_nss_supp *p_mcs_supp = (struct mac_he_mcs_nss_supp *)params->he_capab->optional;
		os_memcpy(&cmd.he_cap.mcs_supp, p_mcs_supp, sizeof(cmd.he_cap.mcs_supp));

		//TODO complete
	}
	if (params->vht_opmode_enabled) {
		cmd.flags |= STA_OPMOD_NOTIF;
		cmd.opmode = params->vht_opmode;
	}
	if (params->flags & WPA_STA_WMM)
		cmd.flags |= STA_QOS_CAPA;
	if (params->flags & WPA_STA_MFP)
		cmd.flags |= STA_MFP_CAPA;
	cmd.uapsd_queues = (params->qosinfo & 0xF);
	cmd.max_sp_len  = (params->qosinfo & 0x60) >> 4;

	if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) || (resp.status != CFGRWNX_SUCCESS))
		return -1;
	if (drv->key_mgmt_suites == WPA_KEY_MGMT_SAE){
		u8 staid = vif_mgmt_get_staid(fhost_to_mac_vif(0),(const struct mac_addr *)params->addr);
		drv->sta_wpa3_active |= CO_BIT(staid%32);
	}
	return 0;
}

static int wpa_rwnx_driver_sta_remove(void *priv, const u8 *addr)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct cfgrwnx_sta_remove cmd;
	struct cfgrwnx_resp resp;
	u8 staid = vif_mgmt_get_staid(fhost_to_mac_vif(0),(const struct mac_addr *)addr);

	wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_STA_REMOVE_CMD, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_STA_REMOVE_RESP, sizeof(resp));

	cmd.fhost_vif_idx = drv->fhost_vif_idx;
	cmd.addr = (const struct mac_addr *)addr;

	if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) || (resp.status != CFGRWNX_SUCCESS))
		return -1;

	if (drv->key_mgmt_suites == WPA_KEY_MGMT_SAE){
		drv->sta_wpa3_active &= ~(CO_BIT(staid%32));
	}

	return 0;
}

static int wpa_rwnx_driver_sta_set_flags(void *priv, const u8 *addr,
					 unsigned int total_flags, unsigned int flags_or,
					 unsigned int flags_and)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct cfgrwnx_ctrl_port cmd;
	struct cfgrwnx_resp resp;
	int authorized = -1;

	// Only support authorized flag for now
	if (flags_or & WPA_STA_AUTHORIZED)
		authorized = 1;
	if (!(flags_and & WPA_STA_AUTHORIZED))
		authorized = 0;

	if (authorized < 0)
		return 0;

	wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_CTRL_PORT_CMD, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_CTRL_PORT_RESP, sizeof(resp));

	cmd.fhost_vif_idx = drv->fhost_vif_idx;
	os_memcpy(cmd.addr.array, addr, ETH_ALEN);
	cmd.authorized = authorized;

	if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) || (resp.status != CFGRWNX_SUCCESS))
		return -1;

	return 0;
}

static int wpa_rwnx_driver_sta_deauth(void *priv, const u8 *own_addr, const u8 *addr,
				      u16 reason)
{
	//struct wpa_rwnx_driver_data *drv = priv;
	struct ieee80211_mgmt mgmt;

	os_memset(&mgmt, 0, sizeof(mgmt));
	mgmt.frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					  WLAN_FC_STYPE_DEAUTH);
	os_memcpy(mgmt.da, addr, ETH_ALEN);
	os_memcpy(mgmt.sa, own_addr, ETH_ALEN);
	os_memcpy(mgmt.bssid, own_addr, ETH_ALEN);
	mgmt.u.deauth.reason_code = host_to_le16(reason);
	return wpa_rwnx_driver_send_mlme(priv, (u8 *) &mgmt,
					 IEEE80211_HDRLEN + sizeof(mgmt.u.deauth),
					 0, 0, NULL, 0);
}

static int wpa_rwnx_driver_sta_disassoc(void *priv, const u8 *own_addr, const u8 *addr,
					u16 reason)
{
	//struct wpa_rwnx_driver_data *drv = priv;
	struct ieee80211_mgmt mgmt;

	os_memset(&mgmt, 0, sizeof(mgmt));
	mgmt.frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					  WLAN_FC_STYPE_DISASSOC);
	os_memcpy(mgmt.da, addr, ETH_ALEN);
	os_memcpy(mgmt.sa, own_addr, ETH_ALEN);
	os_memcpy(mgmt.bssid, own_addr, ETH_ALEN);
	mgmt.u.disassoc.reason_code = host_to_le16(reason);
	return wpa_rwnx_driver_send_mlme(priv, (u8 *) &mgmt,
					 IEEE80211_HDRLEN + sizeof(mgmt.u.disassoc),
					 0, 0, NULL, 0);
}
#endif /* CFG_SOFTAP */

#if 0
static int wpa_rwnx_driver_get_seqnum(const char *ifname, void *priv, const u8 *addr,
				      int idx, u8 *seq)
{
	struct wpa_rwnx_driver_data *drv = priv;
	struct cfgrwnx_key_seqnum cmd;
	struct cfgrwnx_key_seqnum_resp resp;

	wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_KEY_SEQNUM_CMD, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_KEY_SEQNUM_RESP, sizeof(resp));

	cmd.fhost_vif_idx = drv->fhost_vif_idx;
	cmd.addr = (const struct mac_addr *)addr;
	cmd.key_idx = idx;

	if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) || (resp.status != CFGRWNX_SUCCESS))
		return -1;

	// assume buffer is always 8 bytes long
	for (int i = 0 ; i < 8 ; i ++) {
		seq[i] = (resp.seqnum >> (8 * i)) & 0xff;
	}

	return 0;
}
#endif
static void wpa_rwnx_driver_hostap_poll_client(void *priv, const u8 *own_addr,
					  const u8 *addr, int qos)
{
	struct ieee80211_hdr hdr;

	os_memset(&hdr, 0, sizeof(hdr));

	/*
	 * WLAN_FC_STYPE_NULLFUNC would be more appropriate,
	 * but it is apparently not retried so TX Exc events
	 * are not received for it.
	 * This is the reason the driver overrides the default
	 * handling.
	 */
	hdr.frame_control = IEEE80211_FC(WLAN_FC_TYPE_DATA,
					 WLAN_FC_STYPE_NULLFUNC);

	hdr.frame_control |=
		host_to_le16(WLAN_FC_FROMDS);
	os_memcpy(hdr.IEEE80211_DA_FROMDS, addr, ETH_ALEN);
	os_memcpy(hdr.IEEE80211_BSSID_FROMDS, own_addr, ETH_ALEN);
	os_memcpy(hdr.IEEE80211_SA_FROMDS, own_addr, ETH_ALEN);

	wpa_rwnx_driver_send_mlme(priv, (u8 *)&hdr, sizeof(hdr), 0, 0, NULL, 0);
}

static int wpa_rwnx_driver_get_inact_sec(void *priv, const u8 *addr)
{
	struct wpa_rwnx_driver_data *drv = priv;

	struct vif_info_tag *mac_vif = fhost_env.vif[drv->fhost_vif_idx].mac_vif;
	if (mac_vif) {
		uint8_t staid = vif_mgmt_get_staid(mac_vif, (struct mac_addr *)addr);
		if(staid < NX_REMOTE_STA_MAX) {
			struct sta_info_tag *sta = vif_mgmt_get_sta_by_staid(staid);
			//return (rtos_now(0) - sta->last_active_time_us) / 1000;
			return 0; // bypass sta activity poll, this handled by FW.
		}
	}

	return -1;
}

static void clean_survey_results(struct survey_results *survey_results)
{
	struct freq_survey *survey, *tmp;

	if (dl_list_empty(&survey_results->survey_list))
		return;

	dl_list_for_each_safe(survey, tmp, &survey_results->survey_list,
			      struct freq_survey, list) {
		dl_list_del(&survey->list);
		os_free(survey);
	}
}
#include "fhost_config.h"
extern struct rwnx_hw *g_rwnx_hw;
static void add_survey(unsigned int dummy, u32 ifidx,
		       struct dl_list *survey_list)
{
	struct freq_survey *survey;
	unsigned int freq;
    int idx;
	for (idx = 0; idx < SCAN_CHANNEL_MAX; idx ++) {
		if (g_rwnx_hw->survey[idx].filled != 0) {
			survey = os_zalloc(sizeof(struct freq_survey));
			if	(!survey)
				return;
			
			// Get the survey
			struct rwnx_survey_info *rwnx_survey = &g_rwnx_hw->survey[idx];

			if (idx < fhost_chan.chan2G4_cnt) {
				freq = fhost_chan.chan2G4[idx].freq;
			} else {
				freq = fhost_chan.chan5G[idx - fhost_chan.chan2G4_cnt].freq;
			}

			if (!freq) {
				os_free(survey);
				wpa_printf(MSG_DEBUG,"$$$ idx = %d\r\n", idx);
				continue;
			}
			survey->ifidx = idx;
			survey->freq = freq;
			survey->filled = 0;
			if (rwnx_survey->noise_dbm) {
				survey->nf = (int8_t)rwnx_survey->noise_dbm;
				survey->filled |= SURVEY_HAS_NF;
			}
			if (rwnx_survey->chan_time_ms) {
				survey->channel_time = rwnx_survey->chan_time_ms;
				survey->filled |= SURVEY_HAS_CHAN_TIME;
			}
			// if (rwnx_survey->chan_time_busy_ms) {
				survey->channel_time_busy = rwnx_survey->chan_time_busy_ms;
				survey->filled |= SURVEY_HAS_CHAN_TIME_BUSY;
			// }
			wpa_printf(MSG_DEBUG, "Freq survey dump event (freq=%d MHz noise=%d channel_time=%ld busy_time=%ld tx_time=%ld rx_time=%ld filled=%04x)",
				   survey->freq,
				   survey->nf,
				   (unsigned long int) survey->channel_time,
				   (unsigned long int) survey->channel_time_busy,
				   (unsigned long int) survey->channel_time_tx,
				   (unsigned long int) survey->channel_time_rx,
				   survey->filled);
			dl_list_add_tail(survey_list, &survey->list);
		}
	}
}

static int wpa_rwnx_get_survey(void *priv, unsigned int freq)
{
	struct wpa_rwnx_driver_data *drv = priv;

	union wpa_event_data data;
	struct survey_results *survey_results;

	os_memset(&data, 0, sizeof(data));
	survey_results = &data.survey_results;

	dl_list_init(&survey_results->survey_list);

	//if (freq)
	//	data.survey_results.freq_filter = freq;

	add_survey(freq, 0, &survey_results->survey_list);

	wpa_supplicant_event(drv->ctx, EVENT_SURVEY, &data);

	clean_survey_results(survey_results);

	return 0;
}

#ifdef CFG_P2P
static int wpa_rwnx_driver_probe_req_report(void *priv, int report) {
	wpa_printf(MSG_ERROR, "Probe_req_report");
	return 0;
}

static int wpa_rwnx_send_action(void *priv, unsigned int freq, unsigned int wait_time,
			   const u8 *dst, const u8 *src, const u8 *bssid,
			   const u8 *data, size_t data_len, int no_cck) 
{
	struct wpa_rwnx_driver_data *drv = priv;
	int ret = -1;
	u8 *buf;
	struct ieee80211_hdr *hdr;
	wpa_printf(MSG_ERROR, "Send Action frame (ifindex=%d, "
		   "freq=%u MHz wait=%d ms no_cck=%d)",
		   drv->fhost_vif_idx, freq, wait_time, no_cck);
	buf = os_zalloc(24 + data_len);
	if (buf == NULL)
		return ret;
	os_memcpy(buf + 24, data, data_len);
	hdr = (struct ieee80211_hdr *) buf;
	hdr->frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT, WLAN_FC_STYPE_ACTION);
	os_memcpy(hdr->addr1, dst, ETH_ALEN);
	os_memcpy(hdr->addr2, src, ETH_ALEN);
	os_memcpy(hdr->addr3, bssid, ETH_ALEN);

	ret = wpa_rwnx_driver_send_mlme(priv, buf, 24 + data_len,
						   0, freq, NULL, 0);
	os_free(buf);
	return ret;
	
}

static void wpa_rwnx_send_action_cancel_wait(void *priv) 
{
	//struct wpa_rwnx_driver_data *drv = priv;
	wpa_printf(MSG_ERROR, "Cancel TX action frame wait");
}

static int wpa_rwnx_driver_remain_on_channel(void *priv, unsigned int freq,
						unsigned int duration)
{
	printf("%s in\n", __func__);
	//struct i802_bss *bss = priv;
	//struct wpa_driver_nl80211_data *drv = bss->drv;
	struct wpa_rwnx_driver_data *drv = priv;
	struct cfgrwnx_roc_cmd cmd;
	struct cfgrwnx_resp resp;
	//struct nl_msg *msg;
	//int ret;
	//u64 cookie;

	wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_REMAIN_ON_CHANNEL_CMD, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_REMAIN_ON_CHANNEL_RESP, sizeof(resp));

	cmd.fhost_vif_idx = drv->fhost_vif_idx;
	cmd.freq = freq;
	cmd.duration = duration;
	cmd.sock = drv->link->sock_send;
	
	if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) || (resp.status != CFGRWNX_SUCCESS)) {
		wpa_printf(MSG_ERROR, "nl80211: Failed to request remain-on-channel "
		   "(freq=%d duration=%u)", freq, duration);
		return -1;
	}
		
	drv->pending_remain_on_chan = 1;
	printf("Remain-on-channel for freq=%u MHz duration=%u\n", freq, duration);

	return 0;

	#if 0
	if (!(msg = nl80211_cmd_msg(bss, 0, NL80211_CMD_REMAIN_ON_CHANNEL)) ||
	    nla_put_u32(msg, NL80211_ATTR_WIPHY_FREQ, freq) ||
	    nla_put_u32(msg, NL80211_ATTR_DURATION, duration)) {
		nlmsg_free(msg);
		return -1;
	}

	cookie = 0;
	ret = send_and_recv_msgs(drv, msg, cookie_handler, &cookie, NULL, NULL);
	if (ret == 0) {
		wpa_printf(MSG_DEBUG, "nl80211: Remain-on-channel cookie "
			   "0x%llx for freq=%u MHz duration=%u",
			   (long long unsigned int) cookie, freq, duration);
		drv->remain_on_chan_cookie = cookie;
		drv->pending_remain_on_chan = 1;
		return 0;
	}
	wpa_printf(MSG_DEBUG, "nl80211: Failed to request remain-on-channel "
		   "(freq=%d duration=%u): %d (%s)",
		   freq, duration, ret, strerror(-ret));
	return -1;
	#endif
}

static int wpa_rwnx_driver_cancel_remain_on_channel(void *priv)
{
	printf("%s in\n", __func__);
	//struct i802_bss *bss = priv;
	//struct wpa_driver_nl80211_data *drv = bss->drv;
	//struct nl_msg *msg;
	struct wpa_rwnx_driver_data *drv = priv;
	struct cfgrwnx_cancel_roc_cmd cmd;
	struct cfgrwnx_resp resp;
	int ret;

	if (!drv->pending_remain_on_chan) {
		printf("No pending remain-on-channel to cancel\n");
		return -1;
	}
	wpa_rwnx_msg_hdr_init(drv, &cmd.hdr, CFGRWNX_CANCEL_REMAIN_ON_CHANNEL_CMD, sizeof(cmd));
	wpa_rwnx_msg_hdr_init(drv, &resp.hdr, CFGRWNX_CANCEL_REMAIN_ON_CHANNEL_RESP, sizeof(resp));

	cmd.fhost_vif_idx = drv->fhost_vif_idx;

	if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr)) {
		printf("Failed to cancel remain-on-channel\n");
		return -1;
	}

	//drv->pending_remain_on_chan = 0;
	return 0;

	#if 0
	wpa_printf(MSG_DEBUG, "nl80211: Cancel remain-on-channel with cookie "
		   "0x%llx",
		   (long long unsigned int) drv->remain_on_chan_cookie);

	msg = nl80211_cmd_msg(bss, 0, NL80211_CMD_CANCEL_REMAIN_ON_CHANNEL);
	if (!msg ||
	    nla_put_u64(msg, NL80211_ATTR_COOKIE, drv->remain_on_chan_cookie)) {
		nlmsg_free(msg);
		return -1;
	}
	#endif

	#if 0
	ret = send_and_recv_msgs(drv, msg, NULL, NULL, NULL, NULL);
	if (ret == 0)
		return 0;
	wpa_printf(MSG_DEBUG, "nl80211: Failed to cancel remain-on-channel: "
		   "%d (%s)", ret, strerror(-ret));
	return -1;
	#endif
}

static int wpa_rwnx_driver_send_action(void *priv,
					  unsigned int freq,
					  unsigned int wait_time,
					  const u8 *dst, const u8 *src,
					  const u8 *bssid,
					  const u8 *data, size_t data_len,
					  int no_cck)
{
	//struct wpa_driver_nl80211_data *drv = bss->drv;
	struct wpa_rwnx_driver_data *drv = priv;
	int ret = -1;
	u8 *buf;
	struct ieee80211_hdr *hdr;
	int offchanok = 1;

    struct fhost_vif_tag *fhost_vif = &fhost_env.vif[drv->fhost_vif_idx];
    fhost_vif->conn_sock = drv->link->sock_send;
	#if 0
	if (is_ap_interface(drv->nlmode) && (int) freq == bss->freq &&
	    bss->beacon_set)
		offchanok = 0;

	wpa_printf(MSG_DEBUG, "nl80211: Send Action frame (ifindex=%d, "
		   "freq=%u MHz wait=%d ms no_cck=%d offchanok=%d)",
		   drv->ifindex, freq, wait_time, no_cck, offchanok);
	#endif
	printf("Send Action frame: vifindex %d freq=%u MHz wait=%d ms no_cck=%d offchanok=%d\n",
		   drv->fhost_vif_idx, freq, wait_time, no_cck, offchanok);

	buf = os_zalloc(24 + data_len);
	if (buf == NULL)
		return ret;
	os_memcpy(buf + 24, data, data_len);
	hdr = (struct ieee80211_hdr *) buf;
	hdr->frame_control =
		IEEE80211_FC(WLAN_FC_TYPE_MGMT, WLAN_FC_STYPE_ACTION);
	os_memcpy(hdr->addr1, dst, ETH_ALEN);
	os_memcpy(hdr->addr2, src, ETH_ALEN);
	os_memcpy(hdr->addr3, bssid, ETH_ALEN);

	#if 0
	if (os_memcmp(bss->addr, src, ETH_ALEN) != 0) {
		wpa_printf(MSG_DEBUG, "nl80211: Use random TA " MACSTR,
			   MAC2STR(src));
		os_memcpy(bss->rand_addr, src, ETH_ALEN);
	} else {
		os_memset(bss->rand_addr, 0, ETH_ALEN);
	}
	#endif

#if 0
#ifdef CONFIG_MESH
	if (is_mesh_interface(drv->nlmode)) {
		struct hostapd_hw_modes *modes;
		u16 num_modes, flags;
		u8 dfs_domain;
		int i;

		modes = nl80211_get_hw_feature_data(bss, &num_modes,
						    &flags, &dfs_domain);
		if (dfs_domain != HOSTAPD_DFS_REGION_ETSI &&
		    ieee80211_is_dfs(bss->freq, modes, num_modes))
			offchanok = 0;
		if (modes) {
			for (i = 0; i < num_modes; i++) {
				os_free(modes[i].channels);
				os_free(modes[i].rates);
			}
			os_free(modes);
		}
	}
#endif /* CONFIG_MESH */
#endif

	#if 0
	if (is_ap_interface(drv->nlmode) &&
	    (!(drv->capa.flags & WPA_DRIVER_FLAGS_OFFCHANNEL_TX) ||
	     (int) freq == bss->freq || drv->device_ap_sme ||
	     !drv->use_monitor))
		ret = wpa_driver_nl80211_send_mlme(bss, buf, 24 + data_len,
						   0, freq, no_cck, offchanok,
						   wait_time, NULL, 0, 0);
	else
		ret = nl80211_send_frame_cmd(bss, freq, wait_time, buf,
					     24 + data_len,
					     1, no_cck, 0, offchanok, NULL, 0);
	#endif

	wpa_rwnx_driver_send_mlme(priv, buf, 24 + data_len, 0, freq, NULL, 0);

	os_free(buf);
	return ret;
}
#endif

#ifdef CFG_SOFTAP
#ifdef CONFIG_RWNX_RADAR
int rwnx_start_radar_detection(struct aic_80211_chan_def *chandef);
int wpa_rwnx_start_dfs_cac(void *priv, struct hostapd_freq_params *freq)
{
	aic_dbg("%s %d\r\n", __func__, __LINE__);

	struct aic_80211_chan_def chandef;
	chandef.chan.band = (freq->freq > 5180)? _80211_BAND_5GHZ : _80211_BAND_2GHZ;
	chandef.chan.center_freq = freq->freq;
	chandef.center_freq1      = freq->center_freq1;
	chandef.center_freq2      = freq->center_freq2;
	chandef.width         = freq->bandwidth;

    if ((freq->freq >= 5260) && (freq->freq <= 5720 )) {
        chandef.chan.flags = CHAN_RADAR;
    }

	return rwnx_start_radar_detection(&chandef);
}
#endif
#endif

#ifdef CFG_P2P
#include "mac.h"
unsigned int g_aic_pref_chan_list[16] = {0};
unsigned int g_aic_pref_chan_cnt = 0;
void aic_set_pref_chan_list(unsigned int chan_list[], unsigned int num)
{
	if (num <= 16) {
		g_aic_pref_chan_cnt = num;
	} else {
		g_aic_pref_chan_cnt = 16;
	}

	for(int i = 0; i < g_aic_pref_chan_cnt; i++) {
		g_aic_pref_chan_list[i] = chan_list[i];
		aic_dbg("%s %d\r\n", __func__,chan_list[i]);
	}
}
int wpa_rwnx_get_pref_freq_list(void *priv, enum wpa_driver_if_type if_type,
				unsigned int *num, unsigned int *freq_list)
{
	if (WPA_IF_P2P_GO == if_type) {
		*num = g_aic_pref_chan_cnt;
		for(int i = 0; i < g_aic_pref_chan_cnt; i++) {
			freq_list[i] = phy_channel_to_freq(((g_aic_pref_chan_list[i]>14) ? 1: 0), g_aic_pref_chan_list[i]);
			aic_dbg("%s %d %d\r\n", __func__, freq_list[i], g_aic_pref_chan_list[i] );
		}
		return 0;
	}
	return -1;
}
#endif /* CFG_P2P */
const struct wpa_driver_ops wpa_driver_rwnx_ops = {
	.name = "RWNX",
	.desc = "RWNX + lwIP driver",
	.init = wpa_rwnx_driver_init,
	.deinit = wpa_rwnx_driver_deinit,
	.get_hw_feature_data = wpa_rwnx_driver_get_hw_feature_data,
	.get_capa = wpa_rwnx_driver_get_capa,
	.set_key = wpa_rwnx_driver_set_key,
	.scan2 = wpa_rwnx_driver_scan2,
	.get_scan_results2 = wpa_rwnx_driver_get_scan_results2,
	.set_supp_port = wpa_rwnx_driver_set_supp_port,
	.associate = wpa_rwnx_driver_associate,
	.get_bssid = wpa_rwnx_driver_get_bssid,
	.get_ssid = wpa_rwnx_driver_get_ssid,
	.deauthenticate = wpa_rwnx_driver_deauthenticate,
	.set_operstate = wpa_rwnx_driver_set_operstate,
	.send_mlme = wpa_rwnx_driver_send_mlme,
	.send_external_auth_status = wpa_rwnx_driver_send_external_auth_status,
	#ifdef CFG_SOFTAP
	.set_ap = wpa_rwnx_driver_set_ap,
	.deinit_ap = wpa_rwnx_driver_deinit_ap,
	.stop_ap   = wpa_rwnx_driver_stop_ap,
	//.set_tx_queue_params = wpa_rwnx_driver_set_tx_queue_params,
	.hapd_send_eapol = wpa_rwnx_driver_hapd_send_eapol,
	.sta_add = wpa_rwnx_driver_sta_add,
	.sta_remove = wpa_rwnx_driver_sta_remove,
	.sta_set_flags = wpa_rwnx_driver_sta_set_flags,
	.sta_deauth = wpa_rwnx_driver_sta_deauth,
	.sta_disassoc = wpa_rwnx_driver_sta_disassoc,
	.switch_channel = wpa_rwnx_driver_switch_channel,
	.poll_client = wpa_rwnx_driver_hostap_poll_client,
	.get_inact_sec = wpa_rwnx_driver_get_inact_sec,
	.get_survey    = wpa_rwnx_get_survey,
    #ifdef CONFIG_RWNX_RADAR
	.start_dfs_cac = wpa_rwnx_start_dfs_cac,
	#endif
	#endif
	#ifdef CFG_P2P
	.probe_req_report = wpa_rwnx_driver_probe_req_report,
	.remain_on_channel = wpa_rwnx_driver_remain_on_channel,
	.cancel_remain_on_channel = wpa_rwnx_driver_cancel_remain_on_channel,
	.send_action = wpa_rwnx_driver_send_action,
	.send_action_cancel_wait = wpa_rwnx_send_action_cancel_wait,
	.get_pref_freq_list      = wpa_rwnx_get_pref_freq_list,
	#endif
	//.get_seqnum = wpa_rwnx_driver_get_seqnum,
};
