/**
 ****************************************************************************************
 *
 * @file fhost_config.c
 *
 * @brief Definition of configuration for Fully Hosted firmware.
 *
 * Copyright (C) RivieraWaves 2017-2019
 *
 ****************************************************************************************
 */

#include "fhost_config.h"
#include "mac.h"
#include "mac_frame.h"
#include "fhost.h"
#include "rwnx_config.h"
#include "wifi.h"
#include "rwnx_msg_tx.h"
/**
 ****************************************************************************************
 * @addtogroup FHOST_CONFIG
 * @{
 ****************************************************************************************
 */
/// define a channel
#define CHAN(_freq, _band, _flags, _pwr) {         \
        .freq = (_freq),                           \
        .band = (_band),                           \
        .flags = (_flags),                         \
        .tx_power = (_pwr),                        \
    }

/// define a channel in 2.4GHz band
#define CHAN_24(_freq, _flag, _pwr) CHAN(_freq, PHY_BAND_2G4, _flag, _pwr)
/// define a channel in 5GHz band
#define CHAN_5(_freq, _flag, _pwr) CHAN(_freq, PHY_BAND_5G, _flag, _pwr)
#define CHAN_IR_RADAR_FLAGS     (CHAN_NO_IR | CHAN_RADAR)

#define CHAN_BW_FLAGS_CH_1      (CHAN_HT40P)
#define CHAN_BW_FLAGS_CH_2      (CHAN_HT40P)
#define CHAN_BW_FLAGS_CH_3      (CHAN_HT40P)
#define CHAN_BW_FLAGS_CH_4      (CHAN_HT40P)
#define CHAN_BW_FLAGS_CH_5      (CHAN_HT40P | CHAN_HT40M)
#define CHAN_BW_FLAGS_CH_6      (CHAN_HT40P | CHAN_HT40M)
#define CHAN_BW_FLAGS_CH_7      (CHAN_HT40P | CHAN_HT40M)
#define CHAN_BW_FLAGS_CH_8      (CHAN_HT40P | CHAN_HT40M)
#define CHAN_BW_FLAGS_CH_9      (CHAN_HT40P | CHAN_HT40M)
#define CHAN_BW_FLAGS_CH_10     (CHAN_HT40M)
#define CHAN_BW_FLAGS_CH_11     (CHAN_HT40M)
#define CHAN_BW_FLAGS_CH_12     (CHAN_HT40M)
#define CHAN_BW_FLAGS_CH_13     (CHAN_HT40M)
#define CHAN_BW_FLAGS_CH_14     (0)
#define CHAN_BW_FLAGS_CH_36     (CHAN_HT40P | CHAN_VHT80_10_70)
#define CHAN_BW_FLAGS_CH_40     (CHAN_HT40M | CHAN_VHT80_30_50)
#define CHAN_BW_FLAGS_CH_44     (CHAN_HT40P | CHAN_VHT80_50_30)
#define CHAN_BW_FLAGS_CH_48     (CHAN_HT40M | CHAN_VHT80_70_10)
#define CHAN_BW_FLAGS_CH_52     (CHAN_HT40P | CHAN_VHT80_10_70)
#define CHAN_BW_FLAGS_CH_56     (CHAN_HT40M | CHAN_VHT80_30_50)
#define CHAN_BW_FLAGS_CH_60     (CHAN_HT40P | CHAN_VHT80_50_30)
#define CHAN_BW_FLAGS_CH_64     (CHAN_HT40M | CHAN_VHT80_70_10)
#define CHAN_BW_FLAGS_CH_100    (CHAN_HT40P | CHAN_VHT80_10_70)
#define CHAN_BW_FLAGS_CH_104    (CHAN_HT40M | CHAN_VHT80_30_50)
#define CHAN_BW_FLAGS_CH_108    (CHAN_HT40P | CHAN_VHT80_50_30)
#define CHAN_BW_FLAGS_CH_112    (CHAN_HT40M | CHAN_VHT80_70_10)
#define CHAN_BW_FLAGS_CH_116    (CHAN_HT40P | CHAN_VHT80_10_70)
#define CHAN_BW_FLAGS_CH_120    (CHAN_HT40M | CHAN_VHT80_30_50)
#define CHAN_BW_FLAGS_CH_124    (CHAN_HT40P | CHAN_VHT80_50_30)
#define CHAN_BW_FLAGS_CH_128    (CHAN_HT40M | CHAN_VHT80_70_10)
#define CHAN_BW_FLAGS_CH_132    (CHAN_HT40P | CHAN_VHT80_10_70)
#define CHAN_BW_FLAGS_CH_136    (CHAN_HT40M | CHAN_VHT80_30_50)
#define CHAN_BW_FLAGS_CH_140    (CHAN_HT40P | CHAN_VHT80_50_30)
#define CHAN_BW_FLAGS_CH_144    (CHAN_HT40M | CHAN_VHT80_70_10)
#define CHAN_BW_FLAGS_CH_149    (CHAN_HT40P | CHAN_VHT80_10_70)
#define CHAN_BW_FLAGS_CH_153    (CHAN_HT40M | CHAN_VHT80_30_50)
#define CHAN_BW_FLAGS_CH_157    (CHAN_HT40P | CHAN_VHT80_50_30)
#define CHAN_BW_FLAGS_CH_161    (CHAN_HT40M | CHAN_VHT80_70_10)
#define CHAN_BW_FLAGS_CH_165    (CHAN_HT40P | CHAN_VHT80_10_70)

