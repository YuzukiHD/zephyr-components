/**
 ****************************************************************************************
 *
 * @file fhost.c
 *
 * @brief Implementation of the fully hosted entry point.
 *
 * Copyright (C) RivieraWaves 2017-2019
 *
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @addtogroup FHOST
 * @{
 ****************************************************************************************
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "fhost.h"
#include "fhost_tx.h"
#include "fhost_rx.h"
#include "fhost_cntrl.h"
#include "fhost_wpa.h"
#include "fhost_config.h"
#include "net_al.h"
#include "dbg_assert.h"
#include "rwnx_utils.h"
#include "rwnx_defs.h"
#include "fhost_wpa.h"
#include "co_endian.h"
#include "wlan_if.h"

#include "porting.h"
#include "rwnx_msg_tx.h"
#include "lwip/sockets.h"

#define dbg_snprintf snprintf
/*
 * DEFINITIONS
 ****************************************************************************************
 */

/*
 * GLOBAL VARIABLES
 ****************************************************************************************
 */
#define SCANU_MAX_RESULTS 64
struct mac_scan_result scan_result[SCANU_MAX_RESULTS];
static uint16_t scan_result_cnt = 0;
static int8_t rssi_thold = ~0x7F;

struct fhost_env_tag fhost_env;
// FIXME: share ???
struct vif_info_tag vif_info_tab[NX_VIRT_DEV_MAX];
struct co_list free_sta_list;
struct sta_info_tag sta_info_tab[STA_MAX + NX_VIRT_DEV_MAX];
int wlan_connected = 0;
#ifdef CFG_AIC_HSU_CHKSUM
/// Semaphore used for Checksum
rtos_semaphore checksum_lock;
#endif /* CFG_AIC_HSU_CHKSUM */
fhost_user_cfg_t fhost_usr_cfg = {0}; //.ipc_irq_prio = __NVIC_PRIO_LOWEST,
extern uint8_t mac_vif_index;
extern uint8_t sta_index;


/*
 * FUNCTIONS
 ****************************************************************************************
 */

static void vif_mgmt_entry_init(struct vif_info_tag *vif)
{
    // Reset table
    //memset(vif, 0, sizeof(*vif));//

    vif->type = VIF_UNKNOWN;
}

uint8_t vif_mgmt_get_staid(const struct vif_info_tag *vif, const struct mac_addr *sta_addr)
{
    struct co_list_hdr *list_hdr = co_list_pick(&vif->sta_list);

    // TODO: Using MACHW to retrieve KeyRam Idx from MAC addr may be faster
    while (list_hdr != NULL)
    {
        struct sta_info_tag *sta = (struct sta_info_tag *)list_hdr;
        if (MAC_ADDR_CMP(&sta->mac_addr, sta_addr))
            return sta->staid;
        list_hdr = co_list_next(list_hdr);
    }
    return INVALID_STA_IDX;
}

struct sta_info_tag * vif_mgmt_get_sta_by_addr(const struct mac_addr * sta_addr)
{
    uint8_t i = 0;
    struct sta_info_tag *sta = NULL;

    for (i = 0; i< STA_MAX; i++) {
        sta = &sta_info_tab[i];
        if (sta->valid && MAC_ADDR_CMP(sta->mac_addr.array, sta_addr->array))
        {
            return sta;
        }
    }

    return NULL;;
}

struct sta_info_tag * vif_mgmt_get_sta_by_staid(uint8_t sta_id)
{
    uint8_t i = 0;
    struct sta_info_tag *sta = NULL;

    for (i = 0; i< STA_MAX; i++) {
        sta = &sta_info_tab[i];
        if (sta->valid && (sta->staid == sta_id))
        {
            return sta;
        }
    }

    return NULL;
}

uint8_t vif_mgmt_sta_cnt(void)
{
    struct vif_info_tag *vif = fhost_to_mac_vif(0);

    if (!vif)
        return 0;

    return co_list_cnt(&vif->sta_list);
}

void * get_vif_mgmt_sta_list(void)
{
    struct vif_info_tag *vif = fhost_to_mac_vif(0);

    if (!vif)
        return NULL;

    return &vif->sta_list;
}

static uint8_t user_limit_sta_num = NX_REMOTE_STA_MAX;

uint8_t user_limit_sta_num_get(void)
{
    return user_limit_sta_num;
}
void user_limit_sta_num_set(uint8_t num)
{
    user_limit_sta_num = num;
}

static void sta_mgmt_entry_init(struct sta_info_tag *sta)
{
    // Reset table
    memset(sta, 0, sizeof(*sta));

    // Set the instance number to 0xFF, indicate the sta is free
    sta->inst_nbr = 0xFF;
}

void fhost_init(struct rwnx_hw *rwnx_hw)
{
    int i = 0;

    // Initialize FHOST environment
    //memset(&fhost_env, 0, sizeof(fhost_env));//

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    for(i = 0; i < NX_VIRT_DEV_MAX ; i++)
    {
        struct vif_info_tag *vif = &vif_info_tab[i];
        // Init VIF info table.
        vif_mgmt_entry_init(vif);
        co_list_init(&vif->sta_list);
        // Push to free list.
        //co_list_push_back(&vif_mgmt_env.free_list, (struct co_list_hdr*)vif);
    }

    // Initialize STA search lists
    // init free list
    co_list_init(&free_sta_list);
    // push all the entries to the free list
    for(i = 0; i < NX_REMOTE_STA_MAX; i++)
    {
        struct sta_info_tag *sta = &sta_info_tab[i];
        // Init STA info table.
        sta_mgmt_entry_init(sta);
        // Push to free list.
        co_list_push_back(&free_sta_list, (struct co_list_hdr*)sta);
    }

    // Control task
    if (fhost_cntrl_init((void*)rwnx_hw))
    {
        ASSERT_ERR(0);
    }

    // TX
    if (fhost_tx_init())
    {
        ASSERT_ERR(0);
    }

    #if (FHOST_RX_SW_VER == 2)
    fhost_rx_init();
    #elif (FHOST_RX_SW_VER == 3)
    fhost_rx_init((void*)rwnx_hw);
    #endif
    #ifdef CONFIG_RWNX_RADAR
    rwnx_radar_detection_init(&rwnx_hw->radar);
    #ifdef CONFIG_RADAR_OR_IR_DETECT
    rwnx_radar_set_domain(&rwnx_hw->radar, NL80211_DFS_FCC);
    #endif /* CONFIG_RADAR_OR_IR_DETECT */
    #endif /* CONFIG_RWNX_RADAR */
}

int fhost_vif_name(int fhost_vif_idx, char *name, int len)
{
    if (fhost_vif_idx > NX_VIRT_DEV_MAX)
        return -1;

    return net_if_get_name(&fhost_env.vif[fhost_vif_idx].net_if, name, len);
}

int fhost_vif_idx_from_name(const char *name)
{
    net_if_t *net_if;
    int i;

    if (name == NULL)
        return -1;

    net_if = net_if_find_from_name(name);
    if (!net_if)
        return -1;

    for (i = 0 ; i < NX_VIRT_DEV_MAX ; i++) {
        if (&fhost_env.vif[i].net_if == net_if)
            return i;
    }

    return -1;
}

int fhost_vif_set_uapsd_queues(int fhost_vif_idx, uint8_t uapsd_queues)
{
    if (fhost_vif_idx < 0)
    {
        int i;
        for (i = 0 ; i < NX_VIRT_DEV_MAX ; i++)
        {
            fhost_env.vif[i].uapsd_queues = uapsd_queues;
        }
    }
    else
    {
        if (fhost_vif_idx > NX_VIRT_DEV_MAX)
            return -1;
        fhost_env.vif[fhost_vif_idx].uapsd_queues = uapsd_queues;
    }

    return 0;
}

