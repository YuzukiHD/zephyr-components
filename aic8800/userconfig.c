/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
 *
 * All Rights Reserved
 */

#include "lmac_msg.h"
#include "rwnx_platform.h"
#include "aic_log.h"
#include <string.h>

//Parser state
#define INIT 0
#define CMD 1
#define PRINT 2
#define GET_VALUE 3

#define USRCFG_TXPWR_LOG 0

//8800D userconfig
typedef struct
{
    txpwr_idx_conf_t txpwr_idx;
    txpwr_ofst_conf_t txpwr_ofst;
    xtal_cap_conf_t xtal_cap;
} nvram_info_t;

nvram_info_t nvram_info = {
    .txpwr_idx = {
        .enable           = 1,
        .dsss             = 9,
        .ofdmlowrate_2g4  = 10,
        .ofdm64qam_2g4    = 10,
        .ofdm256qam_2g4   = 9,
        .ofdm1024qam_2g4  = 8,
        .ofdmlowrate_5g   = 11,
        .ofdm64qam_5g     = 9,
        .ofdm256qam_5g    = 9,
        .ofdm1024qam_5g   = 9
    },
    .txpwr_ofst = {
        .enable       = 1,
        .chan_1_4     = 0,
        .chan_5_9     = 0,
        .chan_10_13   = 0,
        .chan_36_64   = 3,
        .chan_100_120 = -15,
        .chan_122_140 = -7,
        .chan_142_165 = 3,
    },
    .xtal_cap = {
        .enable        = 0,
        .xtal_cap      = 24,
        .xtal_cap_fine = 31,
    },
};

// userconfig
typedef struct
{
    txpwr_lvl_conf_t txpwr_lvl;
    txpwr_lvl_conf_v2_t txpwr_lvl_v2; // 8800DW
    txpwr_lvl_conf_v3_t txpwr_lvl_v3; // 8800D40L/D80
    txpwr_loss_conf_t txpwr_loss;
    txpwr_ofst_conf_t txpwr_ofst;
    txpwr_ofst2x_conf_t txpwr_ofst2x;
    xtal_cap_conf_t xtal_cap;
} userconfig_info_t;