const struct {
    uint16_t ch_freq;
    uint16_t bw_flags;
} chan_bw_flags_table[] = {
    {2412, CHAN_BW_FLAGS_CH_1  }, //0
    {2417, CHAN_BW_FLAGS_CH_2  }, //1
    {2422, CHAN_BW_FLAGS_CH_3  }, //2
    {2427, CHAN_BW_FLAGS_CH_4  }, //3
    {2432, CHAN_BW_FLAGS_CH_5  }, //4
    {2437, CHAN_BW_FLAGS_CH_6  }, //5
    {2442, CHAN_BW_FLAGS_CH_7  }, //6
    {2447, CHAN_BW_FLAGS_CH_8  }, //7
    {2452, CHAN_BW_FLAGS_CH_9  }, //8
    {2457, CHAN_BW_FLAGS_CH_10 }, //9
    {2462, CHAN_BW_FLAGS_CH_11 }, //10
    {2467, CHAN_BW_FLAGS_CH_12 }, //11
    {2472, CHAN_BW_FLAGS_CH_13 }, //12
    {2484, CHAN_BW_FLAGS_CH_14 }, //13
    {5180, CHAN_BW_FLAGS_CH_36 }, //14
    {5200, CHAN_BW_FLAGS_CH_40 }, //15
    {5220, CHAN_BW_FLAGS_CH_44 }, //16
    {5240, CHAN_BW_FLAGS_CH_48 }, //17
    {5260, CHAN_BW_FLAGS_CH_52 }, //18
    {5280, CHAN_BW_FLAGS_CH_56 }, //19
    {5300, CHAN_BW_FLAGS_CH_60 }, //20
    {5320, CHAN_BW_FLAGS_CH_64 }, //21
    {5500, CHAN_BW_FLAGS_CH_100}, //22
    {5520, CHAN_BW_FLAGS_CH_104}, //23
    {5540, CHAN_BW_FLAGS_CH_108}, //24
    {5560, CHAN_BW_FLAGS_CH_112}, //25
    {5580, CHAN_BW_FLAGS_CH_116}, //26
    {5600, CHAN_BW_FLAGS_CH_120}, //27
    {5620, CHAN_BW_FLAGS_CH_124}, //28
    {5640, CHAN_BW_FLAGS_CH_128}, //29
    {5660, CHAN_BW_FLAGS_CH_132}, //30
    {5680, CHAN_BW_FLAGS_CH_136}, //31
    {5700, CHAN_BW_FLAGS_CH_140}, //32
    {5720, CHAN_BW_FLAGS_CH_144}, //33
    {5745, CHAN_BW_FLAGS_CH_149}, //34
    {5765, CHAN_BW_FLAGS_CH_153}, //35
    {5785, CHAN_BW_FLAGS_CH_157}, //36
    {5805, CHAN_BW_FLAGS_CH_161}, //37
    {5825, CHAN_BW_FLAGS_CH_165}, //38
};

/** List of supported Channel */
struct me_chan_config_req fhost_chan = {
    .chan2G4_cnt = 13,
    .chan2G4[0] = CHAN_24(2412, 0, 20),
    .chan2G4[1] = CHAN_24(2417, 0, 20),
    .chan2G4[2] = CHAN_24(2422, 0, 20),
    .chan2G4[3] = CHAN_24(2427, 0, 20),
    .chan2G4[4] = CHAN_24(2432, 0, 20),
    .chan2G4[5] = CHAN_24(2437, 0, 20),
    .chan2G4[6] = CHAN_24(2442, 0, 20),
    .chan2G4[7] = CHAN_24(2447, 0, 20),
    .chan2G4[8] = CHAN_24(2452, 0, 20),
    .chan2G4[9] = CHAN_24(2457, 0, 20),
    .chan2G4[10] = CHAN_24(2462, 0, 20),
    .chan2G4[11] = CHAN_24(2467, 0, 20),
    .chan2G4[12] = CHAN_24(2472, 0, 20),
    //.chan2G4[13] = CHAN_24(2484, 0, 20),

#ifdef USE_5G
    .chan5G_cnt = 25,
    .chan5G[0] = CHAN_5(5180, 0, 20),
    .chan5G[1] = CHAN_5(5200, 0, 20),
    .chan5G[2] = CHAN_5(5220, 0, 20),
    .chan5G[3] = CHAN_5(5240, 0, 20),
    .chan5G[4] = CHAN_5(5260, CHAN_NO_IR|CHAN_RADAR, 20),
    .chan5G[5] = CHAN_5(5280, CHAN_NO_IR|CHAN_RADAR, 20),
    .chan5G[6] = CHAN_5(5300, CHAN_NO_IR|CHAN_RADAR, 20),
    .chan5G[7] = CHAN_5(5320, CHAN_NO_IR|CHAN_RADAR, 20),
    .chan5G[8] = CHAN_5(5500, CHAN_NO_IR|CHAN_RADAR, 20),
    .chan5G[9] = CHAN_5(5520, CHAN_NO_IR|CHAN_RADAR, 20),
    .chan5G[10] = CHAN_5(5540, CHAN_NO_IR|CHAN_RADAR, 20),
    .chan5G[11] = CHAN_5(5560, CHAN_NO_IR|CHAN_RADAR, 20),
    .chan5G[12] = CHAN_5(5580, CHAN_NO_IR|CHAN_RADAR, 20),
    .chan5G[13] = CHAN_5(5600, CHAN_NO_IR|CHAN_RADAR, 20),
    .chan5G[14] = CHAN_5(5620, CHAN_NO_IR|CHAN_RADAR, 20),
    .chan5G[15] = CHAN_5(5640, CHAN_NO_IR|CHAN_RADAR, 20),
    .chan5G[16] = CHAN_5(5660, CHAN_NO_IR|CHAN_RADAR, 20),
    .chan5G[17] = CHAN_5(5680, CHAN_NO_IR|CHAN_RADAR, 20),
    .chan5G[18] = CHAN_5(5700, CHAN_NO_IR|CHAN_RADAR, 20),
    .chan5G[19] = CHAN_5(5720, CHAN_NO_IR|CHAN_RADAR, 20),
    .chan5G[20] = CHAN_5(5745, 0, 20),
    .chan5G[21] = CHAN_5(5765, 0, 20),
    .chan5G[22] = CHAN_5(5785, 0, 20),
    .chan5G[23] = CHAN_5(5805, 0, 20),
    .chan5G[24] = CHAN_5(5825, 0, 20),
#else
    .chan5G_cnt = 0,
#endif
};

#undef CHAN_5
#undef CHAN_24
#undef CHAN

#ifdef USE_5G
#define PLF_BAND5G 1
#else
#define PLF_BAND5G 0
#endif

static const wifi_regdom_t regdom_00 = {
    .cc = "00",
    #if PLF_BAND5G
    .reg_rules_cnt = 5,
    #else
    .reg_rules_cnt = 1,
    #endif
    .dfs_region    = NL80211_DFS_FCC,
    .reg_rules_arr = {
        {  1, 14, 20, 0},
        #if PLF_BAND5G
        { 36,  4, 20, 0},
        { 52,  4, 20, CHAN_NO_IR|CHAN_RADAR},
        {100, 12, 20, CHAN_NO_IR|CHAN_RADAR},
        {149,  5, 20, 0},
        #endif
    },
};

