/*
 * Copyright (C) 2018-2025 AICSemi Ltd.
 *
 * All Rights Reserved
 */

#include "lmac_msg.h"
#include "rwnx_platform.h"
#include "aic_log.h"
#include "mac.h"
#include <string.h>

#ifdef CONFIG_POWER_LIMIT

#define POWER_LEVEL_INVALID_VAL     (127)
#define POWER_LIMIT_INVALID_VAL     POWER_LEVEL_INVALID_VAL

#define POWER_LIMIT_CC_MATCHED_BIT  (0x1U << 0)

#define MAX_2_4G_BW_NUM    2
#define MAX_5G_BW_NUM      3
#define MAX_REGION_NUM     5

typedef struct
{
    u8_l ch_cnt_2g4[MAX_2_4G_BW_NUM];
    u8_l ch_cnt_5g[MAX_5G_BW_NUM];
    u8_l ch_num_2g4[MAX_2_4G_BW_NUM][MAC_DOMAINCHANNEL_24G_MAX];
    u8_l ch_num_5g[MAX_5G_BW_NUM][MAC_DOMAINCHANNEL_5G_MAX];
    s8_l max_pwr_2g4[MAX_2_4G_BW_NUM][MAC_DOMAINCHANNEL_24G_MAX];
    s8_l max_pwr_5g[MAX_5G_BW_NUM][MAC_DOMAINCHANNEL_5G_MAX];
} txpwr_lmt_info_t;

typedef struct
{
    u32_l flags;
    txpwr_lmt_info_t txpwr_lmt[MAX_REGION_NUM];
} powerlimit_info_t;

typedef enum {
	REGIONS_SRRC,
	REGIONS_FCC,
	REGIONS_ETSI,
	REGIONS_JP,
	REGIONS_DEFAULT,
} Regions_code;