int fhost_open_loopback_udp_sock(int port)
{
    struct sockaddr_in wpa;
    int sock;

    sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock == -1)
        return sock;
    memset(&wpa, 0, sizeof(wpa));
    wpa.sin_family = AF_INET;
    wpa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    wpa.sin_port = htons(port);
    if (connect(sock, (struct sockaddr *)&wpa, sizeof(wpa)) < 0)
        goto err;

    return sock;

  err:
    close(sock);
    return -1;
}

void fhost_get_status(struct fhost_status *status)
{
    int i;

    status->vif_max_cnt = NX_VIRT_DEV_MAX;
    status->vif_active_cnt = 0;
    status->vif_first_active = -1;

    for (i = 0 ; i < NX_VIRT_DEV_MAX ; i++)
    {
        if (fhost_env.vif[i].mac_vif != NULL)
        {
            status->vif_active_cnt++;
            if (status->vif_first_active < 0)
                status->vif_first_active = i;
        }
    }

    status->chan_2g4_cnt = fhost_chan.chan2G4_cnt;
    status->chan_2g4 = fhost_chan.chan2G4;

    status->chan_5g_cnt = fhost_chan.chan5G_cnt;
    status->chan_5g = fhost_chan.chan5G;
}

int fhost_get_vif_status(int fvif_idx, struct fhost_vif_status *status)
{
    struct fhost_vif_tag *fhost_vif;

    if (fvif_idx >= NX_VIRT_DEV_MAX)
    {
        printf("fhost_get_vif_status 1\n");
        return -1;
    }

    memset(status, 0, sizeof(*status));

    fhost_vif = &fhost_env.vif[fvif_idx];
    status->index = fvif_idx;
    if (fhost_vif->mac_vif)
    {
        struct vif_info_tag *mac_vif = fhost_vif->mac_vif;
        status->type = mac_vif->type;

        #if (NX_CHNL_CTXT)
        if (mac_vif->chan_ctxt)
        {
            status->chan = mac_vif->chan_ctxt->channel;
        }
        #endif

        if ((mac_vif->type == VIF_STA) &&
            (mac_vif->u.sta.ap_id != INVALID_STA_IDX))
        {
            struct sta_info_tag *sta = vif_mgmt_get_sta_by_staid(mac_vif->u.sta.ap_id);
            if (sta) {
                status->sta.bssid = sta->mac_addr;
                status->sta.rssi = mac_vif->u.sta.rssi;
            }
        }
    }
    else
    {
        //printf("fhost_get_vif_status 2\n");
        status->type = VIF_UNKNOWN;
    }

    status->mac_addr = (uint8_t *)&fhost_vif->mac_addr;

    return 0;
}

int fhost_set_vif_type(struct fhost_cntrl_link *link, int fvif_idx, enum mac_vif_type type,
                       bool p2p)
{
    struct fhost_vif_tag *fhost_vif;
    enum mac_vif_type prev_type = VIF_UNKNOWN;

    if (fvif_idx >= NX_VIRT_DEV_MAX)
        return -1;

    fhost_vif = &fhost_env.vif[fvif_idx];

    if (fhost_vif->mac_vif)
        prev_type = (enum mac_vif_type)fhost_vif->mac_vif->type;

    // Do nothing if interface type is already the requested one
    if (prev_type == type)
        return 0;

    // Close current connection (if any)
    //p_cfg.mode = IP_ADDR_NONE;
    //fhost_set_vif_ip(fvif_idx, &ip_cfg);
    fhost_wpa_end(fvif_idx);

    if (prev_type == VIF_MONITOR)
        fhost_rx_set_monitor_cb(NULL, NULL);

    // Change type of the associated MAC vif
    return fhost_cntrl_set_mac_vif_type(link, fvif_idx, type, p2p);
}

void fhost_scan_start(void);

int fhost_scan(struct fhost_cntrl_link *link, int fvif_idx, int *freq_list)
{
    struct cfgrwnx_scan cmd;
    struct cfgrwnx_resp resp;
    struct cfgrwnx_scan_ssid ssid;
    struct cfgrwnx_msg_hdr msg_hdr;
    int nb_result = 0;

    ssid.len = 0;
    ssid.ssid = NULL;
    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = CFGRWNX_SCAN_CMD;
    cmd.fhost_vif_idx = fvif_idx;
    cmd.freqs = freq_list;
    cmd.extra_ies = NULL;
    cmd.bssid = NULL;
    cmd.ssids = &ssid;
    cmd.extra_ies_len = 0;
    cmd.no_cck = 0;
    cmd.ssid_cnt = 1;
    cmd.sock = link->sock_send;
    cmd.hdr.resp_queue = link->queue;

    resp.hdr.len = sizeof(resp);
    resp.hdr.id = CFGRWNX_SCAN_RESP;

    if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) ||
        (resp.status != CFGRWNX_SUCCESS))
        return -1;

    fhost_scan_start();

    // Wait scan to complete
    while(1)
    {
        int len = recv(link->sock_recv, &msg_hdr, sizeof(msg_hdr), MSG_PEEK);
        if (len < 0)
            break;

        if (msg_hdr.id == CFGRWNX_SCAN_DONE_EVENT)
        {
            struct cfgrwnx_scan_completed res;
            len = recv(link->sock_recv, &res, sizeof(res), 0);
            if (len == sizeof(res))
            {
                if (res.status != CFGRWNX_SUCCESS)
                    return -1;
                nb_result = res.result_cnt;
            }
            break;
        }
        else if (msg_hdr.id == CFGRWNX_SCAN_RESULT_EVENT)
        {
            struct cfgrwnx_scan_result res;
            // re-read without MSG_PEEK to skip buffer
            len = recv(link->sock_recv, &res, sizeof(res), 0);
            if (res.payload) {
                //AIC_LOG_PRINTF("free payload:%p\n",res.payload);
                rtos_free(res.payload);
                res.payload = NULL;
            }
        } else {
            AIC_LOG_PRINTF("recv msg_hdr.id=%d\n", msg_hdr.id);
        }
    }

    return nb_result;
}

static uint16_t mac_ie_len(uint32_t addr)
{
    return ((uint16_t)co_read8p(addr + MAC_INFOELT_LEN_OFT) + MAC_INFOELT_INFO_OFT);
}
static uint32_t mac_ie_find(uint32_t addr,
                            uint16_t buflen,
                            uint8_t ie_id,
                            uint16_t *len)
{
    uint32_t end = addr + buflen;
    // loop as long as we do not go beyond the frame size
    while ((addr + MAC_INFOELT_LEN_OFT) < end)
    {
        uint16_t ie_len = mac_ie_len(addr);
        uint32_t ie_end = addr + ie_len;

        // Check if the current IE is the one we look for
        if (ie_id == co_read8p(addr))
        {
            // Check if the IE length complies with the remaining length in the buffer
            if (ie_end > end)
                return 0;

            *len = ie_len;

            // The IE is valid
            return addr;
        }
        // move on to the next IE
        addr = ie_end;
    }

    return 0;
}

static uint32_t mac_vsie_find(uint32_t addr,
                              uint16_t buflen,
                              uint8_t const *oui,
                              uint8_t ouilen,
                              uint16_t *len)
{
    uint32_t end = addr + buflen;

    // loop as long as we do not go beyond the frame size
    while (addr < end)
    {
        // First of all we need to find the OUI ID
        addr = mac_ie_find(addr, buflen, MAC_ELTID_OUI, len);

        // Check if we found the OUI ID, and that we have enough bytes
        // available after the length for the OUI length
        if ((addr == 0) || ((addr + MAC_INFOELT_INFO_OFT + ouilen) > end))
            return 0;

        // check if the OUI matches the one we are looking for
        if (co_cmp8p(addr + MAC_INFOELT_INFO_OFT, oui, ouilen))
        {
            // the OUI matches, return the pointer to this IE
            return addr;
        }

        // Move on to the next OUI ID
        addr += *len;
        buflen -= *len;
    }

    return 0;
}
static uint32_t mac_ie_ssid_find(uint32_t buffer, uint16_t buflen, uint8_t *ie_len)
{
    uint16_t len;
    uint32_t addr = mac_ie_find(buffer, buflen, MAC_ELTID_SSID, &len);

    if ((addr == 0) || (len > MAC_SSID_MAX_LEN))
        return 0;

    *ie_len = len - MAC_INFOELT_INFO_OFT;

    return addr;
}