userconfig_info_t userconfig_info = {
    .txpwr_lvl = {
        .enable           = 1,
        .dsss             = 17,
        .ofdmlowrate_2g4  = 15,
        .ofdm64qam_2g4    = 14,
        .ofdm256qam_2g4   = 13,
        .ofdm1024qam_2g4  = 13,
        .ofdmlowrate_5g   = 15,
        .ofdm64qam_5g     = 14,
        .ofdm256qam_5g    = 13,
        .ofdm1024qam_5g   = 13
    },
    .txpwr_lvl_v2 = {
        .enable             = 1,
        .pwrlvl_11b_11ag_2g4 =
            //1M,   2M,   5M5,  11M,  6M,   9M,   12M,  18M,  24M,  36M,  48M,  54M
            { 20,   20,   20,   20,   20,   20,   20,   20,   18,   18,   16,   16},
        .pwrlvl_11n_11ac_2g4 =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9
            { 20,   20,   20,   20,   18,   18,   16,   16,   16,   16},
        .pwrlvl_11ax_2g4 =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9, MCS10,MCS11
            { 20,   20,   20,   20,   18,   18,   16,   16,   16,   16,   15,   15},
    },
#if 0
    .txpwr_lvl_v3 = {
        .enable             = 1,
        .pwrlvl_11b_11ag_2g4 =
            //1M,   2M,   5M5,  11M,  6M,   9M,   12M,  18M,  24M,  36M,  48M,  54M
            { 18,   18,   18,   18,   18,   18,   18,   18,   16,   16,   15,   15},
        .pwrlvl_11n_11ac_2g4 =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9
            { 18,   18,   18,   18,   16,   16,   15,   15,   14,   14},
        .pwrlvl_11ax_2g4 =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9, MCS10,MCS11
            { 18,   18,   18,   18,   16,   16,   15,   15,   14,   14,   13,   13},
         .pwrlvl_11a_5g =
            //NA,   NA,   NA,   NA,   6M,   9M,   12M,  18M,  24M,  36M,  48M,  54M
            { 0x80, 0x80, 0x80, 0x80, 18,   18,   18,   18,   16,   16,   15,   15},
        .pwrlvl_11n_11ac_5g =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9
            { 18,   18,   18,   18,   16,   16,   15,   15,   14,   14},
        .pwrlvl_11ax_5g =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9, MCS10,MCS11
            { 18,   18,   18,   18,   16,   16,   14,   14,   13,   13,   12,   12},
    },
#else
     .txpwr_lvl_v3 = {
        .enable             = 1,
        .pwrlvl_11b_11ag_2g4 =
            //1M,   2M,   5M5,  11M,  6M,   9M,   12M,  18M,  24M,  36M,  48M,  54M
            { 21,   21,   21,   21,   21,   21,   21,   21,   19,   19,   17,   17},
        .pwrlvl_11n_11ac_2g4 =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9
            { 21,   21,   21,   21,   17,   17,   16,   16,   15,   15},
        .pwrlvl_11ax_2g4 =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9, MCS10,MCS11
            { 21,   21,   21,   21,   17,   17,   16,   16,   15,   15,   14,   14},
         .pwrlvl_11a_5g =
            //NA,   NA,   NA,   NA,   6M,   9M,   12M,  18M,  24M,  36M,  48M,  54M
            { 0x80, 0x80, 0x80, 0x80, 18,   18,   18,   18,   16,   16,   15,   15},
        .pwrlvl_11n_11ac_5g =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9
            { 18,   18,   18,   18,   16,   16,   15,   15,   14,   14},
        .pwrlvl_11ax_5g =
            //MCS0, MCS1, MCS2, MCS3, MCS4, MCS5, MCS6, MCS7, MCS8, MCS9, MCS10,MCS11
            { 18,   18,   18,   18,   16,   16,   14,   14,   13,   13,   12,   12},
    },
#endif
    .txpwr_loss = {
        .loss_enable_2g4 = 0,
        .loss_value_2g4 = 0,
        .loss_enable_5g = 0,
        .loss_value_5g = 0,
    },
#if 0
    .txpwr_ofst = {
        .enable       = 1,
        .chan_1_4     = 0,
        .chan_5_9     = 0,
        .chan_10_13   = 0,
        .chan_36_64   = -4,
        .chan_100_120 = -4,
        .chan_122_140 = 4,
        .chan_142_165 = 4,
    },
#else
    .txpwr_ofst = {
        .enable       = 1,
        .chan_1_4     = 0,
        .chan_5_9     = 0,
        .chan_10_13   = 2,
        .chan_36_64   = 0,
        .chan_100_120 = 0,
        .chan_122_140 = 0,
        .chan_142_165 = 0,
    },
#endif
    .txpwr_ofst2x = {
        .enable       = 1,
        .pwrofst2x_tbl_2g4 =
        { // ch1-4, ch5-9, ch10-13
            {   -7,    -7,    -5   }, // 11b
            {   -5,    -6,    -4  }, // ofdm_highrate
            {   0,    0,    0   }, // ofdm_lowrate
        },
        .pwrofst2x_tbl_5g =
        { // ch42,  ch58, ch106,ch122,ch138,ch155
            {   0,    0,    0,    0,    0,    0   }, // ofdm_lowrate
            {   0,    1,    0,    0,    0,    -5   }, // ofdm_highrate
            {   0,    1,    0,    0,    0,    -2   }, // ofdm_midrate
        },
    },
    .xtal_cap = {
        .enable        = 1,
        .xtal_cap      = 24,
        .xtal_cap_fine = 31,
    },
};

#ifdef CONFIG_LOAD_USERCONFIG
int rwnx_atoi(char *value)
{
    int len = 0;
    int i = 0;
    int result = 0;
    int flag = 1;

    if (value[0] == '-') {
        flag = -1;
        value++;
    }
    len = strlen(value);

    for (i = 0;i < len ;i++) {
        result = result * 10;
        if (value[i] >= 48 && value[i] <= 57) {
            result += value[i] - 48;
        } else {
            result = 0;
            break;
        }
    }

    return result * flag;
}