static const wifi_regdom_t regdom_CA = {
    .cc = "CA",
    #if PLF_BAND5G
    .reg_rules_cnt = 5,
    #else
    .reg_rules_cnt = 1,
    #endif
    .dfs_region    = NL80211_DFS_FCC,
    .reg_rules_arr = {
        {  1, 11, 30, 0},
        #if PLF_BAND5G
        { 36,  4, 17, 0},
        { 52,  4, 23, CHAN_NO_IR|CHAN_RADAR},
        {100, 12, 24, CHAN_NO_IR|CHAN_RADAR},
        {149,  5, 30, 0},
        #endif
    },
};

static const wifi_regdom_t regdom_CN = {
    .cc = "CN",
    #if PLF_BAND5G
    .reg_rules_cnt = 4,
    #else
    .reg_rules_cnt = 1,
    #endif
    .dfs_region    = NL80211_DFS_FCC,
    .reg_rules_arr = {
        {  1, 13, 20, 0},
        #if PLF_BAND5G
        { 36,  4, 23, 0},
        { 52,  4, 23, CHAN_NO_IR|CHAN_RADAR},
        {149,  5, 30, 0},
        #endif
    },
};

static const wifi_regdom_t regdom_US = {
    .cc = "US",
    #if PLF_BAND5G
    .reg_rules_cnt = 5,
    #else
    .reg_rules_cnt = 1,
    #endif
    .dfs_region    = NL80211_DFS_FCC,
    .reg_rules_arr = {
        {  1, 11, 30, 0},
        #if PLF_BAND5G
        { 36,  4, 17, 0},
        { 52,  4, 23, CHAN_NO_IR|CHAN_RADAR},
        {100, 11, 24, CHAN_NO_IR|CHAN_RADAR},
        {149,  5, 30, 0},
        #endif
    },
};

static const wifi_regdom_t regdom_EU = {
    .cc = "EU",
    #if PLF_BAND5G
    .reg_rules_cnt = 4,
    #else
    .reg_rules_cnt = 1,
    #endif
    .dfs_region    = NL80211_DFS_ETSI,
    .reg_rules_arr = {
        {  1, 13, 20, 0},
        #if PLF_BAND5G
        { 36,  8, 20, 0},
        {100, 11, 23, CHAN_NO_IR|CHAN_RADAR},
        #endif
    },
};
static const wifi_regdom_t regdom_IN = {
    .cc = "IN",
    #if PLF_BAND5G
    .reg_rules_cnt = 4,
    #else
    .reg_rules_cnt = 1,
    #endif
    .reg_rules_arr = {
        {  1, 13, 20, 0},
        #if PLF_BAND5G
        { 36,  4, 20, 0},
        { 52,  4, 20, CHAN_NO_IR|CHAN_RADAR},
        {149,  5, 20, 0},
        #endif
    },
};
static const wifi_regdom_t regdom_JP = {
    .cc = "JP",
    #if PLF_BAND5G
    .reg_rules_cnt = 5,
    #else
    .reg_rules_cnt = 2,
    #endif
    .dfs_region = NL80211_DFS_JP,
    .reg_rules_arr = {
        {  1, 13, 20, 0},
        { 14, 11, 20, 0},
        #if PLF_BAND5G
        { 36,  4, 17, 0},
        { 52,  4, 23, CHAN_NO_IR|CHAN_RADAR},
        {100, 11, 24, CHAN_NO_IR|CHAN_RADAR},
        #endif
    },
};
static const wifi_regdom_t regdom_KR = {
    .cc = "KR",
    #if PLF_BAND5G
    .reg_rules_cnt = 5,
    #else
    .reg_rules_cnt = 1,
    #endif
    .dfs_region = NL80211_DFS_JP,
    .reg_rules_arr = {
        {  1, 13, 20, 0},
        #if PLF_BAND5G
        { 36,  4, 20, 0},
        { 52,  2, 20, CHAN_NO_IR|CHAN_RADAR},
        {100, 11, 30, CHAN_NO_IR|CHAN_RADAR},
        {149,  5, 30, 0},
        #endif
    },
};
const wifi_regdom_t *wifi_regdb[] = {
    &regdom_00,
    &regdom_CA,
    &regdom_CN,
    &regdom_US,
    &regdom_EU,
    &regdom_IN,
    &regdom_JP,
    &regdom_KR,
};

const int wifi_regdb_size = sizeof(wifi_regdb) / sizeof(wifi_regdom_t *);

/*******************************************************************************
 * FW configuration:
 ******************************************************************************/
/// MAC Address
uint8_t fhost_default_mac_addr[6] = {0x88, 0x00, 0x33, 0x77, 0x69, 0xcc};
static uint8_t fhost_mac_addr[6] = {0x88, 0x00, 0x33, 0x77, 0x69, 0xcc};

/// PHY configuration (This is only valid for KARST radio)
static uint32_t phy_cfg[] = {0x01000000, 0x01000000, 0x01000000, 0x01000000,
                             0x01000000, 0x01000000, 0x01000000, 0x01000000,
                             0x0};

static struct {
    bool ps_on;
    #if NX_UAPSD
    uint32_t uapsd_timeout;
    #endif
} ps_cfg = {
    .ps_on = true,
    #if NX_UAPSD
    .uapsd_timeout = 300,
    #endif
};
/**
 * WiFi country code string
 *  Supported country codes: "00", "CA", "CN", "US", "IN", "JP", "KR"
 */
char current_country_code[3] = {'0', '0', '\0'};
uint16_t tx_lft_ms = 1000;

bool ht_support  = 1;
bool ht_40mhz    = 1;
bool sgi_20_40mhz = 0;
#if defined(CONFIG_AIC8800D80)
bool vht_80mhz   = 0;
bool sgi_80mhz   = 0;
#endif
bool vht_support = 1;
uint8_t vht_mcs  = 2;
bool he_support  = 1;
uint8_t he_mcs   = 2;
uint8_t nss      = 1;
bool stbc        = 1;
bool ldpc        = 1;
bool su_bfmee    = 1;
bool mu_bfmee    = 0;