uint32_t mac_ie_tim_find(uint32_t buffer, uint16_t buflen, uint16_t *ie_len)
{
    uint32_t addr = mac_ie_find(buffer, buflen, MAC_ELTID_TIM, ie_len);

    if ((addr == 0) || (*ie_len > MAC_TIM_MAX_LEN) ||
        (*ie_len < MAC_TIM_MIN_LEN))
        return 0;
    return (addr - buffer);
}

enum mac_cipher_suite mac_cipher_suite_value(uint32_t cipher_suite)
{
    switch (cipher_suite)
    {
        case MAC_RSNIE_CIPHER_WEP_40:
            return MAC_CIPHER_WEP40;
        case MAC_RSNIE_CIPHER_TKIP:
        case MAC_WPA_CIPHER_TKIP:
            return MAC_CIPHER_TKIP;
        case MAC_RSNIE_CIPHER_CCMP_128:
        case MAC_WPA_CIPHER_CCMP:
            return MAC_CIPHER_CCMP;
        case MAC_RSNIE_CIPHER_WEP_104:
            return MAC_CIPHER_WEP104;
        case MAC_RSNIE_CIPHER_BIP_CMAC_128:
            return MAC_CIPHER_BIP_CMAC_128;
        case MAC_RSNIE_CIPHER_GCMP_128:
            return MAC_CIPHER_GCMP_128;
        case MAC_RSNIE_CIPHER_GCMP_256:
            return MAC_CIPHER_GCMP_256;
        case MAC_RSNIE_CIPHER_CCMP_256:
            return MAC_CIPHER_CCMP_256;
        case MAC_RSNIE_CIPHER_BIP_GMAC_128:
            return MAC_CIPHER_BIP_GMAC_128;
        case MAC_RSNIE_CIPHER_BIP_GMAC_256:
            return MAC_CIPHER_BIP_GMAC_256;
        case MAC_RSNIE_CIPHER_BIP_CMAC_256:
            return MAC_CIPHER_BIP_CMAC_256;
        case MAC_WAPI_CIPHER_WPI_SMS4:
            return MAC_CIPHER_WPI_SMS4;
        default:
            return -1;
    }
}

uint32_t mac_ie_rsn_find(uint32_t buffer, uint16_t buflen, uint8_t *ie_len)
{
    uint16_t len;
    uint32_t addr = mac_ie_find(buffer, buflen, MAC_ELTID_RSN_IEEE, &len);

    if ((addr == 0) || (len < MAC_RSNIE_MIN_LEN))
        return 0;

    *ie_len = len - MAC_INFOELT_INFO_OFT;

    return addr;
}

uint32_t mac_ie_wpa_find(uint32_t buffer, uint16_t buflen, uint8_t *ie_len)
{
    uint16_t len;
    uint32_t addr = mac_vsie_find(buffer, buflen,
                                  (uint8_t const *)"\x00\x50\xF2\x01\x01", 5, &len);

    if ((addr == 0) ||  (len < MAC_WPA_MIN_LEN))
        return 0;

    *ie_len = len - MAC_INFOELT_INFO_OFT;

    return addr;
}

uint32_t mac_ie_wapi_find(uint32_t buffer, uint16_t buflen, uint8_t *ie_len)
{
    uint16_t len;
    uint32_t addr = mac_ie_find(buffer, buflen, MAC_ELTID_WAPI, &len);

    if ((addr == 0) || (len < MAC_WAPI_MIN_LEN))
        return 0;

    *ie_len = len - MAC_INFOELT_INFO_OFT;

    return addr;
}

enum mac_akm_suite mac_akm_suite_value(uint32_t akm_suite)
{
    switch (akm_suite)
    {
        case MAC_RSNIE_AKM_8021X:
        case MAC_WPA_AKM_8021X:
            return MAC_AKM_8021X;
        case MAC_RSNIE_AKM_PSK:
        case MAC_WPA_AKM_PSK:
            return MAC_AKM_PSK;
        case MAC_RSNIE_AKM_FT_8021X:
            return MAC_AKM_FT_8021X;
        case MAC_RSNIE_AKM_FT_PSK:
            return MAC_AKM_FT_PSK;
        case MAC_RSNIE_AKM_8021X_SHA256:
            return MAC_AKM_8021X_SHA256;
        case MAC_RSNIE_AKM_PSK_SHA256:
            return MAC_AKM_PSK_SHA256;
        case MAC_RSNIE_AKM_TDLS:
            return MAC_AKM_TDLS;
        case MAC_RSNIE_AKM_SAE:
            return MAC_AKM_SAE;
        case MAC_RSNIE_AKM_FT_OVER_SAE:
            return MAC_AKM_FT_OVER_SAE;
        case MAC_RSNIE_AKM_8021X_SUITE_B:
            return MAC_AKM_8021X_SUITE_B;
        case MAC_RSNIE_AKM_8021X_SUITE_B_192:
            return MAC_AKM_8021X_SUITE_B_192;
        case MAC_RSNIE_AKM_FILS_SHA256:
            return MAC_AKM_FILS_SHA256;
        case MAC_RSNIE_AKM_FILS_SHA384:
            return MAC_AKM_FILS_SHA384;
        case MAC_RSNIE_AKM_FT_FILS_SHA256:
            return MAC_AKM_FT_FILS_SHA256;
        case MAC_RSNIE_AKM_FT_FILS_SHA384:
            return MAC_AKM_FT_FILS_SHA384;
        case MAC_RSNIE_AKM_OWE:
            return MAC_AKM_OWE;
        case MAC_WAPI_AKM_CERT:
            return MAC_AKM_WAPI_CERT;
        case MAC_WAPI_AKM_PSK:
            return MAC_AKM_WAPI_PSK;
        default:
            return -1;
    }
}