void get_nvram_txpwr_idx(txpwr_idx_conf_t *txpwr_idx)
{
    txpwr_idx->enable           = nvram_info.txpwr_idx.enable;
    txpwr_idx->dsss             = nvram_info.txpwr_idx.dsss;
    txpwr_idx->ofdmlowrate_2g4  = nvram_info.txpwr_idx.ofdmlowrate_2g4;
    txpwr_idx->ofdm64qam_2g4    = nvram_info.txpwr_idx.ofdm64qam_2g4;
    txpwr_idx->ofdm256qam_2g4   = nvram_info.txpwr_idx.ofdm256qam_2g4;
    txpwr_idx->ofdm1024qam_2g4  = nvram_info.txpwr_idx.ofdm1024qam_2g4;
    txpwr_idx->ofdmlowrate_5g   = nvram_info.txpwr_idx.ofdmlowrate_5g;
    txpwr_idx->ofdm64qam_5g     = nvram_info.txpwr_idx.ofdm64qam_5g;
    txpwr_idx->ofdm256qam_5g    = nvram_info.txpwr_idx.ofdm256qam_5g;
    txpwr_idx->ofdm1024qam_5g   = nvram_info.txpwr_idx.ofdm1024qam_5g;

    #if USRCFG_TXPWR_LOG
    printk("%s:enable:%d\r\n",          __func__, txpwr_idx->enable);
    printk("%s:dsss:%d\r\n",            __func__, txpwr_idx->dsss);
    printk("%s:ofdmlowrate_2g4:%d\r\n", __func__, txpwr_idx->ofdmlowrate_2g4);
    printk("%s:ofdm64qam_2g4:%d\r\n",   __func__, txpwr_idx->ofdm64qam_2g4);
    printk("%s:ofdm256qam_2g4:%d\r\n",  __func__, txpwr_idx->ofdm256qam_2g4);
    printk("%s:ofdm1024qam_2g4:%d\r\n", __func__, txpwr_idx->ofdm1024qam_2g4);
    printk("%s:ofdmlowrate_5g:%d\r\n",  __func__, txpwr_idx->ofdmlowrate_5g);
    printk("%s:ofdm64qam_5g:%d\r\n",    __func__, txpwr_idx->ofdm64qam_5g);
    printk("%s:ofdm256qam_5g:%d\r\n",   __func__, txpwr_idx->ofdm256qam_5g);
    printk("%s:ofdm1024qam_5g:%d\r\n",  __func__, txpwr_idx->ofdm1024qam_5g);
    #endif
}

void get_nvram_txpwr_ofst(txpwr_ofst_conf_t *txpwr_ofst)
{
    txpwr_ofst->enable       = nvram_info.txpwr_ofst.enable;
    txpwr_ofst->chan_1_4     = nvram_info.txpwr_ofst.chan_1_4;
    txpwr_ofst->chan_5_9     = nvram_info.txpwr_ofst.chan_5_9;
    txpwr_ofst->chan_10_13   = nvram_info.txpwr_ofst.chan_10_13;
    txpwr_ofst->chan_36_64   = nvram_info.txpwr_ofst.chan_36_64;
    txpwr_ofst->chan_100_120 = nvram_info.txpwr_ofst.chan_100_120;
    txpwr_ofst->chan_122_140 = nvram_info.txpwr_ofst.chan_122_140;
    txpwr_ofst->chan_142_165 = nvram_info.txpwr_ofst.chan_142_165;

#if USRCFG_TXPWR_LOG
    printk("%s:enable      :%d\r\n", __func__, txpwr_ofst->enable);
    printk("%s:chan_1_4    :%d\r\n", __func__, txpwr_ofst->chan_1_4);
    printk("%s:chan_5_9    :%d\r\n", __func__, txpwr_ofst->chan_5_9);
    printk("%s:chan_10_13  :%d\r\n", __func__, txpwr_ofst->chan_10_13);
    printk("%s:chan_36_64  :%d\r\n", __func__, txpwr_ofst->chan_36_64);
    printk("%s:chan_100_120:%d\r\n", __func__, txpwr_ofst->chan_100_120);
    printk("%s:chan_122_140:%d\r\n", __func__, txpwr_ofst->chan_122_140);
    printk("%s:chan_142_165:%d\r\n", __func__, txpwr_ofst->chan_142_165);
#endif
}

void get_nvram_xtal_cap(xtal_cap_conf_t *xtal_cap)
{
    *xtal_cap = nvram_info.xtal_cap;

    printk("%s:enable       :%d\r\n", __func__, xtal_cap->enable);
    printk("%s:xtal_cap     :%d\r\n", __func__, xtal_cap->xtal_cap);
    printk("%s:xtal_cap_fine:%d\r\n", __func__, xtal_cap->xtal_cap_fine);
}