/// Wifi configuration
struct fhost_config_item fw_config[] = {
    {FHOST_CFG_MAC_ADDR, 6, fhost_mac_addr},
    {FHOST_CFG_PHY_KARST, 33, phy_cfg},
    {FHOST_CFG_PS, 4, &ps_cfg.ps_on},
    #if NX_UAPSD
    {FHOST_CFG_UAPSD_TIMEOUT, 4, &ps_cfg.uapsd_timeout},
    #endif
    {FHOST_CFG_TX_LFT, 2, &tx_lft_ms},
    {FHOST_CFG_NSS, 1, &nss},
    {FHOST_CFG_LDPC, 1, &ldpc},
    {FHOST_CFG_STBC, 1, &stbc},
    {FHOST_CFG_HT, 1, &ht_support},
    {FHOST_CFG_40MHZ, 1, &ht_40mhz},
    {FHOST_CFG_VHT, 1, &vht_support},
    {FHOST_CFG_VHT_MCS, 1, &vht_mcs},
    {FHOST_CFG_HE, 1, &he_support},
    {FHOST_CFG_HE_MCS, 1, &he_mcs},
    {FHOST_CFG_BFMEE, 1, &su_bfmee},
    {FHOST_CFG_MURX, 1, &mu_bfmee},

    {FHOST_CFG_END, 0, NULL}
};
/*
 * fhost_config_get_next_item: Must return configuration item one by one.
 * In this console confguration is saved in the global varaible fw_config
 */
void fhost_config_get_next_item(struct fhost_config_item *item)
{
    static int index;

    // First call, initialize index
    if (item->data == NULL)
        index = 0;

    // Check that we don't read outside of console_config table
    if (index > (sizeof(fw_config) / sizeof(struct fhost_config_item)))
    {
        item->id = FHOST_CFG_END;
    }

    // Copy one configuration parameter
    *item = fw_config[index];

    // Update index for next call
    index++;
}
static const int mcs_map_to_rate[4][3] = {
    [PHY_CHNL_BW_20][MAC_VHT_MCS_MAP_0_7] = 65,
    [PHY_CHNL_BW_20][MAC_VHT_MCS_MAP_0_8] = 78,
    [PHY_CHNL_BW_20][MAC_VHT_MCS_MAP_0_9] = 78,
    [PHY_CHNL_BW_40][MAC_VHT_MCS_MAP_0_7] = 135,
    [PHY_CHNL_BW_40][MAC_VHT_MCS_MAP_0_8] = 162,
    [PHY_CHNL_BW_40][MAC_VHT_MCS_MAP_0_9] = 180,
    [PHY_CHNL_BW_80][MAC_VHT_MCS_MAP_0_7] = 292,
    [PHY_CHNL_BW_80][MAC_VHT_MCS_MAP_0_8] = 351,
    [PHY_CHNL_BW_80][MAC_VHT_MCS_MAP_0_9] = 390,
    [PHY_CHNL_BW_160][MAC_VHT_MCS_MAP_0_7] = 585,
    [PHY_CHNL_BW_160][MAC_VHT_MCS_MAP_0_8] = 702,
    [PHY_CHNL_BW_160][MAC_VHT_MCS_MAP_0_9] = 780,
};

#define MAX_VHT_RATE(map, nss, bw) (mcs_map_to_rate[bw][map] * (nss))

extern struct rwnx_hw *g_rwnx_hw;
int fhost_chan_update(const char * country_code);
void fhost_config_prepare(struct me_config_req *me_config, struct mm_start_req *start,
                          struct mac_addr *base_mac_addr, bool init)
{
    struct fhost_config_item item;
    uint32_t phy_ver1, /*phy_ver2, */sec = 0, usec = 0;
    uint8_t nss, vht_mcs;
    bool sgi, stbc, ht40, ldpc_rx, bfmee, bfmer, mu_rx, mu_tx;
    #if defined(CONFIG_AIC8800D80)
    bool sgi80, vht80;
    #endif
    #if NX_HE
    uint8_t he_mcs = 0;
    #endif

    memset(me_config, 0, sizeof(*me_config));
    memset(start, 0, sizeof(*start));
    item.data = NULL;

    /* Default value */
    stbc = true;
    sgi = true;
    ht40 = true;
    #if defined(CONFIG_AIC8800D80)
    vht80 = false;
    sgi80 = false;
    #endif
    vht_mcs = 0;
    me_config->ht_supp = true;
    me_config->vht_supp = false;
    me_config->he_supp = true;
    nss = 1;
    ldpc_rx = true;
    bfmee = true;
    bfmer = false;
    mu_rx = true;
    mu_tx = false;
    phy_ver1 = 0x00220000;

