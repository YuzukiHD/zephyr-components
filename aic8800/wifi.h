#ifndef _WIFI_H_
#define _WIFI_H_

//#include "lwip/netif.h"
//#include "lwip/netifapi.h"
#include "co_int.h"
#if defined(PLATFORM_ALLWIN_RT_THREAD) || defined(PLATFORM_ALLWIN_FREERTOS)
#endif

#define AIC_MAC_ADDR_LEN    6
#define AIC_MAX_SSID_LEN    32
#define AIC_MAX_PASSWD_LEN  64
#define AIC_MIN_KEY_LEN     8
#define AIC_MAX_KEY_LEN     64
#define AIC_MAX_AP_COUNT    20
#define AIC_MAX_STA_COUNT   4

#if defined CONFIG_VENDOR_IE
#define DEFAULT_VENDOR_IE "dd0800A0400000020022"
#define AIC_DEV_OUI 0x00A040 //APPLE specific ie, indication of carplay app support
#endif

#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

#define TRACE_IN() printf("[%s][%d] in\n",  __FUNCTION__, __LINE__)
#define TRACE_OUT() printf("[%s][%d] out\n",__FUNCTION__, __LINE__)

typedef enum
{
    WIFI_MODE_STA = 0,
    WIFI_MODE_AP,
    WIFI_MODE_P2P,
    WIFI_MODE_RFTEST,
    WIFI_MODE_UNKNOWN
}AIC_WIFI_MODE;

typedef enum {
    PRODUCT_ID_AIC8801 = 0,
    PRODUCT_ID_AIC8800DC,
    PRODUCT_ID_AIC8800DW,
    PRODUCT_ID_AIC8800D80,
    PRODUCT_ID_AIC8800D81,
}AICWF_IC;

typedef enum
{
    KEY_NONE = 0, KEY_WEP, KEY_WPA, KEY_WPA2, KEY_WPA3, KEY_MAX_VALUE
} AIC_WIFI_SECURITY_TYPE;

struct ap_ssid
{
    /// Actual length of the SSID.
    unsigned char length;
    /// Array containing the SSID name.
    unsigned char array[AIC_MAX_SSID_LEN];
};

struct ap_passwd
{
    unsigned char length;
    unsigned char array[AIC_MAX_PASSWD_LEN];
};

struct aic_ap_cfg
{
    struct ap_ssid aic_ap_ssid;
    /**
     * Password: if OPEN, set length to 0
     */
    struct ap_passwd aic_ap_passwd;
    /**
     * Band : 0 -> 2.4G, 1 -> 5G
     */
    unsigned char band;
    /**
     * Type : 0 -> 20M, 1 -> 40M
     */
    unsigned char type;
    /**
     * Channel Number : 2.4G (1 ~ 13), 5G (36/40/44/48/149/153/157/161/165)
     */
    unsigned char channel;
    /**
     * Hidden ssid : 0 -> no hidden, 1 -> zero length, 2 -> zero contents
     */
    unsigned char hidden_ssid;
    /**
     * Value for maximum station inactivity, seconds
     */
    unsigned int max_inactivity;
    /**
     * Enable wifi6
     */
    unsigned char enable_he;
    /**
     * Enable ACS
     */
    unsigned char enable_acs;
    /**
     * Beacon interval
     */
    unsigned char bcn_interval;

    AIC_WIFI_SECURITY_TYPE sercurity_type;
    /**
     * STA number: max(NX_REMOTE_STA_MAX)
     */
    unsigned char sta_num;
    /**
     * ACS scan num
     */
    unsigned char acs_scan_num;
};

struct aic_sta_cfg
{
    struct ap_ssid aic_ap_ssid;
    /**
     * Password: if OPEN, set length to 0
     */
    struct ap_passwd aic_ap_passwd;
};

struct aic_p2p_cfg
{
    struct ap_ssid aic_p2p_ssid;
    /**
     * Password: if OPEN, set length to 0
     */
    struct ap_passwd aic_ap_passwd;
    /**
     * Band : 0 -> 2.4G, 1 -> 5G
     */
    unsigned char band;
    /**
     * Type : 0 -> 20M, 1 -> 40M
     */
    unsigned char type;
    /**
     * Channel Number : 2.4G (1 ~ 13), 5G (36/40/44/48/149/153/157/161/165)
     */
    unsigned char channel;
    /**
     * Enable wifi6
     */
    unsigned char enable_he;
    /**
     * Enable ACS
     */
    unsigned char enable_acs;
    /**
     * ACS scan num
     */
    unsigned char acs_scan_num;
};


typedef enum
{
    SCAN_RESULT_EVENT = 0,
    SCAN_DONE_EVENT,        // sta scan done
    JOIN_SUCCESS_EVENT,     // sta eapol success
    JOIN_FAIL_EVENT,
    LEAVE_RESULT_EVENT,
    ASSOC_IND_EVENT,        // as ap role, sta assoc ind
    DISASSOC_STA_IND_EVENT, // as ap role, sta disassoc ind
    DISASSOC_P2P_IND_EVENT,// as p2p GO role, peer disassoc ind
    EAPOL_STA_FIN_EVENT,   // as ap role, sta eapol success
    EAPOL_P2P_FIN_EVENT,  // as p2p GO  role, peer eapol success
    PRO_DISC_REQ_EVENT,
    STA_DISCONNECT_EVENT, // sta disconnect
    STA_ASSOC_EVENT,      // sta assoc success
    AP_STARTED_EVENT,     // as ap role, start success
    AP_STOPED_EVENT,      // as ap role, stop success
    GO_STARTED_EVENT,     // as p2p GO role, start success
    GO_REMOVED_EVENT,     // as p2p GO role, stop success
    AIC_WIFI_EVENT_MAX
} AIC_WIFI_EVENT;


