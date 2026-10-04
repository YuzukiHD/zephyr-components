#ifndef _WLAN_IF_H_
#define _WLAN_IF_H_

#include "mac.h"
#include "net_al.h"
#include "wifi.h"
#include "co_list.h"

#define WLAN_CONNECT_CFG_NONE                   0x00
#define WLAN_CONNECT_CFG_DISABLE_AUTO_RECONN    0xDA

#define WLAN_CONNECT_TO_CFG_2_PARAM(cfg,to_ms)  (((cfg) << 24) | ((to_ms) & 0xFFFFFF))
#define WLAN_CONNECT_PARAM_2_TO_MS(param)       ((uint32_t)(param) & 0xFFFFFF)
#define WLAN_CONNECT_PARAM_2_CFG(param)         ((uint32_t)(param) >> 24)

extern uint8_t is_fixed_ip;
extern uint32_t fixed_ip, fixed_gw, fixed_mask;

#define MAX_AP_COUNT 32
typedef struct wifi_ap_info {
    unsigned char   ssid[32];
    unsigned char   bssid[6];
    unsigned int    channel;
    unsigned int    rssi;
} wifi_ap_info_t;

typedef struct _wifi_ap_list
{
	unsigned short  ap_count;
	wifi_ap_info_t ap_info[MAX_AP_COUNT];
}wifi_ap_list_t;

enum wifi_mac_acl_mode {
	WIFI_MAC_ACL_DISABLED,
	WIFI_MAC_ACL_BLACKLIST,
	WIFI_MAC_ACL_WHITELIST
};
#define WIFI_MAC_ADDR_LEN   6
struct wifi_mac_node {
	struct co_list_hdr node;
	char mac[WIFI_MAC_ADDR_LEN];
};
/**
 ****************************************************************************************
 * @brief * set_sta_not_autoconnect :
 *                          Must set_sta_not_autoconnect after calling 'wlan_start_sta'. This
 *                          interface is limit to set sta only, not for sta+ap.
 *
 * @param[in]  vif_idx:     default 0
 * @return value:
 *         other: fail
 *             0: success
 ****************************************************************************************
 */
int set_sta_not_autoconnect(int vif_idx);

/* timeout_ms: 0 -> 10000, -1 -> no timeout, (timeout_ms > 0 && bit0 is 1) -> wep
 * return value: -1:password < 8, -6: dhcp fail
 */
int wlan_start_sta(uint8_t *ssid, uint8_t *pw, int timeout_ms);

int wlan_sta_connect(uint8_t *ssid, uint8_t *pw, int timeout_ms);

int wlan_start_ap(struct aic_ap_cfg *user_cfg);

int wlan_start_p2p(struct aic_p2p_cfg *user_cfg);

int wlan_disconnect_sta(uint8_t idx);

int wlan_stop_ap(void);

int wlan_stop_p2p(void);

/* 1 -> started, 0 -> stoped
 */
uint8_t wlan_soft_ap_started(void);

int wlan_start_wps(void);

int wlan_dhcp(net_if_t *net_if);

int wlan_get_connect_status(void);

int wlan_ap_switch_channel(uint8_t chan_num);

int wlan_ap_disassociate_sta(struct mac_addr *macaddr);

/* should be called before 'wlan_start_ap', default (192 | (168 << 8) | (88 << 16) | (1 << 24)) */
void set_ap_ip_addr(uint32_t new_ip_addr);
uint32_t get_ap_ip_addr(void);

/* Default: 255.255.255.0 -> 0x00FFFFFF */
void set_ap_subnet_mask(uint32_t new_mask);
uint32_t get_ap_subnet_mask(void);

/* Beacon interval, default: 100 */
void set_ap_bcn_interval(uint32_t bcn_interval_ms);

void set_ap_channel_num(uint8_t num);
uint8_t get_ap_channel_num(void);

void set_ap_hidden_ssid(uint8_t val);

void set_ap_enable_he_rate(uint8_t en);

void set_ap_allow_sta_inactivity_s(uint8_t s);

void set_sta_connect_chan_num(uint32_t chan_num);
uint32_t get_sta_connect_chan_freq(void);

void wlan_if_scan_open(void);
void wlan_if_scan(void);
void wlan_if_getscan(wifi_ap_list_t *ap_list);
void wlan_if_scan_close(void);
int wlan_ap_mac_acl_init(void);
int wlan_ap_mac_acl_deinit(void);
int wlan_ap_set_mac_acl_mode(char mode);
int wlan_ap_get_mac_acl_mode(void);
int wlan_ap_add_blacklist(struct mac_addr *macaddr);
int wlan_ap_delete_blacklist(struct mac_addr *macaddr);
int wlan_ap_add_whitelist(struct mac_addr *macaddr);
int wlan_ap_delete_whitelist(struct mac_addr *macaddr);
uint8_t wlan_ap_get_mac_acl_list_cnt(void);
void * wlan_ap_get_mac_acl_list(void);

/*
 * Get the number of associated sta, max: 32
*/
uint8_t wlan_ap_get_associated_sta_cnt(void);

/*
 * Get list of associated sta
*/
void *wlan_ap_get_associated_sta_list(void);

/*
 * Get rssi of associated sta
 * [in] mac addrress of sta
*/
int8_t wlan_ap_get_associated_sta_rssi(uint8_t *addr);

struct sta_info_tag;
struct station_info;
int aicwf_get_station_info(struct sta_info_tag *sta, struct station_info *sta_info, uint8_t *phymode, uint32_t *tx_phyrate, uint32_t *rx_phyrate);

int wlan_stop_p2p_with_monitor(void);
#endif /* _WLAN_IF_H_ */