    fhost_config_get_next_item(&item);
    while (item.id != FHOST_CFG_END)
    {
        switch (item.id)
        {
            case FHOST_CFG_MAC_ADDR:
                memcpy(base_mac_addr, item.data, sizeof(*base_mac_addr));
                break;
            case FHOST_CFG_PS:
                me_config->ps_on = *(bool *)item.data;
                break;
            case FHOST_CFG_DPSM:
                me_config->dpsm = *(bool *)item.data;
                break;
            case FHOST_CFG_SGI:
                sgi = *(bool *)item.data;
                break;
            #if defined(CONFIG_AIC8800D80)
            case FHOST_CFG_SGI80:
                if (vht80)
                    sgi80 = *(bool *)item.data;
                break;
            #endif
            case FHOST_CFG_NSS:
            {
                uint8_t val = *(uint8_t *)item.data;
                if (val && val < nss)
                    nss = val;
            } break;
            case FHOST_CFG_TX_LFT:
                me_config->tx_lft = *(uint16_t *)item.data;
                break;
            #if NX_UAPSD
            case FHOST_CFG_UAPSD_TIMEOUT:
                start->uapsd_timeout = *(uint32_t *)item.data;
                break;
            case FHOST_CFG_UAPSD_QUEUES:
                if (init)
                    fhost_vif_set_uapsd_queues(-1, *(uint8_t *)item.data);
                break;
            #endif
            case FHOST_CFG_LP_CLK_ACCURACY:
                start->lp_clk_accuracy = *(uint16_t *)item.data;
                break;
            case FHOST_CFG_HT:
                me_config->ht_supp = *(bool *)item.data;
                break;
            case FHOST_CFG_40MHZ:
                if (ht40)
                    ht40 = *(bool *)item.data;
                break;
            #if defined(CONFIG_AIC8800D80)
            case FHOST_CFG_80MHZ:
                if (vht80)
                    vht80 = *(bool *)item.data;
                break;
            #endif
            case FHOST_CFG_VHT:
                #if NX_VHT
                //if (phy_vht_supported() && (phy_get_bw() >= 2))
                    me_config->vht_supp = *(bool *)item.data;
                //else
                //    me_config->vht_supp = false;
                #endif
                break;
            case FHOST_CFG_VHT_MCS:
                vht_mcs = *(uint8_t *)item.data;
                if (vht_mcs > 2)
                    vht_mcs = 0;
                break;
            #if NX_HE
            case FHOST_CFG_HE:
                //if (hal_machw_he_support())
                    me_config->he_supp = *(bool *)item.data;
                //else
                //    me_config->he_supp = false;
                break;
            case FHOST_CFG_HE_MCS:
                he_mcs = *(uint8_t *)item.data;
                if (he_mcs > 2)
                    he_mcs = 0;
                break;
            #endif
            case FHOST_CFG_LDPC:
                //if (!*(bool *)item.data)
                    ldpc_rx = *(bool *)item.data;
                break;
            case FHOST_CFG_STBC:
                stbc = *(bool *)item.data;
                break;
            case FHOST_CFG_BFMEE:
                if (!*(bool *)item.data)
                    bfmee = false;
                break;
            case FHOST_CFG_BFMER:
                if (!*(bool *)item.data)
                    bfmer = false;
                break;
            case FHOST_CFG_MURX:
                if (!*(bool *)item.data)
                    mu_rx = false;
                break;
            case FHOST_CFG_MUTX:
                if (!*(bool *)item.data)
                    mu_tx = false;
                break;
            case FHOST_CFG_PHY_TRD:
                if (((phy_ver1 & 0x030000) >> 16) == 0) {
                    memcpy(&start->phy_cfg.parameters, item.data, item.len);
                }
                break;
            case FHOST_CFG_PHY_KARST:
                if (((phy_ver1 & 0x030000) >> 16) == 2)
                {
                    memcpy(&start->phy_cfg.parameters, item.data, item.len);
                }
                break;
            case FHOST_CFG_ANT_DIV:
                me_config->ant_div_on = *(bool *)item.data;
                break;
            case FHOST_CFG_EPOCH_SEC:
                sec = *(uint32_t *)item.data;
                break;
            case FHOST_CFG_EPOCH_USEC:
                usec = *(uint32_t *)item.data;
                break;
            default :
                break;
        }
        fhost_config_get_next_item(&item);
    }

    if (init)
    {
        if (sec && usec)
        {
            //aic_time_init(sec, usec);
        }

        co_random_init((rtos_now(0) & 0xffff) * (rtos_now(0) & 0x1ffff));
    }

    #if defined(CONFIG_AIC8800D80)
    if (!ht40 || !me_config->vht_supp)
    {
        vht80 = false;
    }

    if (!vht80)
    {
        sgi80 = false;
    }
    #endif

    if (me_config->ht_supp)
    {
        int max_rate;
        me_config->ht_cap.ht_capa_info = 0;
        if (ldpc_rx)
            me_config->ht_cap.ht_capa_info |= MAC_HTCAPA_LDPC;
        if (ht40)
            me_config->ht_cap.ht_capa_info |= MAC_HTCAPA_40_MHZ;
        #if !NX_HE
        me_config->ht_cap.ht_capa_info |= MAC_HTCAPA_GREEN_FIELD;
        #endif
        if (sgi)
        {
            me_config->ht_cap.ht_capa_info |= MAC_HTCAPA_SHORTGI_20;
            if (ht40)
                me_config->ht_cap.ht_capa_info |= MAC_HTCAPA_SHORTGI_40;
        }
        if (nss > 1)
            me_config->ht_cap.ht_capa_info |= MAC_HTCAPA_TX_STBC;
        me_config->ht_cap.ht_capa_info |= (1 << MAC_HTCAPA_RX_STBC_OFT);

        me_config->ht_cap.a_mpdu_param = ((3 << MAC_AMPDU_LEN_EXP_OFT) |
                                         (7 << MAC_AMPDU_MIN_SPACING_OFT));
        me_config->ht_cap.mcs_rate[0] = 0xff; /* RX MCS0-7*/
        if (nss > 1)
            me_config->ht_cap.mcs_rate[1] = 0xff; /* RX MCS8-15 */
        if (ht40)
            me_config->ht_cap.mcs_rate[4] = 1; /* RX MCS32 */

        if (sgi && ht40)
            max_rate = 150;
        else if (sgi)
            max_rate = 72;
        else if (ht40)
            max_rate = 135;
        else
            max_rate = 65;

        me_config->ht_cap.mcs_rate[10] = max_rate * nss; /* highest supported rate */
        me_config->ht_cap.mcs_rate[12] = 1;              /* TX mcs same as RX mcs */

        me_config->ht_cap.ht_extended_capa = 0;
        me_config->ht_cap.tx_beamforming_capa = 0;
        me_config->ht_cap.asel_capa = 0;
    }
    else
    {
        ht40 = false;
    }

    #if defined(CONFIG_AIC8800D80)
    if (vht80)
        me_config->phy_bw_max = PHY_CHNL_BW_80;
    else
    #endif
    if (ht40)
        me_config->phy_bw_max = PHY_CHNL_BW_40;
    else
        me_config->phy_bw_max = PHY_CHNL_BW_20;

