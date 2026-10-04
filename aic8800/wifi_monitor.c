/**
 ****************************************************************************************
 *
 * @file fhost_console_moniror.c
 *
 * @brief Implementation of the pre-moniror source code.
 *
 * monitor switch channel : monitor stop / monitor start freq ....
 ****************************************************************************************
 */

#include "rtos_al.h"
#include "rwnx_config.h"
#include "aic_log.h"
#include "fhost_api.h"
#include "fhost.h"
#include "mac_frame.h"
#include "fhost_config.h"
#include "fhost_cntrl.h"
#include "rwnx_msg_tx.h"
#include "rwnx_main.h"

#define MONITOR_VIF_IDX 1
#if NX_FHOST_MONITOR
void dump_b(const uint8_t *buf, uint16_t len)
{
    int i;

    for (i=0;i<len;i++) {
        if((i%8 == 0))
            printf("  ");
        printf("%02x ", buf[i]);
        if((i+1)%16 == 0)
            printf("\n");
    }
    printf("\n");
}
struct fhost_cntrl_link *monitor_cntrl_link = NULL;

/**
 ****************************************************************************************
 * @brief callback function
 *
 * Extract received packet informations (frame length, type, mac addr ...) in monitor mode
 *
 * @param[in] info  RX Frame information.
 * @param[in] arg   Not used
 ****************************************************************************************
 */
static void fhost_console_monitor_cb(struct fhost_frame_info *info, void *arg)
{
    if (info->payload == NULL) {
        printf("Unsupported frame: length = %d\r\n", info->length);
    } else {
        struct mac_hdr *hdr = (struct mac_hdr *)info->payload;
        uint8_t *adr1 = ((uint8_t *)(hdr->addr1.array));
        uint8_t *adr2 = ((uint8_t *)(hdr->addr2.array));
        uint8_t *adr3 = ((uint8_t *)(hdr->addr3.array));
        if ((hdr->fctl & MAC_FCTRL_TYPESUBTYPE_MASK) == MAC_FCTRL_BEACON) {
            return;
        }
        // if ( ((hdr->fctl & MAC_FCTRL_TYPESUBTYPE_MASK) == MAC_FCTRL_ACTION) || \
        //      ((hdr->fctl & MAC_FCTRL_TYPE_MASK) == MAC_FCTRL_DATA_T)) {
        //     printf("a1=%02x:%02x:%02x:%02x:%02x:%02x a2=%02x:%02x:%02x:%02x:%02x:%02x "
        //         "a3=%02x:%02x:%02x:%02x:%02x:%02x fc=%04X SN:%d len=%d\n",
        //     adr1[0], adr1[1], adr1[2], adr1[3], adr1[4], adr1[5],
        //         adr2[0], adr2[1], adr2[2], adr2[3], adr2[4], adr2[5],
        //         adr3[0], adr3[1], adr3[2], adr3[3], adr3[4], adr3[5],
        //         hdr->fctl, hdr->seq >> 4, info->length);
        //     rwnx_data_dump("Frame", info->payload, 32);
        //     // rwnx_data_dump("Ra", info->radiotap_hdr, info->radio_hdr_len);
        // }
        #ifdef CONFIG_MONITOR_RADIO_HEADER
        rtos_free(info->radiotap_hdr);
        #endif /* CONFIG_MONITOR_RADIO_HEADER */
    }
}
#define enDuplicateDetection     (1 << 31)
#define      acceptUnknown       (1 << 30)
#define acceptOtherDataFrames    (1 << 29)
#define      acceptQoSNull       (1 << 28)
#define    acceptQCFWOData       (1 << 27)
#define        acceptQData       (1 << 26)
#define     acceptCFWOData       (1 << 25)
#define         acceptData       (1 << 24)
#define acceptOtherCntrlFrames   (1 << 23)
#define        acceptCFEnd       (1 << 22)
#define          acceptACK       (1 << 21)
#define          acceptCTS       (1 << 20)
#define          acceptRTS       (1 << 19)
#define       acceptPSPoll       (1 << 18)
#define           acceptBA       (1 << 17)
#define          acceptBAR       (1 << 16)
#define acceptOtherMgmtFrames    (1 << 15)
#define  acceptBfmeeFrames       (1 << 14)
#define    acceptAllBeacon       (1 << 13)
#define acceptNotExpectedBA      (1 << 12)
#define acceptDecryptErrorFrames (1 << 11)
#define       acceptBeacon       (1 << 10)
#define    acceptProbeResp       (1 << 09)
#define     acceptProbeReq       (1 << 08)
#define    acceptMyUnicast       (1 << 07)
#define      acceptUnicast       (1 << 06)
#define  acceptErrorFrames       (1 << 05)
#define   acceptOtherBSSID       (1 << 04)
#define    acceptBroadcast       (1 << 03)
#define    acceptMulticast       (1 << 02)
#define        dontDecrypt       (1 << 01)
#define     excUnencrypted       (1 << 00)
/**
 ****************************************************************************************
 * @brief Process function for 'monitor' command
 *
 * monitor command can be used to start the monitor mode
 *
   @verbatim
     monitor start <chan num> <20|40|80|80+80|160>
     monitor stop
   @endverbatim
 *
 * @param[in] params monitor_start/stop commands above
 * @return 0 on success and !=0 if error occurred
 * @e.g   monitor start 36 20
 ****************************************************************************************
 */