void rwnx_plat_nvram_set_value(char *command, char *value)
{
    //TODO send command
    printk("%s:command=%s value=%s\n", __func__, command, value);
    if (!strcmp(command, "enable")) {
        nvram_info.txpwr_idx.enable = rwnx_atoi(value);
    } else if (!strcmp(command, "dsss")) {
        nvram_info.txpwr_idx.dsss = rwnx_atoi(value);
    } else if (!strcmp(command, "ofdmlowrate_2g4")) {
        nvram_info.txpwr_idx.ofdmlowrate_2g4 = rwnx_atoi(value);
    } else if (!strcmp(command, "ofdm64qam_2g4")) {
        nvram_info.txpwr_idx.ofdm64qam_2g4 = rwnx_atoi(value);
    } else if (!strcmp(command, "ofdm256qam_2g4")) {
        nvram_info.txpwr_idx.ofdm256qam_2g4 = rwnx_atoi(value);
    } else if (!strcmp(command, "ofdm1024qam_2g4")) {
        nvram_info.txpwr_idx.ofdm1024qam_2g4 = rwnx_atoi(value);
    } else if (!strcmp(command, "ofdmlowrate_5g")) {
        nvram_info.txpwr_idx.ofdmlowrate_5g = rwnx_atoi(value);
    } else if (!strcmp(command, "ofdm64qam_5g")) {
        nvram_info.txpwr_idx.ofdm64qam_5g = rwnx_atoi(value);
    } else if (!strcmp(command, "ofdm256qam_5g")) {
        nvram_info.txpwr_idx.ofdm256qam_5g = rwnx_atoi(value);
    } else if (!strcmp(command, "ofdm1024qam_5g")) {
        nvram_info.txpwr_idx.ofdm1024qam_5g = rwnx_atoi(value);
    } else if (!strcmp(command, "ofst_enable")) {
        nvram_info.txpwr_ofst.enable = rwnx_atoi(value);
    } else if (!strcmp(command, "ofst_chan_1_4")) {
        nvram_info.txpwr_ofst.chan_1_4 = rwnx_atoi(value);
    } else if (!strcmp(command, "ofst_chan_5_9")) {
        nvram_info.txpwr_ofst.chan_5_9 = rwnx_atoi(value);
    } else if (!strcmp(command, "ofst_chan_10_13")) {
        nvram_info.txpwr_ofst.chan_10_13 = rwnx_atoi(value);
    } else if (!strcmp(command, "ofst_chan_36_64")) {
        nvram_info.txpwr_ofst.chan_36_64 = rwnx_atoi(value);
    } else if (!strcmp(command, "ofst_chan_100_120")) {
        nvram_info.txpwr_ofst.chan_100_120 = rwnx_atoi(value);
    } else if (!strcmp(command, "ofst_chan_122_140")) {
        nvram_info.txpwr_ofst.chan_122_140 = rwnx_atoi(value);
    } else if (!strcmp(command, "ofst_chan_142_165")) {
        nvram_info.txpwr_ofst.chan_142_165 = rwnx_atoi(value);
    } else if (!strcmp(command, "xtal_enable")) {
        nvram_info.xtal_cap.enable = rwnx_atoi(value);
    } else if (!strcmp(command, "xtal_cap")) {
        nvram_info.xtal_cap.xtal_cap = rwnx_atoi(value);
    } else if (!strcmp(command, "xtal_cap_fine")) {
        nvram_info.xtal_cap.xtal_cap_fine = rwnx_atoi(value);
    } else {
        printk("invalid cmd: %s\n", command);
    }
}

void rwnx_plat_userconfig_parsing(char *buffer, int size)
{
    int i = 0;
    int parse_state = 0;
    char command[30];
    char value[100];
    int char_counter = 0;

    memset(command, 0, 30);
    memset(value, 0, 100);

    for (i = 0; i < size; i++) {
        //Send command or print nvram log when char is \r or \n
        if (buffer[i] == 0x0a || buffer[i] == 0x0d) {
            if (command[0] != 0 && value[0] != 0) {
                if (parse_state == PRINT) {
                    printk("%s:%s\r\n", __func__, value);
                } else if (parse_state == GET_VALUE) {
                    rwnx_plat_nvram_set_value(command, value);
                }
            }
            //Reset command value and char_counter
            memset(command, 0, 30);
            memset(value, 0, 100);
            char_counter = 0;
            parse_state = INIT;
            continue;
        }

        //Switch parser state
        if (parse_state == INIT) {
            if (buffer[i] == '#') {
                parse_state = PRINT;
                continue;
            } else if (buffer[i] == 0x0a || buffer[i] == 0x0d) {
                parse_state = INIT;
                continue;
            } else {
                parse_state = CMD;
            }
        }

        //Fill data to command and value
        if (parse_state == PRINT) {
            command[0] = 0x01;
            value[char_counter] = buffer[i];
            char_counter++;
        } else if (parse_state == CMD) {
            if (command[0] != 0 && buffer[i] == '=') {
                parse_state = GET_VALUE;
                char_counter = 0;
                continue;
            }
            command[char_counter] = buffer[i];
            char_counter++;
        } else if (parse_state == GET_VALUE) {
            value[char_counter] = buffer[i];
            char_counter++;
        }
    }
}