    if (me_config->vht_supp)
    {
        int i;
        me_config->vht_cap.vht_capa_info |= MAC_VHTCAPA_MAX_MPDU_LENGTH_3895;
        me_config->vht_cap.vht_capa_info |= MAC_VHTCAPA_SUPP_CHAN_WIDTH_80;

        if (ldpc_rx)
            me_config->vht_cap.vht_capa_info |= MAC_VHTCAPA_RXLDPC;
        #if defined(CONFIG_AIC8800D80)
        if (sgi80)
            me_config->vht_cap.vht_capa_info |= MAC_VHTCAPA_SHORT_GI_80;
        #endif
        if (nss > 1)
            me_config->vht_cap.vht_capa_info |= MAC_VHTCAPA_TXSTBC;
        if (stbc)
            me_config->vht_cap.vht_capa_info |= MAC_VHTCAPA_RXSTBC_1;
        if (bfmer)
            me_config->vht_cap.vht_capa_info |= MAC_VHTCAPA_SU_BEAMFORMER_CAPABLE;
        if (bfmee)
            me_config->vht_cap.vht_capa_info |= MAC_VHTCAPA_SU_BEAMFORMEE_CAPABLE;
        me_config->vht_cap.vht_capa_info |= (nss - 1 ) << MAC_VHTCAPA_SOUNDING_DIMENSIONS_OFT;
        if (mu_tx)
            me_config->vht_cap.vht_capa_info |= MAC_VHTCAPA_MU_BEAMFORMER_CAPABLE;
        if (mu_rx)
            me_config->vht_cap.vht_capa_info |= MAC_VHTCAPA_MU_BEAMFORMEE_CAPABLE;

        me_config->vht_cap.vht_capa_info |= (7 << MAC_VHTCAPA_MAX_A_MPDU_LENGTH_EXP_OFT);
        me_config->vht_cap.vht_capa_info |= (3 << MAC_VHTCAPA_BEAMFORMEE_STS_OFT);

        me_config->vht_cap.rx_mcs_map = vht_mcs; /* RX MCS for 1 NSS */
        for (i = 1; i < nss; i++) /* RX MCS for 2 NSS (limit to mcs7) */
            me_config->vht_cap.rx_mcs_map |= MAC_VHT_MCS_MAP_0_7 << (2 * i);
        for (; i < 8; i++) /* RX MCS for non-supported NSS */
            me_config->vht_cap.rx_mcs_map |= MAC_VHT_MCS_MAP_NONE << (2 * i);
        me_config->vht_cap.rx_highest = MAX_VHT_RATE(MAC_VHT_MCS_MAP_0_7, nss, me_config->phy_bw_max);//200 * nss;

        me_config->vht_cap.tx_mcs_map = vht_mcs; /* TX MCS for 1 NSS */
        if (vht_mcs > MAC_VHT_MCS_MAP_0_9)
            vht_mcs = MAC_VHT_MCS_MAP_0_9;
        for (i = 1; i < nss; i++) /* TX MCS for 2 NSS (limit to mcs8) */
            me_config->vht_cap.tx_mcs_map |= vht_mcs << (2 * i);
        for (; i < 8; i++) /* TX MCS for non-supported NSS */
            me_config->vht_cap.tx_mcs_map |= MAC_VHT_MCS_MAP_NONE << (2 * i);
        me_config->vht_cap.tx_highest = MAX_VHT_RATE(MAC_VHT_MCS_MAP_0_9, nss, me_config->phy_bw_max);//200 * nss;
    }

