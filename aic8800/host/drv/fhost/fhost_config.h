/**
 ****************************************************************************************
 *
 * @file fhost_config.h
 *
 * @brief Definition of configuration for Fully Hosted firmware.
 *
 * Copyright (C) RivieraWaves 2017-2019
 *
 ****************************************************************************************
 */
#ifndef _FHOST_CONFIG_H_
#define _FHOST_CONFIG_H_

/**
 ****************************************************************************************
 * @addtogroup FHOST
 * @{
 ****************************************************************************************
 */

#include "rwnx_config.h"
#include "fhost_api.h"
#include "rwnx_radar.h"

typedef struct {
    uint8_t first_chan;
    uint8_t chan_cnt;
    int8_t power;
    uint8_t flags;
} wifi_regrule_t;

typedef struct {
    char cc[3]; // country code string
    uint8_t reg_rules_cnt;
    enum nl80211_dfs_regions dfs_region;
    wifi_regrule_t reg_rules_arr[];
} wifi_regdom_t;

extern struct me_chan_config_req fhost_chan;
extern char current_country_code[];
extern uint8_t fhost_default_mac_addr[];

#define CMP_MAC( a, b )  (((a[0])==(b[0]))&& \
                          ((a[1])==(b[1]))&& \
                          ((a[2])==(b[2]))&& \
                          ((a[3])==(b[3]))&& \
                          ((a[4])==(b[4]))&& \
                          ((a[5])==(b[5])))
/**
 ****************************************************************************************
 * @brief Initialize wifi configuration structure from fhost configuration
 *
 * To be called before initializing the wifi stack.
 * Can also be used to retrieve firmware feature list at run-time. In this case @p init
 * is false.
 *
 * @param[out] me_config     Configuration structure for the UMAC (i.e. ME task)
 * @param[out] start         Configuration structure for the LMAC (i.e. MM task)
 * @param[out] base_mac_addr Base MAC address of the device (from which all VIF MAC
 *                           addresses are computed)
 * @param[in]  init          Whether it is called before firmware initialization or not.
 ****************************************************************************************
 */
void fhost_config_prepare(struct me_config_req *me_config, struct mm_start_req *start,
                          struct mac_addr *base_mac_addr, bool init);

/**
 ****************************************************************************************
 * @brief Return the channel associated to a given frequency
 *
 * @param[in] freq Channel frequency
 *
 * @return Channel definition whose primary frequency is the requested one and NULL if
 * there no such channel.
 ****************************************************************************************
 */
struct mac_chan_def *fhost_chan_get(int freq);

extern void set_mac_address(uint8_t *addr);

extern uint8_t* get_mac_address(void);

const char * fhost_config_name_get(enum fhost_config_id id);
uint32_t fhost_config_value_get(enum fhost_config_id id);
void fhost_config_value_set(enum fhost_config_id id, uint32_t val);
int fhost_set_max_sta_num(int fhost_vif_idx, uint8_t sta_max);
int fhost_country_code_set(const char * country_code);
uint16_t fhost_chan_bw_flags_get_by_freq(uint16_t ch_freq);

/**
 * @}
 */
#endif /* _FHOST_CONFIG_H_ */