void scanu_get_security_info(uint32_t ies, uint16_t ies_len, struct mac_scan_result *res)
{
    uint32_t sec_ie;
    uint8_t sec_ie_len;
    int cnt, len;

    res->akm = 0;
    res->group_cipher = 0;
    res->pairwise_cipher = 0;

    if (!(res->cap_info & MAC_CAPA_PRIVA))
    {
        res->akm = CO_BIT(MAC_AKM_NONE);
        return;
    }

    #define READ_CIPHER_SUITE(type)                                         \
        {                                                                   \
            int val = mac_cipher_suite_value(co_ntohl(co_read32p(sec_ie))); \
            if (val > 0)                                                    \
                res->type |= CO_BIT(val);                                   \
            sec_ie += 4;                                                    \
            len -= 4;                                                       \
         }

    #define READ_AKM_SUITE()                                             \
        {                                                                \
            int val = mac_akm_suite_value(co_ntohl(co_read32p(sec_ie))); \
            if (val > 0)                                                 \
                res->akm |= CO_BIT(val);                                 \
            sec_ie += 4;                                                 \
            len -= 4;                                                    \
         }

    #define READ_CNT()                          \
        cnt = co_wtohs(co_read16p(sec_ie));     \
        sec_ie += 2;                            \
        len -= 2;

    // First look for RSN Element
    sec_ie = mac_ie_rsn_find(ies, ies_len, &sec_ie_len);

    if (sec_ie)
    {
        uint16_t rsn_capa;

        sec_ie += MAC_RSNIE_GROUP_CIPHER_OFT;
        len = sec_ie_len + MAC_INFOELT_INFO_OFT - MAC_RSNIE_GROUP_CIPHER_OFT;

        READ_CIPHER_SUITE(group_cipher);

        READ_CNT();
        while ((cnt > 0) && (len >= 4))
        {
            READ_CIPHER_SUITE(pairwise_cipher);
            cnt--;
        }

        if (sec_ie_len < 2)
            return;

        READ_CNT();
        while ((cnt > 0) && (len >= 4))
        {
            READ_AKM_SUITE();
            cnt--;
        }

        if (len < 2)
            return;

        rsn_capa = co_wtohs(co_read16p(sec_ie));
        sec_ie += 2;
        len -= 2;

        if (len >= 2)
        {
            READ_CNT();
            sec_ie += MAC_RSNIE_RSN_PMKID_SIZE * cnt;
            len -= MAC_RSNIE_RSN_PMKID_SIZE * cnt;
        }

        if (rsn_capa & (MAC_RSNIE_CAPA_MFPR_BIT | MAC_RSNIE_CAPA_MFPC_BIT))
        {
            if (len >= 4)
            {
                READ_CIPHER_SUITE(group_cipher);
            }
            else
            {
                res->group_cipher |= CO_BIT(MAC_CIPHER_BIP_CMAC_128);
            }
        }

        return;
    }

    // Else look for WPA Element
    sec_ie = mac_ie_wpa_find(ies, ies_len, &sec_ie_len);
    if (sec_ie)
    {
        res->akm = CO_BIT(MAC_AKM_PRE_RSN);

        sec_ie += MAC_WPA_GROUP_CIPHER_OFT;
        len = sec_ie_len + MAC_INFOELT_INFO_OFT -  MAC_WPA_GROUP_CIPHER_OFT;

        READ_CIPHER_SUITE(group_cipher);

        READ_CNT();
        while ((cnt > 0) && (len >= 4))
        {
            READ_CIPHER_SUITE(pairwise_cipher);
            cnt--;
        }

        if (len < 2)
            return;

        READ_CNT();
        while ((cnt > 0) && (len >= 4))
        {
            READ_AKM_SUITE();
            cnt--;
        }

        return;
    }

    #if 1//RW_WAPI_EN
    // Last try WAPI Element
    sec_ie = mac_ie_wapi_find(ies, ies_len, &sec_ie_len);
    if (sec_ie)
    {

        sec_ie += MAC_WAPI_AKM_SUITE_CNT_OFT;
        len = sec_ie_len + MAC_INFOELT_INFO_OFT - MAC_WAPI_AKM_SUITE_CNT_OFT;

        READ_CNT();
        while ((cnt > 0) && (len >= 4))
        {
            READ_AKM_SUITE();
            cnt--;
        }

        if (len < 2)
            return;

        READ_CNT();
        while ((cnt > 0) && (len >= 4))
        {
            READ_CIPHER_SUITE(pairwise_cipher);
            cnt--;
        }

        if (len < 4)
            return;

        READ_CIPHER_SUITE(group_cipher);

        return;
    }
    #endif // RW_WAPI_EN

    // No 'Security' Element, assume WEP
    res->akm = CO_BIT(MAC_AKM_PRE_RSN);
    res->group_cipher = CO_BIT(MAC_CIPHER_WEP40);

    #undef READ_CIPHER_SUITE
    #undef READ_AKM_SUITE
    #undef READ_CNT

}

struct mac_scan_result *scanu_find_result(struct mac_addr const *bssid_ptr,
                                          bool allocate)
{
    uint8_t i=0;
    struct mac_scan_result* scan_rslt = NULL;

    // search in the scan list using the MAC address
    for (i = 0; i < SCANU_MAX_RESULTS; i++)
    {
        struct mac_scan_result *scan = &scan_result[i];

        // if it is a valid BSS.
        if (scan->valid_flag)
        {
            if (MAC_ADDR_CMP(&scan->bssid, bssid_ptr))
            {
                // required BSS found
                scan_rslt = scan;
                break;
            }
        }
        else if (allocate)
        {
            scan_result[i].rssi = ~0x7F;
            scan_result[i].valid_flag = false;
            // empty entry: if allocation was requested, then return this pointer
            scan_rslt = scan;
            break;
        }
    }
    return (scan_rslt);
}

uint32_t mac_ie_ds_find(uint32_t buffer, uint16_t buflen)
{
    uint16_t len;
    uint32_t addr = mac_ie_find(buffer, buflen, MAC_ELTID_DS, &len);

    if ((addr == 0) || (len != MAC_DS_PARAM_LEN))
        return 0;

    return addr;
}

struct mac_chan_def *me_freq_to_chan_ptr(uint8_t band, uint16_t freq)
{
    int i, chan_cnt;
    struct mac_chan_def *chan;

    // Get the correct channel table
    chan = (band == PHY_BAND_2G4)?fhost_chan.chan2G4:fhost_chan.chan5G;
    chan_cnt = (band == PHY_BAND_2G4)?fhost_chan.chan2G4_cnt:fhost_chan.chan5G_cnt;

    for (i = 0; i < chan_cnt; i++)
    {
        if (chan[i].freq == freq)
            return &chan[i];
    }

    return NULL;
}

int fhost_scan_frame_handler(struct scanu_result_ind *res)
{
    struct mac_scan_result *scan;
    uint32_t elmt_addr, var_part_addr, var_part_len;
    struct bcn_frame const *frm = (struct bcn_frame const *)res->payload;
    uint8_t elmt_length;

    if ((0 != rssi_thold) && (res->rssi < rssi_thold)) {
        return 0;
    }

    do
    {
        struct mac_addr bssid = {0};

        memcpy(&bssid, &frm->h.addr3, sizeof(struct mac_addr));
        // find a scan result that has the same BSSID (or allocate it)
        scan = scanu_find_result(&bssid, true);
        if (scan == NULL)
            break;

        // copy the BSSID
        MAC_ADDR_CPY(&scan->bssid, &frm->h.addr3);

        // Retrieve the constant fields
        scan->beacon_period = frm->bcnint;
        scan->cap_info = frm->capa;

        // ESS or IBSS
        if ((scan->cap_info & MAC_CAPA_ESS) == MAC_CAPA_ESS)
        {
            scan->bsstype = INFRASTRUCTURE_MODE;
        }
        else
        {
            scan->bsstype = INDEPENDENT_BSS_MODE;
        }

        // Initialize the variable part address
        var_part_addr = CPU2HW(frm->variable);
        var_part_len = res->length - MAC_BEACON_VARIABLE_PART_OFT;

        // retrieve the SSID if broadcasted
        elmt_addr = mac_ie_ssid_find(var_part_addr, var_part_len, &elmt_length);
        if (elmt_addr != 0)
        {
            scan->ssid.length = elmt_length;
            // copy the SSID length
            co_unpack8p(scan->ssid.array, elmt_addr + MAC_SSID_SSID_OFT, elmt_length);
        }
        else
        {
            // SSID is not broadcasted
            scan->ssid.length = 0;
        }

        // retrieve the channel
        elmt_addr = mac_ie_ds_find(var_part_addr, var_part_len);
        if (elmt_addr != 0)
        {
            uint8_t ch_nbr = co_read8p(elmt_addr + MAC_DS_CHANNEL_OFT);
            scan->chan = me_freq_to_chan_ptr(res->band,
                                             phy_channel_to_freq(res->band, ch_nbr));
            // check if the RSSI of the received beacon is greater than the previous one
            if (res->rssi > scan->rssi)
            {
                scan->rssi = res->rssi;
            }
        }
        else
        {
            // check if the RSSI of the received beacon is greater than the previous one
            if (res->rssi > scan->rssi)
            {
                scan->chan = me_freq_to_chan_ptr(res->band, res->center_freq);
                scan->rssi = res->rssi;
            }
        }

        scanu_get_security_info(var_part_addr, var_part_len, scan);

        // check if the element was already allocated
        if (!scan->valid_flag)
        {
            // one more scan scan is saved
            scan_result_cnt++;
        }

        // set the valid_flag
        scan->valid_flag = true;

    } while(false);

    return 0;
}