void get_userconfig_txpwr_lvl(txpwr_lvl_conf_t *txpwr_lvl)
{
    txpwr_lvl->enable           = userconfig_info.txpwr_lvl.enable;
    txpwr_lvl->dsss             = userconfig_info.txpwr_lvl.dsss;
    txpwr_lvl->ofdmlowrate_2g4  = userconfig_info.txpwr_lvl.ofdmlowrate_2g4;
    txpwr_lvl->ofdm64qam_2g4    = userconfig_info.txpwr_lvl.ofdm64qam_2g4;
    txpwr_lvl->ofdm256qam_2g4   = userconfig_info.txpwr_lvl.ofdm256qam_2g4;
    txpwr_lvl->ofdm1024qam_2g4  = userconfig_info.txpwr_lvl.ofdm1024qam_2g4;
    txpwr_lvl->ofdmlowrate_5g   = userconfig_info.txpwr_lvl.ofdmlowrate_5g;
    txpwr_lvl->ofdm64qam_5g     = userconfig_info.txpwr_lvl.ofdm64qam_5g;
    txpwr_lvl->ofdm256qam_5g    = userconfig_info.txpwr_lvl.ofdm256qam_5g;
    txpwr_lvl->ofdm1024qam_5g   = userconfig_info.txpwr_lvl.ofdm1024qam_5g;

    printk("%s:enable:%d\r\n",          __func__, txpwr_lvl->enable);
    printk("%s:dsss:%d\r\n",            __func__, txpwr_lvl->dsss);
    printk("%s:ofdmlowrate_2g4:%d\r\n", __func__, txpwr_lvl->ofdmlowrate_2g4);
    printk("%s:ofdm64qam_2g4:%d\r\n",   __func__, txpwr_lvl->ofdm64qam_2g4);
    printk("%s:ofdm256qam_2g4:%d\r\n",  __func__, txpwr_lvl->ofdm256qam_2g4);
    printk("%s:ofdm1024qam_2g4:%d\r\n", __func__, txpwr_lvl->ofdm1024qam_2g4);
    printk("%s:ofdmlowrate_5g:%d\r\n",  __func__, txpwr_lvl->ofdmlowrate_5g);
    printk("%s:ofdm64qam_5g:%d\r\n",    __func__, txpwr_lvl->ofdm64qam_5g);
    printk("%s:ofdm256qam_5g:%d\r\n",   __func__, txpwr_lvl->ofdm256qam_5g);
    printk("%s:ofdm1024qam_5g:%d\r\n",  __func__, txpwr_lvl->ofdm1024qam_5g);
}