typedef struct _aic_wifi_scan_result_data
{
    unsigned int  reserved[16];
}aic_wifi_scan_result_data;

typedef struct _aic_wifi_scan_done_data
{
    unsigned int  reserved[1];
}aic_wifi_scan_done_data;

typedef struct _aic_wifi_join_data
{
    unsigned int  reserved[1];
}aic_wifi_join_data;

typedef struct _aic_wifi_leave_data
{
    unsigned int  reserved[1];
}aic_wifi_leave_data;

typedef struct _aic_wifi_auth_deauth_data
{
    unsigned int  reserved[6];
}aic_wifi_auth_deauth_data;

typedef struct _aic_wifi_ap_status_data
{
    unsigned int  reserved[2];
} aic_wifi_ap_status_data;

typedef struct _aic_wifi_event_data
{
    union
    {
        struct _aic_wifi_scan_result_data  scan_result_data;
        struct _aic_wifi_scan_done_data  scan_done_data;
        struct _aic_wifi_join_data  join_data;
        struct _aic_wifi_leave_data  leave_data;
        struct _aic_wifi_auth_deauth_data  auth_deauth_data;
        struct _aic_wifi_ap_status_data  ap_go_status_data;
    }data;
    #ifdef CONFIG_P2P
    unsigned int p2p_enabled;
    unsigned int p2p_dev_port_num;
    #endif
}aic_wifi_event_data;


/* Supported authentication mode. */
/* Values are used to select the authentication mode used to join a network. */
enum {
    WLAN_WPA_AUTH_DISABLED = 0x0000,    /* Legacy (i.e., non-WPA) */
    WLAN_WPA_AUTH_NONE = 0x0001,        /* none (IBSS) */
    WLAN_WPA_AUTH_PSK = 0x0004,        /* Pre-shared key */
    WLAN_WPA2_AUTH_PSK = 0x0080,        /* Pre-shared key */
    WLAN_MIX_AUTH_PSK = 0x0100,            /* Pre-shared key */
};


/* WLAN Security Encryption. */
/* Values are used to select the type of encryption used for testing. */
enum {
    WLAN_ENCRYPT_NONE = 0,    /* No encryption. */
    WLAN_ENCRYPT_WEP = 1,     /* WEP encryption. */
    WLAN_ENCRYPT_TKIP = 2,    /* TKIP encryption. */
    WLAN_ENCRYPT_AES = 4,     /* AES encryption. */
    WLAN_ENCRYPT_WSEC = 8,    /* Software WSEC encryption. */
    WLAN_ENCRYPT_WEP_SHARED = 0x11,      /* WEP shard encryption. */
    WLAN_ENCRYPT_FIPS = 0x80,  /* FIPS encryption. */
};

typedef int (* wifi_event_handle)(void *enData);

typedef void (* aic_wifi_event_cb)(AIC_WIFI_EVENT enEvent, aic_wifi_event_data *enData);

char *aic_wifi_get_version(void);
AIC_WIFI_MODE aic_wifi_get_mode(void);
void aic_wifi_set_mode(AIC_WIFI_MODE mode);

void aicwf_get_chipid(void);
unsigned int aicwf_is_5g_enable(void);
void aic_wifi_event_register(aic_wifi_event_cb cb);

int aic_wifi_init_mac(void);

/**
 * @brief       Init wifi
 * @param[in]   mode: wifi mode, can be sta/ap/rftest
 * @param[in]   param: sta/ap cfg info
 */
extern int aic_wifi_init(int mode, int chip_id, void *param);

/**
 * @brief       De-init wifi
 * @param[in]   mode: wifi mode, can be sta/ap/rftest
 */
void aic_wifi_deinit(int mode);

extern struct wpabuf* aic_build_vendor_specific_ie(void);

int user_p2p_start(struct aic_p2p_cfg *user_p2p_cfg);

void aic_add_custom_ie (char* vendor_ie);
void aic_update_custom_ie (char* vendor_ie);
void aic_del_custom_ie (void);
int user_wps_button_pushed(void);
/*
 eg.
    unsigned int p_chan_list[] = {1,6,11,36,44,141};
    aic_set_pref_chan_list(p_chan_list, 6);
*/
void aic_set_pref_chan_list(unsigned int chan_list[], unsigned int num);

void aic_wifi_event_handler_register(AIC_WIFI_EVENT enEvent, wifi_event_handle cb);

typedef enum {
    AIC_WIFI_WDT_INIT               = 0,
    AIC_WIFI_WDT_KICK              = 1,
    AIC_WIFI_WDT_PAUSE          = 2,
    AIC_WIFI_WDT_CONTINUE = 3,
    AIC_WIFI_WDT_STOP             = 4
} _AIC_WIFI_WDT_CMD_;
void aic_wifi_wdt(uint8_t cmd, uint32_t seconds);

struct aicwf_cs_info {
    uint8_t phymode;
    uint8_t bandwidth;
    uint16_t freq;

    int8_t rssi;
    int8_t snr;
    int8_t noise;
    uint8_t txpwr;

    //chanutil
    uint16_t chan_time_ms;
    uint16_t chan_time_busy_ms;

    char countrycode[4];
    uint8_t rxnss;
    uint8_t rxmcs;
    uint8_t txnss;
    uint8_t txmcs;

    uint32_t tx_phyrate;
    uint32_t rx_phyrate;

    uint32_t tx_ack_succ_stat;
    uint32_t tx_ack_fail_stat;
    uint16_t chan_tx_time_busy_ms;
};
#endif