powerlimit_info_t powerlimit_info = {
    .flags  = POWER_LIMIT_CC_MATCHED_BIT,
    .txpwr_lmt[REGIONS_SRRC] = {
                    /* 20M   40M */
        .ch_cnt_2g4 = { 13,   9 },
                    /* 20M   40M */
        .ch_cnt_5g  = { 25,   12 },
        /* 20M   chan_num */
        .ch_num_2g4[0] = {1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14},
        /* 40M   chan_num */
        .ch_num_2g4[1] = {1,  2,   3, 4, 5, 6, 7, 8, 9,10,11, 12,  13,  14},
        /* 20M 5G chan_num */
        .ch_num_5g[0] = {36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165},
        /* 40M 5G chan_num */
        .ch_num_5g[1] = {38,46,54,62,102,110,118,126,134,142,151,159},
        /* 20M 2G4 max pwr: 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14 */
        .max_pwr_2g4[0] = {15,16,16,16,16,16,16,16,16,16,16,12,12,0x80},
        /* 40M 2G4 max pwr: 1,  2,   3, 4, 5, 6, 7, 8, 9,10,11, 12,  13,  14 */
        .max_pwr_2g4[1] = {0x80,0x80,16,16,16,16,16,16,16,16,16,0x80,0x80,0x80},
        /* 20M 5G max pwr: 36,40,44,48,52,56,60,64,100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149,153,157,161,165 */
        .max_pwr_5g[0] = { 15,15,15,15,15,15,15,15,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,16, 16, 16, 16, 16},
        /* 40M 5G max pwr: 38,46,54,62,102, 110, 118, 126, 134, 142, 151,159 */
        .max_pwr_5g[1] = { 15,15,15,15,0x80,0x80,0x80,0x80,0x80,0x80,16, 16},
    },
    .txpwr_lmt[REGIONS_FCC] = {
                    /* 20M   40M */
        .ch_cnt_2g4 = { 13,   9 },
                    /* 20M   40M */
        .ch_cnt_5g  = { 25,   12 },
        /* 20M   chan_num */
        .ch_num_2g4[0] = {1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14},
        /* 40M   chan_num */
        .ch_num_2g4[1] = {1,  2,   3, 4, 5, 6, 7, 8, 9,10,11, 12,  13,  14},
        /* 20M 5G chan_num */
        .ch_num_5g[0] = {36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165},
        /* 40M 5G chan_num */
        .ch_num_5g[1] = {38,46,54,62,102,110,118,126,134,142,151,159},
        /* 20M 2G4 max pwr: 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14 */
        .max_pwr_2g4[0] = {15,16,16,16,16,16,16,16,16,16,16,12,12,0x80},
        /* 40M 2G4 max pwr: 1,  2,   3, 4, 5, 6, 7, 8, 9,10,11, 12,  13,  14 */
        .max_pwr_2g4[1] = {0x80,0x80,16,16,16,16,16,16,16,16,16,0x80,0x80,0x80},
        /* 20M 5G max pwr: 36,40,44,48,52,56,60,64,100,104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149,153,157,161,165 */
        .max_pwr_5g[0] = { 16,16,16,16,16,16,16,16,16, 16,  16,  16,  16,   16, 16,   16,  16,  16,  16, 0x80,16, 16, 16, 16, 16},
        /* 40M 5G max pwr: 38,46,54,62,102, 110, 118, 126, 134, 142, 151,159 */
        .max_pwr_5g[1] = { 16,16,16,16,16,  16,  16,  16,   16, 16,  16, 16 },
    },
    .txpwr_lmt[REGIONS_ETSI] = {
                    /* 20M   40M */
        .ch_cnt_2g4 = { 13,   9 },
                    /* 20M   40M */
        .ch_cnt_5g  = { 25,   12 },
        /* 20M   chan_num */
        .ch_num_2g4[0] = {1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14},
        /* 40M   chan_num */
        .ch_num_2g4[1] = {1,  2,   3, 4, 5, 6, 7, 8, 9,10,11, 12,  13,  14},
        /* 20M 5G chan_num */
        .ch_num_5g[0] = {36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165},
        /* 40M 5G chan_num */
        .ch_num_5g[1] = {38,46,54,62,102,110,118,126,134,142,151,159},
        /* 20M 2G4 max pwr: 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14 */
        .max_pwr_2g4[0] = {15,16,16,16,16,16,16,16,16,16,16,12,12,0x80},
        /* 40M 2G4 max pwr: 1,  2,   3, 4, 5, 6, 7, 8, 9,10,11, 12,  13,  14 */
        .max_pwr_2g4[1] = {0x80,0x80,16,16,16,16,16,16,16,16,16,0x80,0x80,0x80},
        /* 20M 5G max pwr: 36,40,44,48,52,56,60,64,100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149,153,157,161,165 */
        .max_pwr_5g[0] = { 16,16,16,16,16,16,16,16,16, 16,  16,  16,  16,   16, 16,   16,  16,  16,  16,  16,  11, 11, 11, 11, 11},
        /* 40M 5G max pwr: 38,46,54,62,102, 110, 118, 126, 134, 142, 151,159 */
        .max_pwr_5g[1] = { 16,16,16,16,16,  16,  16,  16,   16, 0x80, 11, 11},
    },
    .txpwr_lmt[REGIONS_JP] = {
                    /* 20M   40M */
        .ch_cnt_2g4 = { 13,   9 },
                    /* 20M   40M */
        .ch_cnt_5g  = { 25,   12 },
        /* 20M   chan_num */
        .ch_num_2g4[0] = {1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14},
        /* 40M   chan_num */
        .ch_num_2g4[1] = {1,  2,   3, 4, 5, 6, 7, 8, 9,10,11, 12,  13,  14},
        /* 20M 5G chan_num */
        .ch_num_5g[0] = {36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165},
        /* 40M 5G chan_num */
        .ch_num_5g[1] = {38,46,54,62,102,110,118,126,134,142,151,159},
        /* 20M 2G4 max pwr: 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14 */
        .max_pwr_2g4[0] = {15,16,16,16,16,16,16,16,16,16,16,12,12,12},
        /* 40M 2G4 max pwr: 1,  2,   3, 4, 5, 6, 7, 8, 9,10,11, 12,  13,  14 */
        .max_pwr_2g4[1] = {0x80,0x80,16,16,16,16,16,16,16,16,16,0x80,0x80,0x80},
        /* 20M 5G max pwr: 36,40,44,48,52,56,60,64,100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165 */
        .max_pwr_5g[0] = { 16,16,16,16,16,16,16,16,16, 16,  16,  16,  16,   16, 16,   16,  16,  16,  16,  16,  0x80,0x80,0x80,0x80,0x80},
        /* 40M 5G max pwr: 38,46,54,62,102, 110, 118, 126, 134, 142, 151, 159 */
        .max_pwr_5g[1] = { 16,16,16,16,16,  16,  16,  16,   16, 16,  0x80,0x80},
    },
    .txpwr_lmt[REGIONS_DEFAULT] = {
                    /* 20M   40M */
        .ch_cnt_2g4 = { 13,   9 },
                    /* 20M   40M */
        .ch_cnt_5g  = { 25,   12 },
        /* 20M   chan_num */
        .ch_num_2g4[0] = {1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14},
        /* 40M   chan_num */
        .ch_num_2g4[1] = {1,  2,   3, 4, 5, 6, 7, 8, 9,10,11, 12,  13,  14},
        /* 20M 5G chan_num */
        .ch_num_5g[0] = {36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165},
        /* 40M 5G chan_num */
        .ch_num_5g[1] = {38,46,54,62,102,110,118,126,134,142,151,159},
        /* 20M 2G4 max pwr: 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14 */
        .max_pwr_2g4[0] = {15,16,16,16,16,16,16,16,16,16,16,12,12,12},
        /* 40M 2G4 max pwr: 1,  2,   3, 4, 5, 6, 7, 8, 9,10,11, 12,  13,  14 */
        .max_pwr_2g4[1] = {0x80,0x80,16,16,16,16,16,16,16,16,16,0x80,0x80,0x80},
        /* 20M 5G max pwr: 36,40,44,48,52,56,60,64,100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149,153,157,161,165 */
        .max_pwr_5g[0] = { 15,15,15,15,15,15,15,15,15, 15,  15,  15,  15,   15, 15,   15,  15,  15,  15,  15,  11, 11, 11, 11, 11},
        /* 40M 5G max pwr: 38,46,54,62,102, 110, 118, 126, 134, 142, 151,159 */
        .max_pwr_5g[1] = { 15,15,15,15,15,  15,  15,  15,  15,  15,  11, 11},
    },
};