void get_userconfig_txpwr_lvl_v2(txpwr_lvl_conf_v2_t *txpwr_lvl_v2)
{
    *txpwr_lvl_v2 = userconfig_info.txpwr_lvl_v2;
#if USRCFG_TXPWR_LOG
    printk("%s:enable:%d\r\n",               __func__, txpwr_lvl_v2->enable);
    printk("%s:lvl_11b_11ag_1m_2g4:%d\r\n",  __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[0]);
    printk("%s:lvl_11b_11ag_2m_2g4:%d\r\n",  __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[1]);
    printk("%s:lvl_11b_11ag_5m5_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[2]);
    printk("%s:lvl_11b_11ag_11m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[3]);
    printk("%s:lvl_11b_11ag_6m_2g4:%d\r\n",  __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[4]);
    printk("%s:lvl_11b_11ag_9m_2g4:%d\r\n",  __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[5]);
    printk("%s:lvl_11b_11ag_12m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[6]);
    printk("%s:lvl_11b_11ag_18m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[7]);
    printk("%s:lvl_11b_11ag_24m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[8]);
    printk("%s:lvl_11b_11ag_36m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[9]);
    printk("%s:lvl_11b_11ag_48m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[10]);
    printk("%s:lvl_11b_11ag_54m_2g4:%d\r\n", __func__, txpwr_lvl_v2->pwrlvl_11b_11ag_2g4[11]);
    printk("%s:lvl_11n_11ac_mcs0_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[0]);
    printk("%s:lvl_11n_11ac_mcs1_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[1]);
    printk("%s:lvl_11n_11ac_mcs2_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[2]);
    printk("%s:lvl_11n_11ac_mcs3_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[3]);
    printk("%s:lvl_11n_11ac_mcs4_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[4]);
    printk("%s:lvl_11n_11ac_mcs5_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[5]);
    printk("%s:lvl_11n_11ac_mcs6_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[6]);
    printk("%s:lvl_11n_11ac_mcs7_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[7]);
    printk("%s:lvl_11n_11ac_mcs8_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[8]);
    printk("%s:lvl_11n_11ac_mcs9_2g4:%d\r\n",__func__, txpwr_lvl_v2->pwrlvl_11n_11ac_2g4[9]);
    printk("%s:lvl_11ax_mcs0_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[0]);
    printk("%s:lvl_11ax_mcs1_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[1]);
    printk("%s:lvl_11ax_mcs2_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[2]);
    printk("%s:lvl_11ax_mcs3_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[3]);
    printk("%s:lvl_11ax_mcs4_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[4]);
    printk("%s:lvl_11ax_mcs5_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[5]);
    printk("%s:lvl_11ax_mcs6_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[6]);
    printk("%s:lvl_11ax_mcs7_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[7]);
    printk("%s:lvl_11ax_mcs8_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[8]);
    printk("%s:lvl_11ax_mcs9_2g4:%d\r\n",    __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[9]);
    printk("%s:lvl_11ax_mcs10_2g4:%d\r\n",   __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[10]);
    printk("%s:lvl_11ax_mcs11_2g4:%d\r\n",   __func__, txpwr_lvl_v2->pwrlvl_11ax_2g4[11]);
#endif
}

void get_userconfig_txpwr_lvl_v3(txpwr_lvl_conf_v3_t *txpwr_lvl_v3)
{
    *txpwr_lvl_v3 = userconfig_info.txpwr_lvl_v3;
#if USRCFG_TXPWR_LOG
    printk("%s:enable:%d\r\n",               __func__, txpwr_lvl_v3->enable);
    printk("%s:lvl_11b_11ag_1m_2g4:%d\r\n",  __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[0]);
    printk("%s:lvl_11b_11ag_2m_2g4:%d\r\n",  __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[1]);
    printk("%s:lvl_11b_11ag_5m5_2g4:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[2]);
    printk("%s:lvl_11b_11ag_11m_2g4:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[3]);
    printk("%s:lvl_11b_11ag_6m_2g4:%d\r\n",  __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[4]);
    printk("%s:lvl_11b_11ag_9m_2g4:%d\r\n",  __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[5]);
    printk("%s:lvl_11b_11ag_12m_2g4:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[6]);
    printk("%s:lvl_11b_11ag_18m_2g4:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[7]);
    printk("%s:lvl_11b_11ag_24m_2g4:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[8]);
    printk("%s:lvl_11b_11ag_36m_2g4:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[9]);
    printk("%s:lvl_11b_11ag_48m_2g4:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[10]);
    printk("%s:lvl_11b_11ag_54m_2g4:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11b_11ag_2g4[11]);
    printk("%s:lvl_11n_11ac_mcs0_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[0]);
    printk("%s:lvl_11n_11ac_mcs1_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[1]);
    printk("%s:lvl_11n_11ac_mcs2_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[2]);
    printk("%s:lvl_11n_11ac_mcs3_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[3]);
    printk("%s:lvl_11n_11ac_mcs4_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[4]);
    printk("%s:lvl_11n_11ac_mcs5_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[5]);
    printk("%s:lvl_11n_11ac_mcs6_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[6]);
    printk("%s:lvl_11n_11ac_mcs7_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[7]);
    printk("%s:lvl_11n_11ac_mcs8_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[8]);
    printk("%s:lvl_11n_11ac_mcs9_2g4:%d\r\n",__func__, txpwr_lvl_v3->pwrlvl_11n_11ac_2g4[9]);
    printk("%s:lvl_11ax_mcs0_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[0]);
    printk("%s:lvl_11ax_mcs1_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[1]);
    printk("%s:lvl_11ax_mcs2_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[2]);
    printk("%s:lvl_11ax_mcs3_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[3]);
    printk("%s:lvl_11ax_mcs4_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[4]);
    printk("%s:lvl_11ax_mcs5_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[5]);
    printk("%s:lvl_11ax_mcs6_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[6]);
    printk("%s:lvl_11ax_mcs7_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[7]);
    printk("%s:lvl_11ax_mcs8_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[8]);
    printk("%s:lvl_11ax_mcs9_2g4:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[9]);
    printk("%s:lvl_11ax_mcs10_2g4:%d\r\n",   __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[10]);
    printk("%s:lvl_11ax_mcs11_2g4:%d\r\n",   __func__, txpwr_lvl_v3->pwrlvl_11ax_2g4[11]);

    printk("%s:lvl_11a_1m_5g:%d\r\n",        __func__, txpwr_lvl_v3->pwrlvl_11a_5g[0]);
    printk("%s:lvl_11a_2m_5g:%d\r\n",        __func__, txpwr_lvl_v3->pwrlvl_11a_5g[1]);
    printk("%s:lvl_11a_5m5_5g:%d\r\n",       __func__, txpwr_lvl_v3->pwrlvl_11a_5g[2]);
    printk("%s:lvl_11a_11m_5g:%d\r\n",       __func__, txpwr_lvl_v3->pwrlvl_11a_5g[3]);
    printk("%s:lvl_11a_6m_5g:%d\r\n",        __func__, txpwr_lvl_v3->pwrlvl_11a_5g[4]);
    printk("%s:lvl_11a_9m_5g:%d\r\n",        __func__, txpwr_lvl_v3->pwrlvl_11a_5g[5]);
    printk("%s:lvl_11a_12m_5g:%d\r\n",       __func__, txpwr_lvl_v3->pwrlvl_11a_5g[6]);
    printk("%s:lvl_11a_18m_5g:%d\r\n",       __func__, txpwr_lvl_v3->pwrlvl_11a_5g[7]);
    printk("%s:lvl_11a_24m_5g:%d\r\n",       __func__, txpwr_lvl_v3->pwrlvl_11a_5g[8]);
    printk("%s:lvl_11a_36m_5g:%d\r\n",       __func__, txpwr_lvl_v3->pwrlvl_11a_5g[9]);
    printk("%s:lvl_11a_48m_5g:%d\r\n",       __func__, txpwr_lvl_v3->pwrlvl_11a_5g[10]);
    printk("%s:lvl_11a_54m_5g:%d\r\n",       __func__, txpwr_lvl_v3->pwrlvl_11a_5g[11]);
    printk("%s:lvl_11n_11ac_mcs0_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[0]);
    printk("%s:lvl_11n_11ac_mcs1_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[1]);
    printk("%s:lvl_11n_11ac_mcs2_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[2]);
    printk("%s:lvl_11n_11ac_mcs3_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[3]);
    printk("%s:lvl_11n_11ac_mcs4_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[4]);
    printk("%s:lvl_11n_11ac_mcs5_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[5]);
    printk("%s:lvl_11n_11ac_mcs6_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[6]);
    printk("%s:lvl_11n_11ac_mcs7_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[7]);
    printk("%s:lvl_11n_11ac_mcs8_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[8]);
    printk("%s:lvl_11n_11ac_mcs9_5g:%d\r\n", __func__, txpwr_lvl_v3->pwrlvl_11n_11ac_5g[9]);
    printk("%s:lvl_11ax_mcs0_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[0]);
    printk("%s:lvl_11ax_mcs1_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[1]);
    printk("%s:lvl_11ax_mcs2_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[2]);
    printk("%s:lvl_11ax_mcs3_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[3]);
    printk("%s:lvl_11ax_mcs4_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[4]);
    printk("%s:lvl_11ax_mcs5_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[5]);
    printk("%s:lvl_11ax_mcs6_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[6]);
    printk("%s:lvl_11ax_mcs7_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[7]);
    printk("%s:lvl_11ax_mcs8_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[8]);
    printk("%s:lvl_11ax_mcs9_5g:%d\r\n",     __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[9]);
    printk("%s:lvl_11ax_mcs10_5g:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[10]);
    printk("%s:lvl_11ax_mcs11_5g:%d\r\n",    __func__, txpwr_lvl_v3->pwrlvl_11ax_5g[11]);
    #endif
}

void get_userconfig_txpwr_ofst(txpwr_ofst_conf_t *txpwr_ofst)
{
    txpwr_ofst->enable       = userconfig_info.txpwr_ofst.enable;
    txpwr_ofst->chan_1_4     = userconfig_info.txpwr_ofst.chan_1_4;
    txpwr_ofst->chan_5_9     = userconfig_info.txpwr_ofst.chan_5_9;
    txpwr_ofst->chan_10_13   = userconfig_info.txpwr_ofst.chan_10_13;
    txpwr_ofst->chan_36_64   = userconfig_info.txpwr_ofst.chan_36_64;
    txpwr_ofst->chan_100_120 = userconfig_info.txpwr_ofst.chan_100_120;
    txpwr_ofst->chan_122_140 = userconfig_info.txpwr_ofst.chan_122_140;
    txpwr_ofst->chan_142_165 = userconfig_info.txpwr_ofst.chan_142_165;

   #if USRCFG_TXPWR_LOG
    printk("%s:enable      :%d\r\n", __func__, txpwr_ofst->enable);
    printk("%s:chan_1_4    :%d\r\n", __func__, txpwr_ofst->chan_1_4);
    printk("%s:chan_5_9    :%d\r\n", __func__, txpwr_ofst->chan_5_9);
    printk("%s:chan_10_13  :%d\r\n", __func__, txpwr_ofst->chan_10_13);
    printk("%s:chan_36_64  :%d\r\n", __func__, txpwr_ofst->chan_36_64);
    printk("%s:chan_100_120:%d\r\n", __func__, txpwr_ofst->chan_100_120);
    printk("%s:chan_122_140:%d\r\n", __func__, txpwr_ofst->chan_122_140);
    printk("%s:chan_142_165:%d\r\n", __func__, txpwr_ofst->chan_142_165);
    #endif
}

void get_userconfig_txpwr_ofst2x(txpwr_ofst2x_conf_t *txpwr_ofst2x)
{
    *txpwr_ofst2x = userconfig_info.txpwr_ofst2x;
#if 0
    printk("%s:enable      :%d\r\n", __func__, txpwr_ofst2x->enable);
    printk("pwrofst2x 2.4g: [0]:11b, [1]:ofdm_highrate, [2]:ofdm_lowrate\n"
        "  chan=" "\t1-4" "\t5-9" "\t10-13");
    for (type = 0; type < 3; type++) {
        printk("\n  [%d] =", type);
        for (ch_grp = 0; ch_grp < 3; ch_grp++) {
            printk("\t%d", txpwr_ofst2x->pwrofst2x_tbl_2g4[type][ch_grp]);
        }
    }
    printk("\npwrofst2x 5g: [0]:ofdm_lowrate, [1]:ofdm_highrate, [2]:ofdm_midrate\n"
        "  chan=" "\t36-50" "\t51-64" "\t98-114" "\t115-130" "\t131-146" "\t147-166");
    for (type = 0; type < 3; type++) {
        printk("\n  [%d] =", type);
        for (ch_grp = 0; ch_grp < 6; ch_grp++) {
            printk("\t%d", txpwr_ofst2x->pwrofst2x_tbl_5g[type][ch_grp]);
        }
    }
    printk("\n");
#endif
}
void get_userconfig_txpwr_loss(txpwr_loss_conf_t *txpwr_loss)
{
	txpwr_loss->loss_enable_2g4 = userconfig_info.txpwr_loss.loss_enable_2g4;
	txpwr_loss->loss_value_2g4 = userconfig_info.txpwr_loss.loss_value_2g4;
	txpwr_loss->loss_enable_5g = userconfig_info.txpwr_loss.loss_enable_5g;
	txpwr_loss->loss_value_5g = userconfig_info.txpwr_loss.loss_value_5g;

	printk("%s:loss_enable_2g4: %d, val_2g4: %d, loss_enable_5g: %d, val_5g: %d\r\n", __func__,
				txpwr_loss->loss_enable_2g4, txpwr_loss->loss_value_2g4,
				txpwr_loss->loss_enable_5g, txpwr_loss->loss_value_5g);
}

void get_userconfig_xtal_cap(xtal_cap_conf_t *xtal_cap)
{
    *xtal_cap = userconfig_info.xtal_cap;

    printk("%s:enable       :%d\r\n", __func__, xtal_cap->enable);
    printk("%s:xtal_cap     :%d\r\n", __func__, xtal_cap->xtal_cap);
    printk("%s:xtal_cap_fine:%d\r\n", __func__, xtal_cap->xtal_cap_fine);
}
uint32_t get_user_pwrlvl_11a_5g(void)
{
    return userconfig_info.txpwr_lvl_v3.pwrlvl_11a_5g[4];
}
uint32_t get_user_pwrlvl_11a_2g4(void)
{
    return userconfig_info.txpwr_lvl_v3.pwrlvl_11ax_2g4[0];
}
#endif