    #if NX_HE
    if (me_config->he_supp)
    {
        int i;
        struct mac_hecapability *he_cap = &me_config->he_cap;

        memset(he_cap, 0, sizeof(*he_cap));

        he_cap->ppe_thres[0] = 0x08;
        he_cap->ppe_thres[1] = 0x1C;
        he_cap->ppe_thres[2] = 0x07;

        // HE MAC Capabilities Information
        HE_MAC_CAPA_BIT_SET(he_cap, ALL_ACK);
        // HE_MAC_CAPA_BIT_SET(he_cap, NDP_FB_REP);
        // HE_MAC_CAPA_BIT_SET(he_cap, 32BIT_BA_BITMAP);
        // HE_MAC_CAPA_BIT_SET(he_cap, MU_CASCADING);
        // HE_MAC_CAPA_BIT_SET(he_cap, ACK_EN);
        // HE_MAC_CAPA_BIT_SET(he_cap, OM_CONTROL);
        // HE_MAC_CAPA_BIT_SET(he_cap, OFDMA_RA);
        // HE_MAC_CAPA_BIT_SET(he_cap, HTC_HE);
        // HE_MAC_CAPA_BIT_SET(he_cap, TSR);
        // HE_MAC_CAPA_BIT_SET(he_cap, BSR); // TBD
        // HE_MAC_CAPA_BIT_SET(he_cap, BSRP_BQRP_A_MPDU_AGG);

        // HE PHY Capabilities Information
        HE_PHY_CAPA_BIT_SET(he_cap, HE_SU_PPDU_1x_LTF_AND_GI_0_8US);
        HE_PHY_CAPA_BIT_SET(he_cap, NDP_4x_LTF_AND_3_2US);
        HE_PHY_CAPA_BIT_SET(he_cap, PPE_THRESHOLD_PRESENT);

        HE_PHY_CAPA_BIT_SET(he_cap, DOPPLER_RX);
        HE_PHY_CAPA_BIT_SET(he_cap, NG16_SU_FEEDBACK);
        HE_PHY_CAPA_BIT_SET(he_cap, NG16_MU_FEEDBACK);
        HE_PHY_CAPA_BIT_SET(he_cap, CODEBOOK_SIZE_42_SU);
        HE_PHY_CAPA_BIT_SET(he_cap, CODEBOOK_SIZE_75_MU);
        HE_PHY_CAPA_BIT_SET(he_cap, TRIG_SU_BEAMFORMER_FB);
        HE_PHY_CAPA_BIT_SET(he_cap, TRIG_MU_BEAMFORMER_FB);
        // HE_PHY_CAPA_BIT_SET(he_cap, TRIG_CQI_FB);

         HE_PHY_CAPA_BIT_SET(he_cap, RX_HE_MU_PPDU_FRM_NON_AP);

        //  HE_PHY_CAPA_BIT_SET(he_cap, CODEBOOK_SIZE_75_MU);
        //  HE_PHY_CAPA_BIT_SET(he_cap, PARTIAL_BW_EXT_RANGE);
        // HE_PHY_CAPA_BIT_SET(he_cap, FULL_BW_UL_MU_MIMO);

        HE_PHY_CAPA_VAL_SET(he_cap, MIDAMBLE_RX_MAX_NSTS, WIDTH);
        HE_PHY_CAPA_VAL_SET(he_cap, DCM_MAX_CONST_TX, 16_QAM);
        HE_PHY_CAPA_VAL_SET(he_cap, DCM_MAX_CONST_RX, 16_QAM);
        if(nss > 1)
            HE_PHY_CAPA_BIT_SET(he_cap, DCM_MAX_NSS_RX);
        #if defined(CONFIG_AIC8800D80)
        if (vht80)
            HE_PHY_CAPA_VAL_SET(he_cap, DCM_MAX_BW, 80MHZ);
        else
        #endif
        if (ht40)
            HE_PHY_CAPA_VAL_SET(he_cap, DCM_MAX_BW, 40MHZ);
        if (bfmee)
        {
            HE_PHY_CAPA_BIT_SET(he_cap, SU_BEAMFORMEE);
            HE_PHY_CAPA_VAL_SET(he_cap, BFMEE_MAX_STS_UNDER_80MHZ, 4);
        }

        if (ht40)
        {
            // HE_PHY_CAPA_VAL_SET(he_cap, CHAN_WIDTH_SET, 40MHZ_IN_2G);
            // HE_PHY_CAPA_VAL_SET(he_cap, CHAN_WIDTH_SET, 40MHZ_80MHZ_IN_5G);
            HE_PHY_CAPA_VAL_SET(he_cap, CHAN_WIDTH_SET, 40MHZ_80MHZ_IN_2G_5G);
            he_cap->ppe_thres[0] |= 0x30;
        }
        if (ldpc_rx)
            HE_PHY_CAPA_BIT_SET(he_cap, LDPC_CODING_IN_PAYLOAD);
        else
            // If no LDPC is supported, we have to limit to MCS0_9, as LDPC is mandatory
            // for MCS 10 and 11
            he_mcs = co_min(he_mcs, MAC_HE_MCS_MAP_0_9);

        if (stbc) {
            //HE_PHY_CAPA_BIT_SET(he_cap, STBC_TX_UNDER_80MHZ); // not support
            HE_PHY_CAPA_BIT_SET(he_cap, STBC_RX_UNDER_80MHZ);
        }

        HE_PHY_CAPA_BIT_SET(he_cap, RX_FULL_BW_SU_COMP_SIGB);
        HE_PHY_CAPA_BIT_SET(he_cap, RX_FULL_BW_SU_NON_COMP_SIGB);
        HE_PHY_CAPA_BIT_SET(he_cap, PARTIAL_BW_DL_MUMIMO);
        HE_PHY_CAPA_BIT_SET(he_cap, 20MHZ_IN_40MHZ_HE_PPDU_IN_2G);
        memset(&he_cap->mcs_supp, 0, sizeof(he_cap->mcs_supp));
        if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8801 || g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DC || g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DW)
            he_cap->mcs_supp.rx_mcs_80 = co_min(he_mcs, MAC_HE_MCS_MAP_0_9);
        else if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8800D80)
            he_cap->mcs_supp.rx_mcs_80 = co_min(he_mcs, MAC_HE_MCS_MAP_0_11);
        for (i = 1; i < nss; i++) {
            uint16_t unsup_for_ss = MAC_HE_MCS_MAP_NONE << (i*2);
            he_cap->mcs_supp.rx_mcs_80 |= MAC_HE_MCS_MAP_0_7 << (i*2);
            he_cap->mcs_supp.rx_mcs_160 |= unsup_for_ss;
            he_cap->mcs_supp.rx_mcs_80p80 |= unsup_for_ss;
        }
        for (; i < 8; i++) {
            uint16_t unsup_for_ss = MAC_HE_MCS_MAP_NONE << (i*2);
            he_cap->mcs_supp.rx_mcs_80 |= unsup_for_ss;
            he_cap->mcs_supp.rx_mcs_160 |= unsup_for_ss;
            he_cap->mcs_supp.rx_mcs_80p80 |= unsup_for_ss;
        }
        he_cap->mcs_supp.tx_mcs_80 = he_mcs;
        for (i = 1; i < nss; i++) {
            uint16_t unsup_for_ss = MAC_HE_MCS_MAP_NONE << (i*2);
            he_cap->mcs_supp.tx_mcs_80 |= MAC_HE_MCS_MAP_0_7 << (i*2);
            he_cap->mcs_supp.tx_mcs_160 |= unsup_for_ss;
            he_cap->mcs_supp.tx_mcs_80p80 |= unsup_for_ss;
        }
        for (; i < 8; i++) {
            uint16_t unsup_for_ss = MAC_HE_MCS_MAP_NONE << (i*2);
            he_cap->mcs_supp.tx_mcs_80 |= unsup_for_ss;
            he_cap->mcs_supp.tx_mcs_160 |= unsup_for_ss;
            he_cap->mcs_supp.tx_mcs_80p80 |= unsup_for_ss;
        }
    }
    #endif
    // update chan info from country code
    if (init)
        fhost_chan_update(current_country_code);
}

int fhost_country_code_set(const char * country_code)
{
    int ret = 0;
    if (country_code && (country_code[2] == '\0')) {
        if (strcmp(current_country_code, country_code)) {
            memcpy(current_country_code, country_code, 2);
            fhost_chan_update(country_code);
            rwnx_send_me_chan_config_req(g_rwnx_hw, &fhost_chan);
        } else {
            ret = 1; // same as current cc, no need to update
        }
    } else {
        ret = -1; // invalid arg
    }
    return ret;
}
/**
 ****************************************************************************************
 * @brief Return the channel associated to a given frequency
 *
 * @param[in] freq Channel frequency
 *
 * @return Channel definition whose primary frequency is the requested one and NULL
 * no such channel doesn't exist.
 ****************************************************************************************
 */
struct mac_chan_def *fhost_chan_get(int freq)
{
    int i, nb_chan;
    struct mac_chan_def *chans, *chan = NULL;

    if (freq < PHY_FREQ_5G)
    {
        chans = fhost_chan.chan2G4;
        nb_chan = fhost_chan.chan2G4_cnt;
    }
    else
    {
        chans = fhost_chan.chan5G;
        nb_chan = fhost_chan.chan5G_cnt;
    }

    for (i = 0; i < nb_chan; i++, chans++)
    {
        if (freq == chans->freq)
        {
            chan = chans;
            break;
        }
    }