void fhost_scan_start(void)
{
    int i;
    // reset the scan results before starting a new scan
    for (i = 0; i < SCANU_MAX_RESULTS; i++) {
        scan_result[i].valid_flag = false;
        scan_result[i].rssi = ~0x7F;
    }
    scan_result_cnt = 0;
}

int fhost_scan_for_ssid_pwd(struct fhost_cntrl_link *link, int fvif_idx, uint8_t *p_ssid, uint8_t *p_password)
{
    struct cfgrwnx_scan cmd;
    struct cfgrwnx_resp resp;
    struct cfgrwnx_scan_ssid ssid;
    struct cfgrwnx_msg_hdr msg_hdr;
    int nb_result = 0;
    static int16_t last_rssi = -90;
    int freq_list[1] = {0};

    freq_list[0] = get_sta_connect_chan_freq();

    ssid.len = 0;
    ssid.ssid = NULL;
    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = CFGRWNX_SCAN_CMD;
    cmd.fhost_vif_idx = fvif_idx;
    if(0 == freq_list[0]) {
        cmd.freqs = NULL;
    } else {
        cmd.freqs = freq_list;
    }
    cmd.extra_ies = NULL;
    cmd.bssid = NULL;
    cmd.ssids = &ssid;
    cmd.extra_ies_len = 0;
    cmd.no_cck = 0;
    cmd.ssid_cnt = 1;
    cmd.sock = link->sock_send;
    cmd.hdr.resp_queue = link->queue;

    resp.hdr.len = sizeof(resp);
    resp.hdr.id = CFGRWNX_SCAN_RESP;

    if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) ||
        (resp.status != CFGRWNX_SUCCESS))
        return -1;

    // Wait scan to complete
    while(1)
    {
        int len = recv(link->sock_recv, &msg_hdr, sizeof(msg_hdr), MSG_PEEK);
        if (len < 0)
            break;

        if (msg_hdr.id == CFGRWNX_SCAN_DONE_EVENT)
        {
            struct cfgrwnx_scan_completed res;
            len = recv(link->sock_recv, &res, sizeof(res), 0);
            if (len == sizeof(res))
            {
                if (res.status != CFGRWNX_SUCCESS)
                    return -1;
                nb_result = res.result_cnt;
            }
            break;
        }
        else
        {
            struct cfgrwnx_scan_result res;
            uint32_t elmt_addr;
            // re-read without MSG_PEEK to skip buffer
            recv(link->sock_recv, &res, sizeof(res), 0);
            if (res.payload) {
                uint16_t len;
                uint32_t vsie = mac_vsie_find((uint32_t)(res.payload+36), (res.length  - 36), (uint8_t const *)"\x41\x49\x43\x01", 4, &len);
                if ((vsie) && (last_rssi < res.rssi)) {
                    uint8_t ssid_len = 0;
                    struct bcn_frame const *frm = (struct bcn_frame const *)res.payload;
                    // copy the BSSID
                    snprintf((char *)p_password, 64, "%04X%04X", frm->h.addr3.array[1] + 1, frm->h.addr3.array[2] + 2);
                    // retrieve the SSID if broadcasted
                    elmt_addr = mac_ie_ssid_find((uint32_t)(res.payload+36), (res.length  - 36), &ssid_len);
                    if (elmt_addr != 0) {
                        // copy the SSID length
                        co_unpack8p(p_ssid, elmt_addr + MAC_SSID_SSID_OFT, ssid_len);
                    } else {
                        AIC_LOG_PRINTF(" SSID is not found \r\n");
                    }
                    last_rssi = res.rssi;
                    AIC_LOG_PRINTF("[%d]SSID %s,%s \r\n", last_rssi, p_ssid, p_password);
                }
                rtos_free(res.payload);
                res.payload = NULL;
            }
        }
    }
    last_rssi = -90;

    return nb_result;
}