int8_t get_powerlimit_by_freq(uint8_t band, uint16_t freq, uint8_t r_idx)
{
	int8_t ret = POWER_LIMIT_INVALID_VAL;
	uint8_t idx;
	if (!(powerlimit_info.flags & POWER_LIMIT_CC_MATCHED_BIT)) {
		aic_dbg( "powerlimit flag not set\n");
		return ret;
	}

	if (band == PHY_BAND_2G4) {
		uint8_t idx_cnt = powerlimit_info.txpwr_lmt[r_idx].ch_cnt_2g4[0];
		for (idx = 0; idx < idx_cnt; idx++) {
			int ch_num = powerlimit_info.txpwr_lmt[r_idx].ch_num_2g4[0][idx];
			uint16_t freq_tmp = phy_channel_to_freq(PHY_BAND_2G4, ch_num);
			if (freq == freq_tmp) {
				ret = powerlimit_info.txpwr_lmt[r_idx].max_pwr_2g4[0][idx];
				// aic_dbg( "[%d]: ch=%d(freq=%d), pwr=%d\n", idx, ch_num, freq, ret);
				break;
			}
		}
		// if (idx == idx_cnt)
		// 	aic_dbg( "powerlimit search failed: band=%d freq=%d\n", band, freq);
	} else if (band == PHY_BAND_5G) {
		uint8_t idx_cnt = powerlimit_info.txpwr_lmt[r_idx].ch_cnt_5g[0];
		for (idx = 0; idx < idx_cnt; idx++) {
			int ch_num = powerlimit_info.txpwr_lmt[r_idx].ch_num_5g[0][idx];
			uint16_t freq_tmp = phy_channel_to_freq(PHY_BAND_5G, ch_num);
			if (freq == freq_tmp) {
				ret = powerlimit_info.txpwr_lmt[r_idx].max_pwr_5g[0][idx];
				// aic_dbg( "[%d]: ch=%d(freq=%d), pwr=%d\n", idx, ch_num, freq, ret);
				break;
			}
		}
		// if (idx == idx_cnt)
		// 	aic_dbg( "powerlimit search failed: band=%d freq=%d\n", band, freq);
	}
    return ret;
}