    return chan;
}
uint16_t fhost_chan_bw_flags_get_by_freq(uint16_t ch_freq)
{
    uint16_t ret_flags = 0;
    do {
        int table_idx;
        if ((ch_freq < 2412) ||
            ((ch_freq > 2484) && (ch_freq < 5180)) ||
            (ch_freq > 5825)) {
            break;
        }
        if (ch_freq < 2484) {
            table_idx = (ch_freq - 2412) / 5;
        } else if (ch_freq == 2484) {
            table_idx = 13;
        } else if (ch_freq <= 5320) {
            table_idx = 14 + (ch_freq - 5180) / 20;
        } else if (ch_freq <= 5720) {
            table_idx = 22 + (ch_freq - 5500) / 20;
        } else {
            table_idx = 34 + (ch_freq - 5745) / 20;
        }
        if (chan_bw_flags_table[table_idx].ch_freq == ch_freq) {
            ret_flags = chan_bw_flags_table[table_idx].bw_flags;
        }
    } while (0);
    return ret_flags;
}
int fhost_chan_update(const char * country_code)
{
    int ret = 0, idx;
    const wifi_regdom_t *usr_regdom = NULL;
    for (idx = 0; idx < wifi_regdb_size; idx++) {
        const wifi_regdom_t *regdom = wifi_regdb[idx];
        if ((regdom->cc[0] == country_code[0]) &&
            (regdom->cc[1] == country_code[1])) {
            usr_regdom = regdom;
            break;
        }
    }
    if (usr_regdom) {
        int ch_cnt[PHY_BAND_MAX] = {0,};
        for (idx = 0; idx < usr_regdom->reg_rules_cnt; idx++) {
            const wifi_regrule_t *regrule = &usr_regdom->reg_rules_arr[idx];
            int band, ch_idx, ch_incr, ch_num;
            uint16_t freq;
            struct mac_chan_def *p_ch_def;
            if (regrule->first_chan <= 14) {
                band = PHY_BAND_2G4;
                ch_incr = 1;
                p_ch_def = &fhost_chan.chan2G4[ch_cnt[PHY_BAND_2G4]];
            } else {
                band = PHY_BAND_5G;
                ch_incr = 4;
                p_ch_def = &fhost_chan.chan5G[ch_cnt[PHY_BAND_5G]];
            }
            ch_num = regrule->first_chan;
            for (ch_idx = 0; ch_idx < regrule->chan_cnt; ch_idx++) {
                freq = phy_channel_to_freq(band, ch_num);
                if (freq) {
                    p_ch_def->freq = freq;
                    p_ch_def->band = band;
                    p_ch_def->flags = regrule->flags;
                    p_ch_def->tx_power = regrule->power;
                    p_ch_def++;
                    ch_cnt[band]++;
                } else {
                    ret++; // invalid freq
                }
                ch_num += ch_incr;
            }
        }
        fhost_chan.chan2G4_cnt = ch_cnt[PHY_BAND_2G4];
        fhost_chan.chan5G_cnt  = ch_cnt[PHY_BAND_5G];
    } else {
        ret = -1; // invalid country code
    }
#ifdef CONFIG_RADAR_OR_IR_DETECT
    rwnx_radar_set_domain(&g_rwnx_hw->radar, usr_regdom->dfs_region);
#endif
    return ret;
}
void set_mac_address(uint8_t *addr)
{
    if (!addr) {
        int ret;
        struct mm_get_mac_addr_cfm cfm;
        ret = rwnx_get_mac_addr_req(g_rwnx_hw, &cfm);
        if (0 == ret) {
            MAC_ADDR_CPY(fhost_mac_addr, (uint8_t *)cfm.mac_addr);
        }
    } else {
        addr[0] &= ~0x01U; // could not be multicast address
        MAC_ADDR_CPY(fhost_mac_addr, addr);
    }
}

uint8_t* get_mac_address(void)
{
    return (uint8_t*)fhost_mac_addr;
}

const char * fhost_config_name_get(enum fhost_config_id id)
{
    switch (id) {
    case FHOST_CFG_MAC_ADDR:
        return "MAC_ADDR";
    case FHOST_CFG_HT:
        return "HT";
    case FHOST_CFG_40MHZ:
        return "HT40";
    case FHOST_CFG_VHT:
        return "VHT";
    case FHOST_CFG_VHT_MCS:
        return "VHT_MCS";
    case FHOST_CFG_HE:
        return "HE";
    case FHOST_CFG_HE_MCS:
        return "HE_MCS";
    case FHOST_CFG_LDPC:
        return "LDPC";
    case FHOST_CFG_STBC:
        return "STBC";
    default:
        return "NOT_SUPPORT_YET";
    }
}

uint32_t fhost_config_value_get(enum fhost_config_id id)
{
    switch (id) {
    case FHOST_CFG_MAC_ADDR:
        return (uint32_t)fhost_mac_addr; // pointer in RAM
    case FHOST_CFG_HT:
        return (uint32_t)ht_support;
    case FHOST_CFG_40MHZ:
        return (uint32_t)ht_40mhz;
    case FHOST_CFG_SGI:
        return (uint32_t)sgi_20_40mhz;
    case FHOST_CFG_VHT:
        return (uint32_t)vht_support;
    #if defined(CONFIG_AIC8800D80)
    case FHOST_CFG_80MHZ:
        return (uint32_t)vht_80mhz;
    case FHOST_CFG_SGI80:
        return (uint32_t)sgi_80mhz;
    #endif
    case FHOST_CFG_VHT_MCS:
        return (uint32_t)vht_mcs;
    case FHOST_CFG_HE:
        return (uint32_t)he_support;
    case FHOST_CFG_HE_MCS:
        return (uint32_t)he_mcs;
    case FHOST_CFG_LDPC:
        return (uint32_t)ldpc;
    case FHOST_CFG_STBC:
        return (uint32_t)stbc;
    default:
        return 0;
    }
}

void fhost_config_value_set(enum fhost_config_id id, uint32_t val)
{
    switch (id) {
    case FHOST_CFG_MAC_ADDR:
        // do nothing
        break;
    case FHOST_CFG_HT:
        ht_support = val;
        break;
    case FHOST_CFG_40MHZ:
        ht_40mhz = val;
        break;
    case FHOST_CFG_SGI:
        sgi_20_40mhz = val;
        break;
    case FHOST_CFG_VHT:
        vht_support = val;
        break;
    #if defined(CONFIG_AIC8800D80)
    case FHOST_CFG_80MHZ:
        vht_80mhz = val;
        break;
    case FHOST_CFG_SGI80:
        sgi_80mhz = val;
        break;
    #endif
    case FHOST_CFG_VHT_MCS:
        vht_mcs = val;
        break;
    case FHOST_CFG_HE:
        he_support = val;
        break;
    case FHOST_CFG_HE_MCS:
        he_mcs = val;
        break;
    case FHOST_CFG_LDPC:
        ldpc = val;
        break;
    case FHOST_CFG_STBC:
        stbc = val;
        break;
    default:
        break;
    }
}

/**
 * @}
 */