int fhost_get_scan_results(struct fhost_cntrl_link *link, int result_idx,
                           int max_nb_result, struct mac_scan_result *results)
{
    int nb_res = 0;
    #if 0
    struct cfgrwnx_scan_results cmd;
    struct cfgrwnx_scan_results_resp resp;
    struct mac_scan_result *results_ptr = results;

    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = CFGRWNX_SCAN_RESULTS_CMD;
    cmd.hdr.resp_queue = link->queue;
    cmd.idx = result_idx;

    resp.hdr.len = sizeof(resp);
    resp.hdr.id = CFGRWNX_SCAN_RESULTS_RESP;

    while ((nb_res < max_nb_result) &&
           !fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) &&
           (resp.status == CFGRWNX_SUCCESS) &&
           resp.scan_result.valid_flag)
    {
        *results_ptr++ = resp.scan_result;
        nb_res++;
        cmd.idx++;
    }
    #else
    if ((result_idx < scan_result_cnt) && scan_result[result_idx].valid_flag) {
        *results = scan_result[result_idx];
        nb_res++;
    }
    #endif

    return nb_res;
}
int fhost_sta_cfg(int fhost_vif_idx, struct fhost_vif_sta_cfg *cfg)
{
    struct fhost_vif_tag *fhost_vif;
    char *cfg_str, *ptr;
    int res, cfg_str_len = 256;
    int key_len;

    if ((fhost_vif_idx >= NX_VIRT_DEV_MAX) || (cfg == NULL))
        return -1;

    fhost_vif = &fhost_env.vif[fhost_vif_idx];

    if ((!fhost_vif->mac_vif) || (fhost_vif->mac_vif->type != VIF_STA))
        return -1;

    cfg_str = rtos_malloc(cfg_str_len + 1);
    if (!cfg_str)
        return -1;
    ptr = cfg_str;

    // SSID
    if (cfg->ssid.length < sizeof(cfg->ssid.array))
    {
        cfg->ssid.array[cfg->ssid.length] = '\0';
        res = dbg_snprintf(ptr, cfg_str_len, "ssid \"%s\";", cfg->ssid.array);
    }
    else
    {
        // Corner case, SSID takes all the place cannot put terminating NULL byte
        char c = cfg->ssid.array[sizeof(cfg->ssid.array) - 1];
        cfg->ssid.array[sizeof(cfg->ssid.array) - 1] = '\0';
        res = dbg_snprintf(ptr, cfg_str_len, "ssid \"%s%c\";", cfg->ssid.array, c);
    }

    if (res >= cfg_str_len)
        goto end;

    ptr += res;
    cfg_str_len -= res;

    // AKM
    key_len = strlen(cfg->key);
    if (!cfg->akm)
    {
        if (key_len < 8)
        {
            // If key is less than 8, assume WEP key
            res = dbg_snprintf(ptr, cfg_str_len, "key_mgmt NONE;");
            cfg->akm = CO_BIT(MAC_AKM_NONE);
        }
        else
        {
            #if NX_CRYPTOLIB
            res = dbg_snprintf(ptr, cfg_str_len, "key_mgmt WPA-PSK SAE;");
            #else
            res = dbg_snprintf(ptr, cfg_str_len, "key_mgmt WPA-PSK;");
            #endif
            cfg->akm = CO_BIT(MAC_AKM_PSK);
        }
        if (res >= cfg_str_len)
            goto end;

        ptr += res;
        cfg_str_len -= res;
    }
    else
    {
        // remove unsupported AKM (so far only PSK is supported by fhost firmware)
        uint32_t akm_supported = CO_BIT(MAC_AKM_NONE) | CO_BIT(MAC_AKM_PSK);
        #if NX_CRYPTOLIB
        akm_supported |= CO_BIT(MAC_AKM_SAE);
        #endif

        cfg->akm &= akm_supported;
        if (cfg->akm == 0)
        {
            res = -1;
            goto end;
        }

        res = dbg_snprintf(ptr, cfg_str_len, "key_mgmt %s%s%s;",
                           (cfg->akm & CO_BIT(MAC_AKM_NONE)) ? "NONE " : "",
                           (cfg->akm & CO_BIT(MAC_AKM_PSK)) ? "WPA-PSK " : "",
                           (cfg->akm & CO_BIT(MAC_AKM_SAE)) ? "SAE " : "");
        if (res >= cfg_str_len)
            goto end;

        ptr += res;
        cfg_str_len -= res;
    }

    // Keys
    if (key_len > 0)
    {
        if ((cfg->akm & CO_BIT(MAC_AKM_NONE)) && (key_len == 5 || key_len == 13 || key_len == 16))
        {
            // WEP keys
            res = dbg_snprintf(ptr, cfg_str_len, "wep_key0 \"%s\";auth_alg OPEN SHARED;", cfg->key);
        }
        else
        {
            // PSK (works also for SAE)
            res = dbg_snprintf(ptr, cfg_str_len, "psk \"%s\";", cfg->key);
        }
        if (res >= cfg_str_len)
            goto end;

        ptr += res;
        cfg_str_len -= res;

        #if NX_MFP
        // Always try to use MFP
        res = dbg_snprintf(ptr, cfg_str_len, "ieee80211w 1;");
        if (res >= cfg_str_len)
            goto end;

        ptr += res;
        cfg_str_len -= res;
        #endif
    }

    // BSSID (optional)
    if (cfg->bssid.array[0] || cfg->bssid.array[1] || cfg->bssid.array[2])
    {
        res = dbg_snprintf(ptr, cfg_str_len, "bssid %02x:%02x:%02x:%02x:%02x:%02x;",
                           ((uint8_t *)cfg->bssid.array)[0], ((uint8_t *)cfg->bssid.array)[1],
                           ((uint8_t *)cfg->bssid.array)[2], ((uint8_t *)cfg->bssid.array)[3],
                           ((uint8_t *)cfg->bssid.array)[4], ((uint8_t *)cfg->bssid.array)[5]);
        if (res >= cfg_str_len)
            goto end;

        ptr += res;
        cfg_str_len -= res;
    }

    // Frequencies (optional)
    if (cfg->freq[0])
    {
        unsigned int i, j;

        // silently remove invalid frequencies
        for (i = 0, j = 0; i < CO_ARRAY_SIZE(cfg->freq); i++)
        {
            if (cfg->freq[i] == 0)
                break;
            if (fhost_chan_get(cfg->freq[i]))
            {
                if (j != i)
                    cfg->freq[j] = cfg->freq[i];
                j++;
            }
        }

        if (j > 0)
        {
            res = dbg_snprintf(ptr, cfg_str_len, "scan_freq ");
            if (res >= cfg_str_len)
                goto end;
            ptr += res;
            cfg_str_len -= res;

            while (j > 0)
            {
                j--;
                res = dbg_snprintf(ptr, cfg_str_len, "%d ", cfg->freq[j]);
                if (res >= cfg_str_len)
                    goto end;
                ptr += res;
                cfg_str_len -= res;
            }

            res = dbg_snprintf(ptr, cfg_str_len, ";");
            if (res >= cfg_str_len)
                goto end;
            ptr += res;
            cfg_str_len -= res;
        }
    }
    res = fhost_wpa_create_network(fhost_vif_idx, cfg_str, true, cfg->timeout_ms);

    extern bool time_out_flag;
    if ((res == -1) && (time_out_flag)) {
        time_out_flag = false;
    }
  end:
    if (res > 0)
    {
        AIC_LOG_PRINTF("Missing at least %d character for wpa_supplicant config",
                    res - cfg_str_len);
        res = -1;
    }
    rtos_free(cfg_str);
    return res;
}

#ifdef CFG_P2P
int fhost_p2p_cfg(int fhost_vif_idx, struct fhost_vif_p2p_cfg *cfg)
{
	struct fhost_vif_tag *fhost_vif;
    char *cfg_str, *ptr;
    int res, cfg_str_len = 256;
	struct mac_chan_def *chan = NULL;

	if ((fhost_vif_idx >= NX_VIRT_DEV_MAX) || (cfg == NULL))
        return -1;

    fhost_vif = &fhost_env.vif[fhost_vif_idx];

    if ((!fhost_vif->mac_vif) || (fhost_vif->mac_vif->type != VIF_AP))
        return -1;

    cfg_str = rtos_malloc(cfg_str_len + 1);
    if (!cfg_str)
        return -1;

    memset(cfg_str, '\0', cfg_str_len);
    ptr = cfg_str;

	// SSID
    if (cfg->ssid.length < sizeof(cfg->ssid.array))
    {
        cfg->ssid.array[cfg->ssid.length] = '\0';
        res = dbg_snprintf(ptr, cfg_str_len, "ssid \"%s\";", cfg->ssid.array);
    }
    else
    {
        // Corner case, SSID takes all the place cannot put terminating NULL byte
        char c = cfg->ssid.array[sizeof(cfg->ssid.array) - 1];
        cfg->ssid.array[sizeof(cfg->ssid.array) - 1] = '\0';
        res = dbg_snprintf(ptr, cfg_str_len, "ssid \"%s%c\";", cfg->ssid.array, c);
    }

    if (res >= cfg_str_len)
        goto end;
    ptr += res;
    cfg_str_len -= res;

    // Operating Channel
    if (!cfg->enable_acs) {
        chan = fhost_chan_get(cfg->chan.prim20_freq);
        if (!chan || (chan->flags & (CHAN_NO_IR | CHAN_DISABLED | CHAN_RADAR)))
            goto end;

        res = dbg_snprintf(ptr, cfg_str_len, "frequency %d;", cfg->chan.prim20_freq);
        if (res >= cfg_str_len)
            goto end;
        ptr += res;
        cfg_str_len -= res;
    } else {
        if (cfg->chan.band == PHY_BAND_5G)
            res = dbg_snprintf(ptr, cfg_str_len, "frequency 5180;");
        else
            res = dbg_snprintf(ptr, cfg_str_len, "frequency 2412;");
        if (res >= cfg_str_len)
            goto end;
        ptr += res;
        cfg_str_len -= res;
    }

    #if NX_VHT
    //if (cfg->chan.band == PHY_BAND_5G)
    {
        res = dbg_snprintf(ptr, cfg_str_len,
                           "vht 1;");
        if (res >= cfg_str_len)
            goto end;
        ptr += res;
        cfg_str_len -= res;
    }
    #endif

    #if NX_HE
    if (cfg->enable_he)
    {
        res = dbg_snprintf(ptr, cfg_str_len,
                           "he 1;");
        if (res >= cfg_str_len)
            goto end;
        ptr += res;
        cfg_str_len -= res;
    }
    #endif

    if (cfg->chan.type >= PHY_CHNL_BW_40)
    {
        int freq_offset = cfg->chan.center1_freq - cfg->chan.prim20_freq;
        int ht40_offset = 1;
        if ((freq_offset == -10) | (freq_offset == 30))
            ht40_offset = -1;
        res = dbg_snprintf(ptr, cfg_str_len, "ht40 %d;", ht40_offset);
        if (res >= cfg_str_len)
            goto end;
        ptr += res;
        cfg_str_len -= res;

        #if NX_VHT
        if (cfg->chan.type == PHY_CHNL_BW_80)
        {
            res = dbg_snprintf(ptr, cfg_str_len,
                               "max_oper_chwidth 1;vht_center_freq1 %d;",
                               cfg->chan.center1_freq);
            if (res >= cfg_str_len)
                goto end;
            ptr += res;
            cfg_str_len -= res;
        }
        else if (cfg->chan.type > PHY_CHNL_BW_80)
            // not supported
            goto end;
        #endif
    }

    if (cfg->enable_acs) {
        res = dbg_snprintf(ptr, cfg_str_len, "acs %d;", cfg->enable_acs);
        if (res >= cfg_str_len)
            goto end;
        ptr += res;
        cfg_str_len -= res;
    }

    res = (fhost_wpa_create_network(fhost_vif_idx, cfg_str, false, 0));
#ifdef CONFIG_VENDOR_IE
    char *set_vendor_ie_cmd = rtos_malloc(sizeof("SET ap_vendor_elements ") + strlen(cfg->vendor_ie));
    if (set_vendor_ie_cmd) {
        memset(set_vendor_ie_cmd, 0, (sizeof("SET ap_vendor_elements ") + strlen(cfg->vendor_ie)));
        strcpy(set_vendor_ie_cmd, "SET ap_vendor_elements ");
        strcat(set_vendor_ie_cmd, cfg->vendor_ie);
        fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 300, set_vendor_ie_cmd);
        rtos_free(set_vendor_ie_cmd);
    }