int do_monitor(int argc, char * const argv[])
{
    struct fhost_vif_tag *fhost_vif;
    #ifndef ARRAY_SIZE
    #define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
    #endif

    const struct {
        const char *name;
        uint8_t val;
    } bwmap[] = {
        { .name = "20", .val = PHY_CHNL_BW_20, },
        { .name = "40", .val = PHY_CHNL_BW_40, },
        { .name = "80", .val = PHY_CHNL_BW_80, },
        { .name = "80+80", .val = PHY_CHNL_BW_80P80, },
        { .name = "160", .val = PHY_CHNL_BW_160, },
    };

    monitor_cntrl_link = fhost_cntrl_cfgrwnx_link_open();
    if (monitor_cntrl_link == NULL) {
        printf("Failed to open link with control task\n");
        ASSERT_ERR(0);
    }

    const u32 mem_addr = 0x40320060;
    struct dbg_mem_read_cfm rd_mem_addr_cfm;
    rwnx_send_dbg_mem_read_req(g_rwnx_hw, mem_addr, &rd_mem_addr_cfm);
    aic_dbg("rd_mem_addr_cfm.memdata %x\r\n ", rd_mem_addr_cfm.memdata);

    int fhost_vif_idx = MONITOR_VIF_IDX;
    int ret = 0;
    int chan_num = 6;
    int offset = 0;

    char *subcmd;
    subcmd = argv[1];
    printf("%s %s...\r\n", __func__, subcmd);
    if (!strcmp("start", subcmd)) {
        struct fhost_vif_monitor_cfg cfg;
        struct mac_chan_def *chan;
        unsigned int i, freq_offset;
        char /* *itf, */ *bw_str;

        #if 0
        if (argc < 6) {
            printf("wrong # of args\n");
            ret = -1;
            goto err;
        }

        itf = argv[2];
        // get the interface index
        fhost_vif_idx = fhost_console_search_itf(itf);
        if (fhost_vif_idx < 0) {
            ret = -2;
            goto err;
        }
        #endif

        // chan num
        chan_num = strtoul(argv[2], NULL, 0);
        cfg.chan.prim20_freq = phy_channel_to_freq((chan_num > 14), chan_num);
        chan = fhost_chan_get(cfg.chan.prim20_freq);
        if (chan == NULL) {
            printf("Invalid freq %d\n", cfg.chan.prim20_freq);
            ret = -3;
            goto err;
        }
        cfg.chan.band = chan->band;
        cfg.chan.tx_power = chan->tx_power;

        // by default 20Mhz bandwidth
        cfg.chan.type = PHY_CHNL_BW_20;
        cfg.chan.center1_freq = cfg.chan.prim20_freq;
        cfg.chan.center2_freq = 0;

        // bw
        bw_str = argv[3];
        for (i = 0; i < ARRAY_SIZE(bwmap); i++) {
            if (strcmp(bwmap[i].name, bw_str) == 0) {
                cfg.chan.type = bwmap[i].val;
                break;
            }
        }
        if (cfg.chan.type == PHY_CHNL_BW_40){
            uint8_t k = 0, found = 0;
            cfg.chan.type = PHY_CHNL_BW_40;
            if (chan_num > 14) {//PHY_BAND_5G
                int band5g_above_allowed[12] = {36,44,52,60,100,108,116,124,132,140,149,157};
                int band5g_below_allowed[11] = {40,48,56,64,104,112,120,128,136,153,161};
                for (k = 0; k < (sizeof(band5g_above_allowed) / sizeof(int)); k++) {
                    //HT40+
                    if (chan_num == band5g_above_allowed[k]) {
                        offset = 10;
                        found = 1;
                        break;
                    }
                }
                if (found == 0) {
                    //HT40-
                    for (k = 0; k < (sizeof(band5g_below_allowed) / sizeof(int)); k++) {
                        if (chan_num == band5g_below_allowed[k]) {
                            offset = -10;
                            found = 1;
                            break;
                        }
                    }
                }
                if (found == 0) {
                    cfg.chan.type = PHY_CHNL_BW_20; //20M only
                }
            }else {
                if (chan_num < 5) {
                    offset = 10;
                } else if (chan_num > 9) {
                    offset = -10;
                } else {
                    offset = 10;
                }
            }
        }
        // center1_freq
        cfg.chan.center1_freq = cfg.chan.prim20_freq + offset;

        if (cfg.chan.center1_freq > cfg.chan.prim20_freq)
            freq_offset = cfg.chan.center1_freq - cfg.chan.prim20_freq;
        else
            freq_offset = cfg.chan.prim20_freq - cfg.chan.center1_freq;

        switch(cfg.chan.type) {
            case PHY_CHNL_BW_20:
                if (freq_offset != 0) {
                    printf("monitor_start :"
                        "Center frequency of primary channel different from "
                        "frequency of primary channel in 20MHz (%d != %d)\n",
                        cfg.chan.center1_freq, cfg.chan.prim20_freq);
                    ret = -4;
                    goto err;
                }
                break;
            case PHY_CHNL_BW_40:
                if (freq_offset != 10) {
                    printf("monitor_start :"
                        "Center frequency of primary channel different from "
                        "frequency of primary channel +/- 10 in 40MHz (%d != %d)\n",
                        cfg.chan.center1_freq, cfg.chan.prim20_freq);
                    ret = -5;
                    goto err;
                }
                break;
            case PHY_CHNL_BW_80P80:
                //center2_freq
                if (argc < 7) {
                    printf("monitor_start :"
                        "Center frequency of secondary channel must be set\n");
                    ret = -6;
                    goto err;
                }
                cfg.chan.center2_freq = strtoul(argv[5], NULL, 0);

                //adjacent channel rejection
                if ((cfg.chan.center1_freq - cfg.chan.center2_freq == 80) ||
                    (cfg.chan.center2_freq - cfg.chan.center1_freq == 80)) {
                    printf("monitor_start :"
                        "Adjacent channel is not allowed, use 160MHz bandwidth\n");
                    ret = -7;
                    goto err;
                }

                // no break
            case PHY_CHNL_BW_80:
                if ((freq_offset != 10) && (freq_offset != 30)) {
                    printf("monitor_start :"
                        "Center frequency of primary channel different from "
                        "frequency of primary channel +/- 10 and frequency of"
                        "primary channel +/- 30 (%d != %d)\n",
                        cfg.chan.center1_freq, cfg.chan.prim20_freq);
                    ret = -8;
                    goto err;
                }
                break;
            case PHY_CHNL_BW_160:
                if ((freq_offset != 10) && (freq_offset != 30) &&
                    (freq_offset != 50) && (freq_offset != 70)) {
                    printf("monitor_start :"
                        "Center frequency of primary channel must belong to the range:"
                        "frequency of primary channel +/- [10, 30, 50, 70]\n");
                    ret = -9;
                    goto err;
                }
                break;
            default:
                printf("monitor_start :"
                    "Invalid bandwidth %d\n", cfg.chan.type);
                ret = -10;
                goto err;
        }

        fhost_vif = &fhost_env.vif[fhost_vif_idx];
        fhost_vif->net_if.state = fhost_vif;

        if (fhost_set_vif_type(monitor_cntrl_link, fhost_vif_idx, VIF_MONITOR, false)) {
            printf("Error while enabling monitor mode\n");
            ret = -11;
            goto err;
        }

        cfg.uf = false;
        cfg.cb = fhost_console_monitor_cb;
        cfg.cb_arg = NULL;
        aic_dbg("Monitor chan %d %d %d\r\n ", cfg.chan.prim20_freq, cfg.chan.center1_freq, cfg.chan.type);
        if (fhost_cntrl_monitor_cfg(monitor_cntrl_link, fhost_vif_idx, &cfg)) {
            printf("Error while configuring monitor mode\n");
            ret = -12;
            goto err;
        }

        fhost_cntrl_mm_set_filter(acceptOtherDataFrames | acceptQCFWOData | acceptQData | acceptCFWOData | acceptData \
                                | acceptOtherCntrlFrames | acceptOtherMgmtFrames);
        rwnx_send_dbg_mem_read_req(g_rwnx_hw, mem_addr, &rd_mem_addr_cfm);
        aic_dbg("Rx filter %x\r\n ", rd_mem_addr_cfm.memdata);
    } else if (!strcmp("stop", subcmd)) {
        //char *itf;

        #if 0
        itf = argv[2];
        // get the interface index
        fhost_vif_idx = fhost_console_search_itf(itf);
        if (fhost_vif_idx < 0) {
            ret = -13;
            goto err;
        }
        #endif

        if (fhost_set_vif_type(monitor_cntrl_link, fhost_vif_idx, VIF_STA, false)) {
            printf("Error while disabling monitor mode\n");
            ret = -14;
            goto err;
        }

    } else {
        printf("monitor: invalid subcmd\n");
        ret = -15;
        goto err;
    }
err:
    fhost_cntrl_cfgrwnx_link_close(monitor_cntrl_link);
    return ret;
}
int do_monitor_tx(int argc, char * const argv[])
{
    uint8_t pkt[] = {
                    0x40,0x00,0x00,0x00,                  // probe request
                    0xff,0xff,0xff,0xff,0xff,0xff,
                    0x82,0xc6,0x2f,0xad,0x5d,0xc1,        //SA
                    0xff,0xff,0xff,0xff,0xff,0xff,
                    0xe0,0xd3,0x00,0x00,0x01,0x08,0x82,0x84,0x8b,0x0c,
                    0x12,0x96,0x18,0x24,0x03,0x01,0x01,0x32,0x04,0x30,0x48,0x60,0x6c,0x7f,0x0a,0x01,
                    0x00,0x08,0x00,0x00,0x00,0x00,0x40,0x70,0x20,0x2d,0x1a,0xef,0x01,0x17,0xff,0xff,
                    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,
                    0x00,0x00,0x00,0x00,0x00,0xdd,0x08,0x00,0xe0,0xfc,0x81,0xf1,0x1e,0x05,0x00,0xdd,
                    0x07,0x00,0x50,0xf2,0x08,0x00,0x19,0x00
    };
    //memcpy(&pkt[10], get_mac_address(), 6);
    fhost_send_80211_frame(MONITOR_VIF_IDX, pkt, sizeof(pkt), NULL, NULL);
    return 0;
}
int do_monitor_set_filter(int argc, char * const argv[])
{
    uint32_t val = strtoul(argv[1], NULL, 16);

    aic_dbg("val %x\r\n", val);
    fhost_cntrl_mm_set_filter(val);

    return 0;
}
int do_monitor_set_channel(int argc, char * const argv[])
{
    uint32_t band_width = strtoul(argv[1], NULL, 10); //0: 20M, 1:40M
    uint32_t chan_num   = strtoul(argv[2], NULL, 10);

    aic_dbg("chan_numal %d, band_width %d\r\n", chan_num, band_width);
    fhost_cntrl_mm_set_monitor_channel(MONITOR_VIF_IDX, chan_num, band_width);

    return 0;
}
#endif // NX_FHOST_MONITOR