int8_t get_powerlimit_by_chnum(uint8_t chnum, uint8_t r_idx, uint8_t bw)
{
	int8_t ret = POWER_LIMIT_INVALID_VAL;
	uint8_t idx;
	if (!(powerlimit_info.flags & POWER_LIMIT_CC_MATCHED_BIT)) {
		aic_dbg( "powerlimit flag not set\n");
		return ret;
	}

	if (chnum <= 14) {
		uint8_t idx_cnt = powerlimit_info.txpwr_lmt[r_idx].ch_cnt_2g4[bw];
		for (idx = 0; idx < idx_cnt; idx++) {
			uint8_t ch_num = powerlimit_info.txpwr_lmt[r_idx].ch_num_2g4[bw][idx];
			if (chnum == ch_num) {
				ret = powerlimit_info.txpwr_lmt[r_idx].max_pwr_2g4[bw][idx];
				// aic_dbg( "[%d]: ch=%d, pwr=%d\n", idx, ch_num, ret);
				break;
			}
		}
		if (idx == idx_cnt)
			aic_dbg( "%s powerlimit search failed: chnum=%d, please confirm the center frequency\n",
					__func__, chnum);
	} else if (chnum <= 165) {
		uint8_t idx_cnt = powerlimit_info.txpwr_lmt[r_idx].ch_cnt_5g[bw];
		for (idx = 0; idx < idx_cnt; idx++) {
			uint8_t ch_num = powerlimit_info.txpwr_lmt[r_idx].ch_num_5g[bw][idx];
			if (chnum == ch_num) {
				ret = powerlimit_info.txpwr_lmt[r_idx].max_pwr_5g[bw][idx];
				// aic_dbg( "[%d]: ch=%d, pwr=%d\n", idx, ch_num, ret);
				break;
			}
		}
		if (idx == idx_cnt)
			aic_dbg( "%s powerlimit search failed: chnum=%d, please confirm the center frequency\n",
					__func__, chnum);
	}

	return ret;
}

typedef struct {
	char ccode[3];
	Regions_code region;
} reg_table;