#endif

    if (res) 
        fhost_wpa_end(fhost_vif_idx);

end:
    if (res > 0)
    {
        res = -1;
    }
    rtos_free(cfg_str);
    return res;
}
#endif

#if NX_BEACONING
int fhost_ap_cfg(int fhost_vif_idx, struct fhost_vif_ap_cfg *cfg)
{
    struct fhost_vif_tag *fhost_vif;
    struct mac_chan_def *chan = NULL;
    char *cfg_str, *ptr;
    int res, cfg_str_len = 320;
    int key_len;
    uint32_t akm, group, pairwise;

    if ((fhost_vif_idx >= NX_VIRT_DEV_MAX) || (cfg == NULL))
        return -1;

    fhost_vif = &fhost_env.vif[fhost_vif_idx];

    if ((!fhost_vif->mac_vif) || (fhost_vif->mac_vif->type != VIF_AP))
        return -1;

    cfg_str = rtos_malloc(cfg_str_len + 1);
    if (!cfg_str)
        return -1;

    memset(cfg_str, '\0', cfg_str_len);
    ptr = cfg_str;

    // Enable AP mode
    res = dbg_snprintf(ptr, cfg_str_len, "mode 2;");
    ptr += res;
    cfg_str_len -= res;

    // SSID
    if (cfg->ssid.length < sizeof(cfg->ssid.array))
    {
        cfg->ssid.array[cfg->ssid.length] = '\0';
        res = dbg_snprintf(ptr, cfg_str_len, "ssid \"%s\";", cfg->ssid.array);
    }
    else
    {
        // Corner case, SSID takes all the place cannot put terminating NULL byte
        char c = cfg->ssid.array[sizeof(cfg->ssid.array) - 1];
        cfg->ssid.array[sizeof(cfg->ssid.array) - 1] = '\0';
        res = dbg_snprintf(ptr, cfg_str_len, "ssid \"%s%c\";", cfg->ssid.array, c);
    }

    if (res >= cfg_str_len)
        goto end;
    ptr += res;
    cfg_str_len -= res;

    // Operating Channel
    if (!cfg->enable_acs) {
        chan = fhost_chan_get(cfg->chan.prim20_freq);
        #ifndef CONFIG_RWNX_RADAR
        if (!chan || (chan->flags & (CHAN_NO_IR | CHAN_DISABLED | CHAN_RADAR)))
            goto end;
        #else
        if (!chan)
            goto end;
        if (chan->flags & (CHAN_NO_IR | CHAN_DISABLED | CHAN_RADAR)) {
            cfg->chan.type = PHY_CHNL_BW_20; //20M only
        }
        #endif /* !CONFIG_RWNX_RADAR */
        res = dbg_snprintf(ptr, cfg_str_len, "frequency %d;", cfg->chan.prim20_freq);
        if (res >= cfg_str_len)
            goto end;
        ptr += res;
        cfg_str_len -= res;
    } else {
        if (cfg->chan.band == PHY_BAND_5G)
            res = dbg_snprintf(ptr, cfg_str_len, "frequency 5180;");
        else
            res = dbg_snprintf(ptr, cfg_str_len, "frequency 2412;");
        if (res >= cfg_str_len)
            goto end;
        ptr += res;
        cfg_str_len -= res;
    }

    #if NX_VHT
    //if (cfg->chan.band == PHY_BAND_5G)
    {
        res = dbg_snprintf(ptr, cfg_str_len,
                           "vht 1;");
        if (res >= cfg_str_len)
            goto end;
        ptr += res;
        cfg_str_len -= res;
    }
    #endif

    #if NX_HE
    if (cfg->enable_he)
    {
        res = dbg_snprintf(ptr, cfg_str_len,
                           "he 1;");
        if (res >= cfg_str_len)
            goto end;
        ptr += res;
        cfg_str_len -= res;
    }
    #endif

    if (cfg->chan.type >= PHY_CHNL_BW_40)
    {
        int freq_offset = cfg->chan.center1_freq - cfg->chan.prim20_freq;
        int ht40_offset = 1;
        if (((cfg->chan.type == PHY_CHNL_BW_40) && (freq_offset == -10)) ||
            ((cfg->chan.type == PHY_CHNL_BW_80) && ((freq_offset == 10) || (freq_offset == -30)))) {
            ht40_offset = -1;
        }
        res = dbg_snprintf(ptr, cfg_str_len, "ht40 %d;", ht40_offset);
        if (res >= cfg_str_len)
            goto end;
        ptr += res;
        cfg_str_len -= res;

        #if NX_VHT
        if (cfg->chan.type == PHY_CHNL_BW_80)
        {
            res = dbg_snprintf(ptr, cfg_str_len,
                               "max_oper_chwidth 1;vht_center_freq1 %d;",
                               cfg->chan.center1_freq);
            if (res >= cfg_str_len)
                goto end;
            ptr += res;
            cfg_str_len -= res;
        }
        else if (cfg->chan.type > PHY_CHNL_BW_80)
            // not supported
            goto end;
        #endif
    }

    // Beacon
    if (cfg->bcn_interval == 0)
        cfg->bcn_interval = 100;
    else if (cfg->bcn_interval < 15)
        cfg->bcn_interval = 15;
    if (cfg->dtim_period < 1)
        cfg->dtim_period = 1;
    res = dbg_snprintf(ptr, cfg_str_len, "beacon_int %d;dtim_period %d;",
                       cfg->bcn_interval, cfg->dtim_period);
    if (res >= cfg_str_len)
        goto end;
    ptr += res;
    cfg_str_len -= res;

    // AKM (remove unsupported ones)
    akm = cfg->akm & (CO_BIT(MAC_AKM_PSK) |
                      CO_BIT(MAC_AKM_PRE_RSN) |
                      #if NX_CRYPTOLIB
                      CO_BIT(MAC_AKM_SAE) |
                      #endif
                      CO_BIT(MAC_AKM_NONE));
    if (!akm)
        goto end;

    key_len = strlen(cfg->key);
    if (akm & CO_BIT(MAC_AKM_NONE))
    {
        if (cfg->akm & ~CO_BIT(MAC_AKM_NONE))
            goto end;
        res = dbg_snprintf(ptr, cfg_str_len, "key_mgmt NONE;");

        if (res >= cfg_str_len)
            goto end;
        ptr += res;
        cfg_str_len -= res;
    }
    else if (akm & CO_BIT(MAC_AKM_PRE_RSN))
    {
        if ((akm & CO_BIT(MAC_AKM_PSK)) ||
            (cfg->unicast_cipher & MAC_CIPHER_TKIP))
        {
            if (key_len < 8)
                goto end;

            // WEP is no longer allowed for group cipher so always use TKIP
            res = dbg_snprintf(ptr, cfg_str_len, "proto WPA;key_mgmt WPA-PSK;"
                               "pairwise TKIP;group TKIP;psk \"%s\";", cfg->key);
        }
        else if ((key_len == 5) || (key_len == 13))
        {
            res = dbg_snprintf(ptr, cfg_str_len, "key_mgmt NONE;"
                               "wep_key0 \"%s\";wep_tx_keyidx 0;",  cfg->key);
        }
        else
            goto end;

        if (res >= cfg_str_len)
            goto end;
        ptr += res;
        cfg_str_len -= res;}
    else
    {
        if (key_len < 8)
            goto end;

        // Remove unsupported cipher or set default value if not set
        if (cfg->unicast_cipher)
        {
            pairwise = cfg->unicast_cipher & (CO_BIT(MAC_CIPHER_TKIP) | CO_BIT(MAC_CIPHER_CCMP));
            if (!pairwise)
                goto end;
        }
        else
            pairwise = CO_BIT(MAC_CIPHER_CCMP);

        if (cfg->group_cipher)
        {
            group = cfg->group_cipher & (CO_BIT(MAC_CIPHER_TKIP) | CO_BIT(MAC_CIPHER_CCMP));
            if (!group)
                goto end;
        }
        else
            group = CO_BIT(MAC_CIPHER_CCMP);

        res = dbg_snprintf(ptr, cfg_str_len, "proto RSN;key_mgmt %s%s;pairwise %s%s;group %s;psk \"%s\";",
                           (akm & CO_BIT(MAC_AKM_PSK)) ? "WPA-PSK " : "",
                           (akm & CO_BIT(MAC_AKM_SAE)) ? "SAE " : "",
                           (pairwise & CO_BIT(MAC_CIPHER_CCMP)) ? "CCMP " : "",
                           (pairwise & CO_BIT(MAC_CIPHER_TKIP)) ? "TKIP" : "",
                           (group & CO_BIT(MAC_CIPHER_TKIP)) ? "TKIP" : "CCMP",
                           cfg->key);
        if (res >= cfg_str_len)
            goto end;
        ptr += res;
        cfg_str_len -= res;

        #if NX_MFP
        if (cfg->mfp < 0 || cfg->mfp > 2)
            goto end;

        res = dbg_snprintf(ptr, cfg_str_len, "ieee80211w %d;", cfg->mfp);
        if (res >= cfg_str_len)
            goto end;
        ptr += res;
        cfg_str_len -= res;
        #endif
    }


    if (cfg->hidden_ssid < 0 || cfg->hidden_ssid > 2)
        goto end;
    res = dbg_snprintf(ptr, cfg_str_len, "ignore_broadcast_ssid %d;", cfg->hidden_ssid);
    if (res >= cfg_str_len)
        goto end;
    ptr += res;
    cfg_str_len -= res;

    if (cfg->max_inactivity == 0)
        cfg->max_inactivity = 60;
    res = dbg_snprintf(ptr, cfg_str_len, "ap_max_inactivity %d;", cfg->max_inactivity);
    if (res >= cfg_str_len)
        goto end;
    ptr += res;
    cfg_str_len -= res;

    if (cfg->enable_acs) {
        res = dbg_snprintf(ptr, cfg_str_len, "acs %d;", cfg->enable_acs);
        if (res >= cfg_str_len)
            goto end;
        ptr += res;
        cfg_str_len -= res;
    }

    if (cfg->sta_num > 0) {
        aic_dbg("fhost_set_max_sta_num %d\r\n", cfg->sta_num);
    }
    
    res = (fhost_wpa_create_network(fhost_vif_idx, cfg_str, false, 0) |
           fhost_wpa_execute_cmd(fhost_vif_idx, NULL, 0, 300, "AP_SCAN 2") |
           fhost_set_max_sta_num(fhost_vif_idx, cfg->sta_num));
    
    if (strcmp(current_country_code, "00")) {
        char cmd[16];
        dbg_snprintf(cmd, 16, "SET country %s", current_country_code);
        res |= fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 300, cmd);
        aic_dbg("fhost_set_max_sta_num %d\r\n", cfg->sta_num);
    }

    #ifdef CONFIG_VENDOR_IE
    #define VENDOR_IE "dd0800A0400000020022"
    if (cfg->vendor_ie) {
        char *set_vendor_ie_cmd = rtos_malloc(sizeof("SET ap_vendor_elements ") + strlen(cfg->vendor_ie));
        aic_dbg("vendor_ie %d\r\n", (sizeof("SET ap_vendor_elements ") + strlen(cfg->vendor_ie)));
        if (set_vendor_ie_cmd) {
            strcpy(set_vendor_ie_cmd, "SET ap_vendor_elements ");
            strcat(set_vendor_ie_cmd, cfg->vendor_ie);
            res |= fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 300, set_vendor_ie_cmd);
            rtos_free(set_vendor_ie_cmd);
       }
    }
    #endif
    res |= fhost_wpa_enable_network(fhost_vif_idx, 100000);

    if (res)
        fhost_wpa_end(fhost_vif_idx);

  end:
    if (res > 0)
    {
        if (res >= cfg_str_len)
        {
            AIC_LOG_PRINTF("Missing at least %d character for wpa_supplicant config (AP)",
                        res - cfg_str_len);
        }
        else
        {
            AIC_LOG_PRINTF("Invalid AP config: chan_freq=%d chan_flags=%x "
                        "akm=%08x unicast=%08x group=%08x key_len=%d\n",
                        cfg->chan.prim20_freq, (chan) ? chan->flags : 0xffff,
                        TR_32(cfg->akm), TR_32(cfg->unicast_cipher),
                        TR_32(cfg->group_cipher), strlen(cfg->key));
        }
        res = -1;
    }
    rtos_free(cfg_str);
    return res;
}
#endif

fhost_mac_status_get_func_t fhost_mac_status_get_callback = NULL;

void fhost_get_mac_status_register(fhost_mac_status_get_func_t func)
{
    fhost_mac_status_get_callback = func;
}

fhost_mac_status_get_func_t fhost_reconnect_dhcp_callback = NULL;

void fhost_reconnect_dhcp_register(fhost_mac_status_get_func_t func)
{
    fhost_reconnect_dhcp_callback = func;
}

struct fhost_vif_tag *fhost_from_mac_vif(uint8_t mac_vif_idx)
{
    struct fhost_vif_tag *fhost_vif;

    // Sanity check - Check that mac_vif_idx is valid
    ASSERT_ERR(mac_vif_idx < NX_VIRT_DEV_MAX);

    fhost_vif = fhost_env.mac2fhost_vif[mac_vif_idx];

    // Sanity check - Currently we consider that when this function is called there shall
    // be a FHOST VIF attached to the MAC VIF. If in the future this has to change then
    // this assertion will be removed
    ASSERT_ERR(fhost_vif != NULL);

    return fhost_vif;
}
net_if_t *fhost_to_net_if(uint8_t fhost_vif_idx)
{
    ASSERT_ERR(fhost_vif_idx < NX_VIRT_DEV_MAX);
    return &(fhost_env.vif[fhost_vif_idx].net_if);
}

uint8_t fhost_vif_get_staid(int fhost_vif, struct mac_addr *dst_addr)
{
    uint8_t sta_id = INVALID_STA_IDX;
    struct fhost_vif_tag *vif = &fhost_env.vif[fhost_vif];
    switch (vif->mac_vif->type)
    {
        case VIF_STA:
            if (vif->mac_vif->active)
                sta_id = vif->ap_id;
            break;
        case VIF_AP:
            if (MAC_ADDR_GROUP(dst_addr))
                sta_id = VIF_TO_BCMC_IDX(vif->mac_vif->index);
            else {
                sta_id = vif_mgmt_get_staid(vif->mac_vif, dst_addr);
            }
            break;
        default:
            break;
    }
    return sta_id;
}

/// @}