/* If the region conflicts with the kernel, the actual authentication standard prevails */
reg_table reg_tables[] = {
	{.ccode = "CN", .region = REGIONS_SRRC},
	{.ccode = "US", .region = REGIONS_FCC},
	{.ccode = "DE", .region = REGIONS_ETSI},
	{.ccode = "00", .region = REGIONS_DEFAULT},
	{.ccode = "WW", .region = REGIONS_DEFAULT},
	{.ccode = "XX", .region = REGIONS_DEFAULT},
	{.ccode = "JP", .region = REGIONS_JP},
	{.ccode = "AD", .region = REGIONS_ETSI},
	{.ccode = "AE", .region = REGIONS_ETSI},
	{.ccode = "AF", .region = REGIONS_ETSI},
	{.ccode = "AI", .region = REGIONS_ETSI},
	{.ccode = "AL", .region = REGIONS_ETSI},
	{.ccode = "AM", .region = REGIONS_ETSI},
	{.ccode = "AN", .region = REGIONS_ETSI},
	{.ccode = "AR", .region = REGIONS_FCC},
	{.ccode = "AS", .region = REGIONS_FCC},
	{.ccode = "AT", .region = REGIONS_ETSI},
	{.ccode = "AU", .region = REGIONS_ETSI},
	{.ccode = "AW", .region = REGIONS_ETSI},
	{.ccode = "AZ", .region = REGIONS_ETSI},
	{.ccode = "BA", .region = REGIONS_ETSI},
	{.ccode = "BB", .region = REGIONS_FCC},
	{.ccode = "BD", .region = REGIONS_JP},
	{.ccode = "BE", .region = REGIONS_ETSI},
	{.ccode = "BF", .region = REGIONS_FCC},
	{.ccode = "BG", .region = REGIONS_ETSI},
	{.ccode = "BH", .region = REGIONS_ETSI},
	{.ccode = "BL", .region = REGIONS_ETSI},
	{.ccode = "BM", .region = REGIONS_FCC},
	{.ccode = "BN", .region = REGIONS_JP},
	{.ccode = "BO", .region = REGIONS_JP},
	{.ccode = "BR", .region = REGIONS_FCC},
	{.ccode = "BS", .region = REGIONS_FCC},
	{.ccode = "BT", .region = REGIONS_ETSI},
	{.ccode = "BW", .region = REGIONS_ETSI},
	{.ccode = "BY", .region = REGIONS_ETSI},
	{.ccode = "BZ", .region = REGIONS_JP},
	{.ccode = "CA", .region = REGIONS_FCC},
	{.ccode = "CF", .region = REGIONS_FCC},
	{.ccode = "CH", .region = REGIONS_ETSI},
	{.ccode = "CI", .region = REGIONS_FCC},
	{.ccode = "CL", .region = REGIONS_ETSI},
	{.ccode = "CO", .region = REGIONS_FCC},
	{.ccode = "CR", .region = REGIONS_FCC},
	{.ccode = "CX", .region = REGIONS_FCC},
	{.ccode = "CY", .region = REGIONS_ETSI},
	{.ccode = "CZ", .region = REGIONS_ETSI},
	{.ccode = "DK", .region = REGIONS_ETSI},
	{.ccode = "DM", .region = REGIONS_FCC},
	{.ccode = "DO", .region = REGIONS_FCC},
	{.ccode = "DZ", .region = REGIONS_JP},
	{.ccode = "EC", .region = REGIONS_FCC},
	{.ccode = "EE", .region = REGIONS_ETSI},
	{.ccode = "EG", .region = REGIONS_ETSI},
	{.ccode = "ES", .region = REGIONS_ETSI},
	{.ccode = "ET", .region = REGIONS_ETSI},
	{.ccode = "FI", .region = REGIONS_ETSI},
	{.ccode = "FM", .region = REGIONS_FCC},
	{.ccode = "FR", .region = REGIONS_ETSI},
	{.ccode = "GB", .region = REGIONS_ETSI},
	{.ccode = "GD", .region = REGIONS_FCC},
	{.ccode = "GE", .region = REGIONS_ETSI},
	{.ccode = "GF", .region = REGIONS_ETSI},
	{.ccode = "GH", .region = REGIONS_FCC},
	{.ccode = "GI", .region = REGIONS_ETSI},
	{.ccode = "GL", .region = REGIONS_ETSI},
	{.ccode = "GP", .region = REGIONS_ETSI},
	{.ccode = "GR", .region = REGIONS_ETSI},
	{.ccode = "GT", .region = REGIONS_FCC},
	{.ccode = "GU", .region = REGIONS_FCC},
	{.ccode = "GY", .region = REGIONS_DEFAULT},
	{.ccode = "HK", .region = REGIONS_ETSI},
	{.ccode = "HN", .region = REGIONS_FCC},
	{.ccode = "HR", .region = REGIONS_ETSI},
	{.ccode = "HT", .region = REGIONS_FCC},
	{.ccode = "HU", .region = REGIONS_ETSI},
	{.ccode = "ID", .region = REGIONS_ETSI},
	{.ccode = "IE", .region = REGIONS_ETSI},
	{.ccode = "IL", .region = REGIONS_ETSI},
	{.ccode = "IN", .region = REGIONS_ETSI},
	{.ccode = "IQ", .region = REGIONS_ETSI},
	{.ccode = "IR", .region = REGIONS_JP},
	{.ccode = "IS", .region = REGIONS_ETSI},
	{.ccode = "IT", .region = REGIONS_ETSI},
	{.ccode = "JM", .region = REGIONS_FCC},
	{.ccode = "JO", .region = REGIONS_ETSI},
	{.ccode = "KE", .region = REGIONS_ETSI},
	{.ccode = "KG", .region = REGIONS_ETSI},
	{.ccode = "KH", .region = REGIONS_ETSI},
	{.ccode = "KN", .region = REGIONS_ETSI},
	{.ccode = "KP", .region = REGIONS_JP},
	{.ccode = "KR", .region = REGIONS_ETSI},
	{.ccode = "KW", .region = REGIONS_ETSI},
	{.ccode = "KY", .region = REGIONS_FCC},
	{.ccode = "KZ", .region = REGIONS_DEFAULT},
	{.ccode = "LB", .region = REGIONS_ETSI},
	{.ccode = "LC", .region = REGIONS_ETSI},
	{.ccode = "LI", .region = REGIONS_ETSI},
	{.ccode = "LK", .region = REGIONS_FCC},
	{.ccode = "LS", .region = REGIONS_ETSI},
	{.ccode = "LT", .region = REGIONS_ETSI},
	{.ccode = "LU", .region = REGIONS_ETSI},
	{.ccode = "LV", .region = REGIONS_ETSI},
	{.ccode = "LY", .region = REGIONS_ETSI},
	{.ccode = "MA", .region = REGIONS_ETSI},
	{.ccode = "MC", .region = REGIONS_ETSI},
	{.ccode = "MD", .region = REGIONS_ETSI},
	{.ccode = "ME", .region = REGIONS_ETSI},
	{.ccode = "MF", .region = REGIONS_ETSI},
	{.ccode = "MH", .region = REGIONS_FCC},
	{.ccode = "MK", .region = REGIONS_ETSI},
	{.ccode = "MN", .region = REGIONS_ETSI},
	{.ccode = "MO", .region = REGIONS_ETSI},
	{.ccode = "MP", .region = REGIONS_FCC},
	{.ccode = "MQ", .region = REGIONS_ETSI},
	{.ccode = "MR", .region = REGIONS_ETSI},
	{.ccode = "MT", .region = REGIONS_ETSI},
	{.ccode = "MU", .region = REGIONS_FCC},
	{.ccode = "MV", .region = REGIONS_ETSI},
	{.ccode = "MW", .region = REGIONS_ETSI},
	{.ccode = "MX", .region = REGIONS_FCC},
	{.ccode = "MY", .region = REGIONS_FCC},
	{.ccode = "NA", .region = REGIONS_ETSI},
	{.ccode = "NG", .region = REGIONS_ETSI},
	{.ccode = "NI", .region = REGIONS_FCC},
	{.ccode = "NL", .region = REGIONS_ETSI},
	{.ccode = "NO", .region = REGIONS_ETSI},
	{.ccode = "NP", .region = REGIONS_JP},
	{.ccode = "NZ", .region = REGIONS_ETSI},
	{.ccode = "OM", .region = REGIONS_ETSI},
	{.ccode = "PA", .region = REGIONS_FCC},
	{.ccode = "PE", .region = REGIONS_FCC},
	{.ccode = "PF", .region = REGIONS_ETSI},
	{.ccode = "PG", .region = REGIONS_FCC},
	{.ccode = "PH", .region = REGIONS_FCC},
	{.ccode = "PK", .region = REGIONS_ETSI},
	{.ccode = "PL", .region = REGIONS_ETSI},
	{.ccode = "PM", .region = REGIONS_ETSI},
	{.ccode = "PR", .region = REGIONS_FCC},
	{.ccode = "PT", .region = REGIONS_ETSI},
	{.ccode = "PW", .region = REGIONS_FCC},
	{.ccode = "PY", .region = REGIONS_FCC},
	{.ccode = "QA", .region = REGIONS_ETSI},
	{.ccode = "RE", .region = REGIONS_ETSI},
	{.ccode = "RO", .region = REGIONS_ETSI},
	{.ccode = "RS", .region = REGIONS_ETSI},
	{.ccode = "RU", .region = REGIONS_ETSI},
	{.ccode = "RW", .region = REGIONS_FCC},
	{.ccode = "SA", .region = REGIONS_ETSI},
	{.ccode = "SE", .region = REGIONS_ETSI},
	{.ccode = "SG", .region = REGIONS_ETSI},
	{.ccode = "SI", .region = REGIONS_ETSI},
	{.ccode = "SK", .region = REGIONS_ETSI},
	{.ccode = "SM", .region = REGIONS_ETSI},
	{.ccode = "SN", .region = REGIONS_FCC},
	{.ccode = "SR", .region = REGIONS_ETSI},
	{.ccode = "SV", .region = REGIONS_FCC},
	{.ccode = "SY", .region = REGIONS_DEFAULT},
	{.ccode = "TC", .region = REGIONS_FCC},
	{.ccode = "TD", .region = REGIONS_ETSI},
	{.ccode = "TG", .region = REGIONS_ETSI},
	{.ccode = "TH", .region = REGIONS_FCC},
	{.ccode = "TJ", .region = REGIONS_ETSI},
	{.ccode = "TM", .region = REGIONS_ETSI},
	{.ccode = "TN", .region = REGIONS_ETSI},
	{.ccode = "TR", .region = REGIONS_ETSI},
	{.ccode = "TT", .region = REGIONS_FCC},
	{.ccode = "TW", .region = REGIONS_FCC},
	{.ccode = "UA", .region = REGIONS_ETSI},
	{.ccode = "UG", .region = REGIONS_FCC},
	{.ccode = "UY", .region = REGIONS_FCC},
	{.ccode = "UZ", .region = REGIONS_ETSI},
	{.ccode = "VC", .region = REGIONS_ETSI},
	{.ccode = "VE", .region = REGIONS_FCC},
	{.ccode = "VI", .region = REGIONS_FCC},
	{.ccode = "VN", .region = REGIONS_JP},
	{.ccode = "VU", .region = REGIONS_FCC},
	{.ccode = "WF", .region = REGIONS_ETSI},
	{.ccode = "YE", .region = REGIONS_DEFAULT},
	{.ccode = "YT", .region = REGIONS_ETSI},
	{.ccode = "ZA", .region = REGIONS_ETSI},
	{.ccode = "ZM", .region = REGIONS_ETSI},
	{.ccode = "ZW", .region = REGIONS_ETSI},
};

uint8_t get_ccode_region(char * ccode)
{
	int i, cnt;
	aic_dbg("%s ccode:%s\r\n", __func__, ccode);

	cnt = sizeof(reg_tables) / sizeof(reg_tables[0]);

	for (i = 0; i < cnt; i++) {
		if (reg_tables[i].ccode[0] == ccode[0] &&
			reg_tables[i].ccode[1] == ccode[1]) {
			aic_dbg("region: %d\r\n", reg_tables[i].region);
			return reg_tables[i].region;
		}
	}
	aic_dbg("use default region\r\n");
	return REGIONS_DEFAULT;
}
#endif