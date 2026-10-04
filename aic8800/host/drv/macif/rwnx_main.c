/**
 ******************************************************************************
 *
 * @file rwnx_main.c
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */

#include <string.h>

#include "rwnx_main.h"
#include "rwnx_platform.h"
#include "reg_access.h"
#include "sdio_port.h"
#include "aic_log.h"
#include "fhost_config.h"
#include "rwnx_defs.h"
#include "rwnx_msg_tx.h"
#include "wlan_if.h"
#include "wifi.h"
#include "fhost.h"
#include "rwnx_radar.h"


#define ASSOC_REQ 0x00
#define ASSOC_RSP 0x10
#define PROBE_REQ 0x40
#define PROBE_RSP 0x50
#define ACTION 0xD0
#define AUTH 0xB0
#define DEAUTH 0xC0
#define QOS 0x88

#define ACTION_MAC_HDR_LEN 24
#define QOS_MAC_HDR_LEN 26

u8 chip_id = 0;
u8 chip_sub_id = 0;
u8 chip_mcu_id = 0;

bool_l func_flag_tx = true;
bool_l func_flag_rx = true;

int adap_test = 0;

enum sta_mgmt_msg_id {
    APM_STA_LOSS       = 0,
    STA_TX_PWR_UPDATE  = 1,
    PRO_DISC_REQ       = 2,
};
struct sta_mgmt_msg_st
{
    enum sta_mgmt_msg_id msg_id;
    u8_l mac_addr[6];
};
void rwnx_frame_parser(char* tag, char* data, unsigned long len){
	char print_data[100];
	int print_index = 0;

	memset(print_data, 0, 100);

	if(data[0] == ASSOC_REQ){
		sprintf(&print_data[print_index], "%s %s %s", __func__, tag, "ASSOC_REQ \r\n");
	}else if(data[0] == ASSOC_RSP){
		sprintf(&print_data[print_index], "%s %s %s", __func__, tag, "ASSOC_RSP \r\n");
	}else if(data[0] == PROBE_REQ){
		sprintf(&print_data[print_index], "%s %s %s", __func__, tag, "PROBE_REQ \r\n");
	}else if(data[0] == PROBE_RSP){
		sprintf(&print_data[print_index], "%s %s %s", __func__, tag, "PROBE_RSP \r\n");
	}else if(data[0] == ACTION){
		sprintf(&print_data[print_index], "%s %s %s", __func__, tag, "ACTION ");
		print_index = strlen(print_data);
		if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x00){
			sprintf(&print_data[print_index], "%s", "GO_NEG_REQ \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x01){
			sprintf(&print_data[print_index], "%s", "GO_NEG_RSP \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x02){
			sprintf(&print_data[print_index], "%s", "GO_NEG_CFM \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x03){
			sprintf(&print_data[print_index], "%s", "P2P_INV_REQ \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x04){
			sprintf(&print_data[print_index], "%s", "P2P_INV_RSP \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x05){
			sprintf(&print_data[print_index], "%s", "DD_REQ \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x06){
			sprintf(&print_data[print_index], "%s", "DD_RSP \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x07){
			sprintf(&print_data[print_index], "%s", "PD_REQ \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x08){
			sprintf(&print_data[print_index], "%s", "PD_RSP \r\n");
		}else{
			sprintf(&print_data[print_index], "%s", "UNKNOW \r\n");
		}

	}else if(data[0] == AUTH){
		sprintf(&print_data[print_index], "%s %s %s", __func__, tag, "AUTH \r\n");
	}else if(data[0] == DEAUTH){
		sprintf(&print_data[print_index], "%s %s %s", __func__, tag, "DEAUTH \r\n");
	}else if(data[0] == QOS){
		if(data[QOS_MAC_HDR_LEN + 6] == 0x88 && data[QOS_MAC_HDR_LEN + 7] == 0x8E){
			sprintf(&print_data[print_index], "%s %s %s", __func__, tag, "QOS_802.1X ");
			print_index = strlen(print_data);
			if(data[QOS_MAC_HDR_LEN + 9] == 0x03){
				sprintf(&print_data[print_index], "%s", "EAPOL \r\n");
			}else if(data[QOS_MAC_HDR_LEN + 9] == 0x00){
				sprintf(&print_data[print_index], "%s", "EAP_PACKAGE ");
				print_index = strlen(print_data);
				if(data[QOS_MAC_HDR_LEN + 12] == 0x01){
					sprintf(&print_data[print_index], "%s", "EAP_REQ \r\n");
				}else if(data[QOS_MAC_HDR_LEN + 12] == 0x02){
					sprintf(&print_data[print_index], "%s", "EAP_RSP \r\n");
				}else if(data[QOS_MAC_HDR_LEN + 12] == 0x04){
					sprintf(&print_data[print_index], "%s", "EAP_FAIL \r\n");
				}else{
					sprintf(&print_data[print_index], "%s", "UNKNOW \r\n");
				}
			}else if(data[QOS_MAC_HDR_LEN + 9] == 0x01){
				sprintf(&print_data[print_index], "%s","EAP_START \r\n");
			}
		}
	}

	if(print_index > 0){
		aic_dbg("%s", print_data);
	}

#if 0
	if(data[0] == ASSOC_REQ){
		printk("%s %s ASSOC_REQ \r\n", __func__, tag);
	}else if(data[0] == ASSOC_RSP){
		printk("%s %s ASSOC_RSP \r\n", __func__, tag);
	}else if(data[0] == PROBE_REQ){
		printk("%s %s PROBE_REQ \r\n", __func__, tag);
	}else if(data[0] == PROBE_RSP){
		printk("%s %s PROBE_RSP \r\n", __func__, tag);
	}else if(data[0] == ACTION){
		printk("%s %s ACTION ", __func__, tag);
		if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x00){
			printk("GO NEG REQ \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x01){
			printk("GO NEG RSP \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x02){
			printk("GO NEG CFM \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x03){
			printk("P2P INV REQ \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x04){
			printk("P2P INV RSP \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x05){
			printk("DD REQ \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x06){
			printk("DD RSP \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x07){
			printk("PD REQ \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x08){
			printk("PD RSP \r\n");
		}else{
			printk("\r\n");
		}

	}else if(data[0] == AUTH){
		printk("%s %s AUTH \r\n", __func__, tag);
	}else if(data[0] == DEAUTH){
		printk("%s %s DEAUTH \r\n", __func__, tag);
	}else if(data[0] == QOS){
		if(data[QOS_MAC_HDR_LEN + 6] == 0x88 && data[QOS_MAC_HDR_LEN + 7] == 0x8E){
			printk("%s %s QOS 802.1X ", __func__, tag);
			if(data[QOS_MAC_HDR_LEN + 9] == 0x03){
				printk("EAPOL");
			}else if(data[QOS_MAC_HDR_LEN + 9] == 0x00){
				printk("EAP PACKAGE ");
				if(data[QOS_MAC_HDR_LEN + 12] == 0x01){
					printk("EAP REQ\r\n");
				}else if(data[QOS_MAC_HDR_LEN + 12] == 0x02){
					printk("EAP RSP\r\n");
				}else if(data[QOS_MAC_HDR_LEN + 12] == 0x04){
					printk("EAP FAIL\r\n");
				}else{
					printk("\r\n");
				}
			}else if(data[QOS_MAC_HDR_LEN + 9] == 0x01){
				aic_dbg("EAP START \r\n");

			}
		}
	}
#endif
}

#if 0
void rwnx_data_dump(char* tag, void* data, unsigned long len){
	unsigned long i = 0;
	unsigned char* data_ = (unsigned char* )data;

	aic_dbg("%s %s len:(%lu)\r\n", __func__, tag, len);

	for (i = 0; i < len; i += 16){
		aic_dbg("%02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
			data_[0 + i],
			data_[1 + i],
			data_[2 + i],
			data_[3 + i],
			data_[4 + i],
			data_[5 + i],
			data_[6 + i],
			data_[7 + i],
			data_[8 + i],
			data_[9 + i],
			data_[10 + i],
			data_[11 + i],
			data_[12 + i],
			data_[13 + i],
			data_[14 + i],
			data_[15 + i]);
	}
}
#else
void rwnx_data_dump(char* tag, void* data, unsigned long len){
    unsigned long i = 0;
    unsigned char* data_ = (unsigned char* )data;

    aic_dbg("%s %s len:(%lu)\r\n", __func__, tag, len);

    for (i = 0; i < len; i += 16){
        aic_dbg("%02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
            data_[0 + i],
            data_[1 + i],
            data_[2 + i],
            data_[3 + i],
            data_[4 + i],
            data_[5 + i],
            data_[6 + i],
            data_[7 + i],
            data_[8 + i],
            data_[9 + i],
            data_[10 + i],
            data_[11 + i],
            data_[12 + i],
            data_[13 + i],
            data_[14 + i],
            data_[15 + i]);
    }

    for(i = 0; i < len; i++){
        if(data_[i] == 0x63 &&
            data_[i-1] == 0x53 &&
            data_[i-2] == 0x82 &&
            data_[i-3] == 0x63){
            if(data_[i+3] == 0x01){
                aic_dbg("%s DHCP DISCOVER\r\n", tag);
                break;
            }else if(data_[i+3] == 0x02){
                aic_dbg("%s DHCP OFFER\r\n", tag);
                break;
            }else if(data_[i+3] == 0x03){
                aic_dbg("%s DHCP REQUEST\r\n", tag);
                break;
            }else if(data_[i+3] == 0x05){
                aic_dbg("%s DHCP ACK\r\n", tag);
                break;
            }
        }
    }

}
#endif

#ifdef CONFIG_USB_SUPPORT
u32 patch_tbl[][2] =
{
    #if defined(CONFIG_TX_ADAPTIVITY)
    {0x0004, 0x0000320A}, //linkloss_thd
    #endif
    {0x0044, 0x00000002}, //hosttype
    {0x0048, 0x00000060}, //pkt_cnt_128=0x60, pkt_cnt_1600=0x00
    #if defined(CONFIG_MCU_MESSAGE)
    {0x004c, 0x00000040}, //pkt_cnt_1724=0x40
    {0x0050, 0x0011FC00}, //ipc_base_addr
    #else
    {0x004c, 0x00000046},
    {0x0050, 0x00000000}, //ipc base
    #endif
    {0x0054, 0x001a0000}, //buf base
    {0x0058, 0x001a0140}, //desc base
    {0x005c, 0x00001020}, //desc size
    {0x0060, 0x001a1020}, //pkt base
    {0x0064, 0x000207e0}, //pkt size
    {0x0068, 0x00000008},
    {0x006c, 0x00000040},
    {0x0070, 0x00000040},
    {0x0074, 0x00000020},
    {0x0078, 0x00000000},
    {0x007c, 0x00000040},
    {0x0080, 0x00190000}, //ringbuf
    {0x0084, 0x0000fc00}, //63kB
    {0x0088, 0x0019fc00},
    #if defined(CONFIG_TX_ADAPTIVITY)
    {0x0094, 0x00000000}, //ac_param_conf
    #endif
    {0x00A8, 0x8F080103}, //dm
    #ifdef USE_5G
    {0x00b4, 0xf3010001},
    #else
    {0x00b4, 0xf3010000},
    #endif
    //{0x00b8, 0x0f010a01}, //tx enhanced en, tx enhanced lo rate
    {0x00BC, 0x0A080108}, //txgain_enhance_highrate, pwridx_en, dsss, lowrate_2g4
    {0x00C0, 0x09080808}, //64qam_2g4, 256qam_2g4, 1024qam_2g4, lowrate_5g
    {0x00C4, 0x00080909}, //64qam_5g, 256qam_5g, 1024qam_5g, pwrofst_en
    //{0x00D0, 0x00010103}, //aon sram
    //{0x00D4, 0x0000087C},
    //{0x00D8, 0x001C0000}, //bt base
    //{0x00DC, 0x00008000}, //bt size
#if 1
    {0x00E0, 0x04020A08}, //fc param
    {0x00E4, 0x00000001}, //fc algo1
#else
    {0x00E0, 0x04010703}, //fc param
    {0x00E4, 0x00000000}, //fc algo0
#endif
    #if defined(CONFIG_TX_ADAPTIVITY)
    {0x00F8, 0x00010138}, //tx_adaptivity_en
    #endif
};
#endif

#ifdef CONFIG_SDIO_SUPPORT
u32 adaptivity_patch_tbl[][2] = {
	{0x0004, 0x0000320A}, //linkloss_thd
    {0x0094, 0x00000000}, //ac_param_conf
	{0x00F8, 0x00010138}, //tx_adaptivity_en
};
u32 patch_tbl[][2] =
{
#if 0 //!defined(CONFIG_LINK_DET_5G)
    {0x0104, 0x00000000}, //link_det_5g
#endif
};
#endif

#if defined(CONFIG_AIC8801)
static void patch_config(struct rwnx_hw *rwnx_hw)
{
    #ifdef CONFIG_ROM_PATCH_EN
    const u32 rd_patch_addr = 0x10180;
    #else
    const u32 rd_patch_addr = RAM_FMAC_FW_ADDR + 0x0180;
    #endif
    u32 config_base;
    uint32_t start_addr = 0x1e6000;
    u32 patch_addr = start_addr;
    u32 patch_num = sizeof(patch_tbl)/4;
    struct dbg_mem_read_cfm rd_patch_addr_cfm;
    u32 patch_addr_reg = 0x1e5318;
    u32 patch_num_reg = 0x1e531c;    
    int ret = 0;
    u16 cnt = 0;
    int tmp_cnt = 0;
	int adap_patch_num = 0;

    if (rwnx_hw->mode == WIFI_MODE_RFTEST) {
        patch_addr_reg = 0x1e5304;
		patch_num_reg = 0x1e5308;
    }

    printk("Read FW mem: %08x\n", rd_patch_addr);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, rd_patch_addr, &rd_patch_addr_cfm);
    if (ret) {
        printk("patch rd fail\n");
    }
    printk("%x=%x\n", rd_patch_addr_cfm.memaddr, rd_patch_addr_cfm.memdata);

    config_base = rd_patch_addr_cfm.memdata;

    ret = rwnx_send_dbg_mem_write_req(rwnx_hw, patch_addr_reg, patch_addr);
    if (ret) {
        printk("%x write fail\n", patch_addr_reg);
    }

	if(adap_test){
		printk("%s for adaptivity test \r\n", __func__);
		adap_patch_num = sizeof(adaptivity_patch_tbl)/4;
		ret = rwnx_send_dbg_mem_write_req(rwnx_hw, patch_num_reg, patch_num + adap_patch_num);
	}else{
        ret = rwnx_send_dbg_mem_write_req(rwnx_hw, patch_num_reg, patch_num);
	}
    if (ret) {
        printk("%x write fail\n", patch_num_reg);
    }

    for (cnt = 0; cnt < patch_num/2; cnt+=1) {
        ret = rwnx_send_dbg_mem_write_req(rwnx_hw, start_addr+8*cnt, patch_tbl[cnt][0]+config_base);
        if (ret) {
            printk("%x write fail\n", start_addr+8*cnt);
        }
        ret = rwnx_send_dbg_mem_write_req(rwnx_hw, start_addr+8*cnt+4, patch_tbl[cnt][1]);
        if (ret) {
            printk("%x write fail\n", start_addr+8*cnt+4);
        }
    }

    if(adap_test){
		for(cnt = 0; cnt < adap_patch_num/2; cnt+=1)
		{
			if((ret = rwnx_send_dbg_mem_write_req(rwnx_hw, start_addr+8*(cnt+tmp_cnt), adaptivity_patch_tbl[cnt][0]+config_base))) {
				printk("%x write fail\n", start_addr+8*cnt);
			}

			if((ret = rwnx_send_dbg_mem_write_req(rwnx_hw, start_addr+8*(cnt+tmp_cnt)+4, adaptivity_patch_tbl[cnt][1]))) {
				printk("%x write fail\n", start_addr+8*cnt+4);
			}
		}
	}
}
#endif /* CONFIG_AIC8801 */

uint32_t ldpc_cfg_ram[] = {
#if 0//def CONFIG_FPGA_VERIFICATION
    0x00363638,
    0x1DF8F834,
    0x1DF8F834,
    0x1DF8F834,
    0x1DF8F834,
    0x002F2F31,
    0x1DF8F82C,
    0x1DF8F82C,
    0x1DF8F82C,
    0x1DF8F82C,
    0x00363639,
    0x1AA5F834,
    0x1AA5F834,
    0x1ADEF834,
    0x1ADEF834,
    0x003A3A3E,
    0x1578F436,
    0x1578F436,
    0x1578F436,
    0x15B6F436,
    0x003B3B40,
    0x1DF8F838,
    0x1DF8F838,
    0x1DF8F838,
    0x1DF8F838,
    0x003B3B41,
    0x1DC4F838,
    0x1DC4F838,
    0x1DF8F838,
    0x1DF8F838,
    0x003B3B40,
    0x1781F838,
    0x1781F838,
    0x1781F838,
    0x17C4F838,
    0x003B3B40,
    0x0E81F838,
    0x0E81F838,
    0x0E81F838,
    0x0E82F838,
    0x003F3F43,
    0x1A92F83D,
    0x1A92F83E,
    0x1A92F83D,
    0x1ADDF83D,
    0x00272729,
    0x1DF8F824,
    0x1DF8F824,
    0x1DF8F843,
    0x1DF8F843,
    0x00272729,
    0x1DF8F824,
    0x1DF8F824,
    0x1DF8F842,
    0x1DF8F842,
    0x00262628,
    0x1DF8F823,
    0x1DF8F823,
    0x1DF8F823,
    0x1DF8F823,
    0x00252528,
    0x1DF8F823,
    0x1DF8F823,
    0x1DF8F823,
    0x1DF8F823,
    0x00262628,
    0x1DF8F823,
    0x1DF8F823,
    0x1DF8F823,
    0x1DF8F823,
    0x00242427,
    0x1DF8F821,
    0x1DF8F821,
    0x1DF8F821,
    0x1DF8F821,
    0x00232326,
    0x1DF8F821,
    0x1DF8F820,
    0x1DF8F820,
    0x1DF8F820,
    0x00262628,
    0x1DF8F823,
    0x1DF8F823,
    0x1DF8F823,
    0x1DF8F823,
    0x00242427,
    0x1DF8F821,
    0x1DF8F821,
    0x1DF8F821,
    0x1DF8F821,
    0x001F1F21,
    0x1DF8F81D,
    0x1DF8F81D,
    0x1DF8F81D,
    0x1DF8F81D,
    0x00262643,
    0x1DF8F822,
    0x1DF8F821,
    0x1DF8F821,
    0x1DF8F821,
    0x0018182B,
    0x1DF8F816,
    0x1DBDF815,
    0x1DF8F815,
    0x1DF8F815,
    0x0018182A,
    0x1195F836,
    0x1195F815,
    0x1195F815,
    0x1196F815,
    0x0028282C,
    0x1DF8F824,
    0x1DF8F824,
    0x1DF8F824,
    0x1DF8F824,
    0x0027272C,
    0x1DF8F824,
    0x1DF8F823,
    0x1DF8F823,
    0x1DF8F823,
    0x0082824A,
    0x1ADFF841,
    0x1ADDF822,
    0x1ADEF822,
    0x1ADFF822,
    0x003E3E40,
    0x09D1F81D,
    0x095BF81D,
    0x095BF81D,
    0x095BF81D,
    0x0029292D,
    0x1DF8F825,
    0x1DF8F825,
    0x1DF8F825,
    0x1DF8F825,
    0x0028282C,
    0x1DF8F824,
    0x1DF8F824,
    0x1DF8F824,
    0x1DF8F824,
    0x0029292D,
    0x1DF8F825,
    0x1DF8F825,
    0x1DF8F825,
    0x1DF8F825,
    0x0028282E,
    0x1DF8F825,
    0x1DF8F824,
    0x1DF8F824,
    0x1DF8F824,
    0x0026262C,
    0x1DF8F823,
    0x1DF8F822,
    0x1DF8F822,
    0x1DF8F822,
    0x0028282D,
    0x1DF8F825,
    0x1DF8F824,
    0x1DF8F824,
    0x1DF8F824,
    0x00282852,
    0x1DF8F827,
    0x1DF8F824,
    0x1DF8F824,
    0x1DF8F824,
    0x0029294E,
    0x1DF8F823,
    0x1DF8F822,
    0x1DF8F822,
    0x1DF8F822,
    0x00212143,
    0x1DF8F821,
    0x1DECF81D,
    0x1DF4F81D,
    0x1DF8F81D,
    0x0086864D,
    0x1CF0F844,
    0x1CEDF823,
    0x1CEFF822,
    0x1CF0F822,
    0x0047474D,
    0x1BE8F823,
    0x1BE8F823,
    0x1BE9F822,
    0x1BEAF822,
    0x0018182F,
    0x14B0F83C,
    0x14B0F814,
    0x14B0F814,
    0x14B0F814,
    0x00404040,
    0x0AE1F81E,
    0x0A61F81D,
    0x0A61F81D,
    0x0A61F81D,
    0x002C2C40,
    0x09555526,
    0x09555512,
    0x09555513,
    0x09555512,
    0x00181840,
    0x06333329,
    0x06333314,
    0x06333314,
    0x06333314,
    0x002B2B2F,
    0x1DF8F828,
    0x1DF8F828,
    0x1DF8F828,
    0x1DF8F828,
    0x002B2B32,
    0x1DF8F829,
    0x1DF8F828,
    0x1DF8F828,
    0x1DF8F828,
    0x002A2A2F,
    0x1DF8F827,
    0x1DF8F827,
    0x1DF8F827,
    0x1DF8F827,
    0x002A2A57,
    0x1DF8F82B,
    0x1DF8F827,
    0x1DF8F827,
    0x1DF8F827,
    0x00919152,
    0x1DF8F84B,
    0x1DF8F825,
    0x1DF8F825,
    0x1DF8F825,
    0x004C4C51,
    0x1DF8F826,
    0x1DF8F825,
    0x1DF8F825,
    0x1DF8F825,
    0x00444440,
    0x0CF8F820,
    0x0C6EF81F,
    0x0C6EF81F,
    0x0C6EF81F,
    0x00424240,
    0x0D75753E,
    0x0D75751E,
    0x0D75751E,
    0x0D75751E,
    0x00191940,
    0x0539392E,
    0x05393914,
    0x05393914,
    0x05393914,
    0x002F2F32,
    0x1AA5F82C,
    0x1AA5F82C,
    0x1ADEF82C,
    0x1ADEF82C,
    0x002F2F40,
    0x0C6EDE2C,
    0x0C6EDE2C,
    0x0C6EDE2C,
    0x0C6EDE2C,
    0x00323240,
    0x053BB62E,
    0x053BB62E,
    0x053BB62E,
    0x053BB62E,
    0x00333339,
    0x1DC4F82F,
    0x1DC4F82F,
    0x1DF8F82F,
    0x1DF8F82F,
    0x00333340,
    0x0E81F82F,
    0x0E81F82F,
    0x0E81F82F,
    0x0E82F82F,
    0x00333340,
    0x063FC42F,
    0x063FC42F,
    0x063FC42F,
    0x063FC42F,
    0x00404040,
    0x063FC42F,
    0x063FC42F,
    0x063FC42F,
    0x063FC42F,
    0x00363640,
    0x0747DD33,
    0x0747DD33,
    0x0747DD33,
    0x0747DD33,
    0x00404040,
    0x0747DD33,
    0x0747DD33,
    0x0747DD33,
    0x0747DD33,
    0x00292940,
    0x07484825,
    0x07484812,
    0x07484812,
    0x07484812,
    0x00404040,
    0x07343428,
    0x07343414,
    0x07343414,
    0x07343414,
    0x00404040,
    0x0538382A,
    0x05383814,
    0x05383814,
    0x05383814,
    0x00404040,
    0x05292914,
    0x05292909,
    0x05292909,
    0x05292909,
    0x000B0B40,
    0x02111108,
    0x0211110E,
    0x02111108,
    0x02111108,
    0x00404040,
    0x063E3E2E,
    0x063E3E15,
    0x063E3E14,
    0x063E3E14,
    0x00404040,
    0x062E2E14,
    0x062E2E09,
    0x062E2E09,
    0x062E2E09,
    0x000B0B40,
    0x02131308,
    0x0213130F,
    0x02131308,
    0x02131308
#else
    0x00767679,
    0x1DF8F870,
    0x1DF8F870,
    0x1DF8F870,
    0x1DF8F870,
    0x006E6E72,
    0x1DF8F869,
    0x1DF8F869,
    0x1DF8F869,
    0x1DF8F869,
    0x0076767B,
    0x1DF8F870,
    0x1DF8F870,
    0x1DF8F870,
    0x1DF8F870,
    0x007E7E85,
    0x1DF4F876,
    0x1DF4F876,
    0x1DF4F876,
    0x1DF8F876,
    0x0081818A,
    0x1DF8F87B,
    0x1DF8F87B,
    0x1DF8F87B,
    0x1DF8F87B,
    0x0081818D,
    0x1DF8F87B,
    0x1DF8F87B,
    0x1DF8F87B,
    0x1DF8F87B,
    0x0081818A,
    0x1DF8F87B,
    0x1DF8F87C,
    0x1DF8F87B,
    0x1DF8F87B,
    0x007E7E40,
    0x1DF8F87B,
    0x1DF8F87B,
    0x1DF8F87B,
    0x1DF8F87B,
    0x008B8B92,
    0x1DF8F887,
    0x1DF8F889,
    0x1DF8F887,
    0x1DF8F887,
    0x00515155,
    0x1DF8F84C,
    0x1DF8F84C,
    0x1DF8F889,
    0x1DF8F889,
    0x00515154,
    0x1DF8F84C,
    0x1DF8F84C,
    0x1DF8F888,
    0x1DF8F888,
    0x004F4F53,
    0x1DF8F84A,
    0x1DF8F84A,
    0x1DF8F84A,
    0x1DF8F84A,
    0x004F4F53,
    0x1DF8F84A,
    0x1DF8F84A,
    0x1DF8F84A,
    0x1DF8F84A,
    0x004F4F53,
    0x1DF8F84A,
    0x1DF8F84A,
    0x1DF8F84A,
    0x1DF8F84A,
    0x004E4E53,
    0x1DF8F849,
    0x1DF8F848,
    0x1DF8F848,
    0x1DF8F848,
    0x004D4D52,
    0x1DF8F847,
    0x1DF8F847,
    0x1DF8F847,
    0x1DF8F847,
    0x004F4F55,
    0x1DF8F84B,
    0x1DF8F84A,
    0x1DF8F84A,
    0x1DF8F84A,
    0x004E4E53,
    0x1DF8F849,
    0x1DF8F848,
    0x1DF8F848,
    0x1DF8F848,
    0x0049494D,
    0x1DF8F844,
    0x1DF8F844,
    0x1DF8F844,
    0x1DF8F844,
    0x0051518F,
    0x1DF8F849,
    0x1DF8F848,
    0x1DF8F848,
    0x1DF8F848,
    0x00424277,
    0x1DF8F83F,
    0x1DF8F83C,
    0x1DF8F83C,
    0x1DF8F83C,
    0x00424275,
    0x1DF8F89E,
    0x1DF8F83C,
    0x1DF8F83C,
    0x1DF8F83C,
    0x0055555C,
    0x1DF8F84C,
    0x1DF8F84C,
    0x1DF8F84C,
    0x1DF8F84C,
    0x0053535C,
    0x1DF8F84C,
    0x1DF8F84B,
    0x1DF8F84B,
    0x1DF8F84B,
    0x00F8F89E,
    0x1DF8F88C,
    0x1DF8F84A,
    0x1DF8F84A,
    0x1DF8F84A,
    0x00898940,
    0x18F8F846,
    0x18CFF845,
    0x18CFF844,
    0x18CFF844,
    0x0056565F,
    0x1DF8F84F,
    0x1DF8F84F,
    0x1DF8F84F,
    0x1DF8F84F,
    0x0055555E,
    0x1DF8F84E,
    0x1DF8F84E,
    0x1DF8F84E,
    0x1DF8F84E,
    0x0056565F,
    0x1DF8F84F,
    0x1DF8F84F,
    0x1DF8F84F,
    0x1DF8F84F,
    0x00555561,
    0x1DF8F850,
    0x1DF8F84E,
    0x1DF8F84E,
    0x1DF8F84E,
    0x0053535F,
    0x1DF8F84D,
    0x1DF8F84C,
    0x1DF8F84C,
    0x1DF8F84C,
    0x0055555F,
    0x1DF8F84F,
    0x1DF8F84E,
    0x1DF8F84E,
    0x1DF8F84E,
    0x005555AA,
    0x1DF8F854,
    0x1DF8F84E,
    0x1DF8F84E,
    0x1DF8F84E,
    0x005959A6,
    0x1DF8F84D,
    0x1DF8F84C,
    0x1DF8F84C,
    0x1DF8F84C,
    0x004F4F9B,
    0x1DF8F84E,
    0x1DF8F846,
    0x1DF8F846,
    0x1DF8F846,
    0x00F8F8A5,
    0x1DF8F894,
    0x1DF8F84C,
    0x1DF8F84C,
    0x1DF8F84C,
    0x009898A4,
    0x1DF8F84D,
    0x1DF8F84C,
    0x1DF8F84C,
    0x1DF8F84C,
    0x00464686,
    0x1DF8F8B3,
    0x1DF8F83D,
    0x1DF8F83D,
    0x1DF8F83D,
    0x008E8E40,
    0x1AF8F848,
    0x1ADFF848,
    0x1ADFF846,
    0x1ADFF846,
    0x007F7F40,
    0x18D2D275,
    0x18D2D23A,
    0x18D2D23A,
    0x18D2D239,
    0x00454540,
    0x0F868664,
    0x0F86863E,
    0x0F86863D,
    0x0F86863D,
    0x005C5C64,
    0x1DF8F856,
    0x1DF8F855,
    0x1DF8F855,
    0x1DF8F855,
    0x005B5B68,
    0x1DF8F858,
    0x1DF8F855,
    0x1DF8F855,
    0x1DF8F855,
    0x005A5A64,
    0x1DF8F855,
    0x1DF8F854,
    0x1DF8F854,
    0x1DF8F854,
    0x005A5AB5,
    0x1DF8F85B,
    0x1DF8F855,
    0x1DF8F854,
    0x1DF8F854,
    0x00F8F8B0,
    0x1DF8F8A3,
    0x1DF8F852,
    0x1DF8F852,
    0x1DF8F852,
    0x00A4A4AE,
    0x1DF8F854,
    0x1DF8F852,
    0x1DF8F852,
    0x1DF8F852,
    0x009A9A40,
    0x1DF8F84E,
    0x1DF8F84D,
    0x1DF8F84C,
    0x1DF8F84C,
    0x009C9C40,
    0x1DF8F895,
    0x1DF8F849,
    0x1DF8F84A,
    0x1DF8F84A,
    0x00494940,
    0x1197976F,
    0x11979742,
    0x11979741,
    0x11979741,
    0x006E6E74,
    0x1DF8F869,
    0x1DF8F869,
    0x1DF8F869,
    0x1DF8F869,
    0x006E6E40,
    0x1ADEF869,
    0x1ADEF869,
    0x1ADEF869,
    0x1ADEF869,
    0x00757540,
    0x0D78F86E,
    0x0D78F86E,
    0x0D78F86E,
    0x0D79F86E,
    0x00787885,
    0x1DF8F873,
    0x1DF8F873,
    0x1DF8F873,
    0x1DF8F873,
    0x00787840,
    0x1DF8F873,
    0x1DF8F873,
    0x1DF8F873,
    0x1DF8F873,
    0x00787840,
    0x0E81F873,
    0x0E81F873,
    0x0E81F873,
    0x0E82F873,
    0x00404040,
    0x0E82F873,
    0x0E82F873,
    0x0E82F873,
    0x0E82F873,
    0x00818140,
    0x1092F87E,
    0x1092F87E,
    0x1092F87E,
    0x1092F87E,
    0x00404040,
    0x1092F87E,
    0x1092F87E,
    0x1092F87E,
    0x1092F87E,
    0x00737340,
    0x14B2B26B,
    0x14B2B235,
    0x14B2B235,
    0x14B2B235,
    0x00404040,
    0x0E828260,
    0x0E82823D,
    0x0E82823C,
    0x0E82823C,
    0x00404040,
    0x0F8B8B66,
    0x0F8B8B3F,
    0x0F8B8B3D,
    0x0F8B8B3D,
    0x00404040,
    0x0B68683D,
    0x0B68681E,
    0x0B68681E,
    0x0B68681E,
    0x00222240,
    0x06434318,
    0x06434329,
    0x06434318,
    0x06434318,
    0x00404040,
    0x129D9D72,
    0x129D9D43,
    0x129D9D41,
    0x129D9D41,
    0x00404040,
    0x0D757542,
    0x0D757520,
    0x0D757520,
    0x0D757520,
    0x00232340,
    0x084C4C19,
    0x084C4C2C,
    0x084C4C19,
    0x084C4C19
#endif
};


uint32_t agc_cfg_ram[] = {
    0x20000000,
    0x0400000E,
    0x3000200E,
    0x5B000000,
    0x0400004B,
    0x3000008E,
    0x32000000,
    0x0400007B,
    0x40000000,
    0xF8000026,
    0x04000011,
    0x4819008E,
    0x9C000020,
    0x08000191,
    0x38008000,
    0x0A000000,
    0x08104411,
    0x38018000,
    0x0C004641,
    0x08D00014,
    0x30000000,
    0x01000000,
    0x04000017,
    0x30000000,
    0x3C000000,
    0x0400001A,
    0x38020000,
    0x40000001,
    0x0800001D,
    0x3808008E,
    0x14000050,
    0x08000020,
    0x4000008E,
    0xA400007B,
    0x00000101,
    0x3000339F,
    0x41000700,
    0x04104420,
    0x90000000,
    0x49000000,
    0xF00E842F,
    0xEC0E842C,
    0xEC0E842C,
    0x04000032,
    0x30000000,
    0x48000101,
    0x04000032,
    0x30000000,
    0x48000202,
    0x04000032,
    0x30000000,
    0x46000000,
    0x04000011,
    0x58010006,
    0x3D040472,
    0xDC204439,
    0x081DD4D2,
    0x480A0006,
    0xDC2044DC,
    0x081DD43C,
    0x38050004,
    0x0EF1F1C3,
    0x342044DC,
    0x30000000,
    0x01000000,
    0x04000042,
    0x30000000,
    0x33000000,
    0x04104445,
    0x38008000,
    0x2200109C,
    0x08104448,
    0x38008000,
    0x23D4509C,
    0x08104417,
    0x9000A000,
    0x32000000,
    0x18000063,
    0x14000060,
    0x1C000051,
    0x10000057,
    0x38028000,
    0x0C000001,
    0x08D04466,
    0x3000200F,
    0x00000000,
    0x00000000,
    0x38030000,
    0x0C002601,
    0x08D0445A,
    0x30000000,
    0x3D020230,
    0x0400005D,
    0x30000000,
    0x3E000100,
    0x04000066,
    0x38028000,
    0x0C001601,
    0x34204466,
    0x38028000,
    0x0C000A01,
    0x34204466,
    0x38008004,
    0xFF000000,
    0x0800007B,
    0x3800802F,
    0x26000000,
    0x0800006C,
    0x380404AF,
    0x1F191010,
    0x0800006F,
    0x20000CAF,
    0x04000071,
    0x60000CAF,
    0x18700079,
    0x14000077,
    0x10000075,
    0x28140CAF,
    0x09B00084,
    0x280A0CAF,
    0x09B00084,
    0x28060CAF,
    0x09B00084,
    0x28048086,
    0x0800007D,
    0x38000086,
    0x22800000,
    0x04000080,
    0x30000000,
    0x0EF1F101,
    0x36004883,
    0x28020000,
    0x08000085,
    0x3802008E,
    0x3D040431,
    0x08000088,
    0x3805008E,
    0x1F241821,
    0x0800008B,
    0x3000008E,
    0xA0163021,
    0x0400008E,
    0x3000008E,
    0x0EF10012,
    0x34000091,
    0x300000CC,
    0x50000000,
    0x04000094,
    0x380095FE,
    0x32010000,
    0x04000097,
    0x50001FFE,
    0x5A010000,
    0x6DC9989B,
    0xFC19D4B9,
    0x30000186,
    0x3D840373,
    0x0400009E,
    0x3000008E,
    0x0A000000,
    0x040000A1,
    0x3000008E,
    0x22C00000,
    0x040000A4,
    0x9000028E,
    0x32010001,
    0x8E4000AA,
    0xC80000B0,
    0x00000000,
    0x00000000,
    0x3000008E,
    0x32010001,
    0x040000CB,
    0x3000008E,
    0x29000000,
    0x94045011,
    0x300019B6,
    0x32010000,
    0x040000B3,
    0x300019B6,
    0x3D040431,
    0x040000B6,
    0x300019B6,
    0x22800000,
    0x04000097,
    0x30000186,
    0x3D840473,
    0x040000BC,
    0x3000008E,
    0x29030000,
    0x040000BF,
    0x9AEE028E,
    0x32010100,
    0x7C0000C5,
    0xCC0000B0,
    0x080000B0,
    0x00000000,
    0x3000008E,
    0x32010100,
    0x040000C8,
    0x3000028E,
    0x29000000,
    0x94045011,
    0x5000038E,
    0x29000000,
    0x94045011,
    0xC0000035,
    0x38010006,
    0x3D040472,
    0x080000D2,
    0x30000004,
    0x0EF1F141,
    0x340000D5,
    0x28040004,
    0x080000D7,
    0x2808000E,
    0x080000D9,
    0x3000018E,
    0x0EF10052,
    0x340000DC,
    0x3000038E,
    0x29000000,
    0x94045011,
    0x38020000,
    0x32000000,
    0x080000E2,
    0x60000000,
    0xD80000E6,
    0xD40000E9,
    0x040000EC,
    0x30000000,
    0x0EF1F121,
    0x360048EF,
    0x30000000,
    0x0C002421,
    0x360048EF,
    0x30000000,
    0x0C000021,
    0x360048EF,
    0x28020000,
    0x0800007B,
    0x50001EFE,
    0x5A010000,
    0x6DC998F5,
    0xFC19D4F8,
    0x3000028E,
    0x32000040,
    0x040000FB,
    0x3AEE028E,
    0x32000080,
    0x040000FB,
    0x30000000,
    0x0EF1F101,
    0x360048FE,
    0x28020000,
    0x08000100,
    0x3802008E,
    0x3D040431,
    0x08000103,
    0x3805008E,
    0x1F241821,
    0x08000106,
    0x3000008E,
    0xA0163021,
    0x04000109,
    0x3000008E,
    0x0EF10012,
    0x3400010C,
    0x300014F6,
    0x32010000,
    0x04000114,
    0x20000000,
    0x04000111,
    0x300000EC,
    0x50000000,
    0x040000F1,
    0x300014F6,
    0x32030000,
    0x04000117,
    0x30001086,
    0x3D840473,
    0x0400011A,
    0x5000108E,
    0x22C00000,
    0x8E47C0CB,
    0xCB30011E,
    0x300019B6,
    0x32040000,
    0x04000121,
    0x300019B6,
    0x3D040431,
    0x04000124,
    0x300019B6,
    0x22800000,
    0x04000111,
    0x00000000,
    0x00000000,
    0x00000000,
    0x30000186,
    0x3D840473,
    0x0400012D,
    0x5000038E,
    0x29000000,
    0x94045011,
    0xC0000131,
    0x380C800E,
    0xFF000000,
    0x08000134,
    0x30000004,
    0x0FF1F103,
    0x34000137,
    0x28020000,
    0x08000139,
    0x3000038E,
    0x29000000,
    0x94045011,
    0x00000000,
    0x00000000,
    0x00000000,
    0x58010006,
    0x3D040472,
    0xDC204543,
    0x081DD4D2,
    0x480A0006,
    0xDC2044DC,
    0x081DD546,
    0x38050004,
    0x0EF1F141,
    0x342044DC,
    0x2802800E,
    0x080000DC,
    0x48000035,
    0x0400014A,
    0x7896638F,
    0x4100000F,
    0x8C00014F,
    0x080450C4,
    0x90104574,
    0x88C8620F,
    0xC000015A,
    0x90104574,
    0x08104554,
    0x94104557,
    0x3000628F,
    0x29000000,
    0x9404517A,
    0x3000638F,
    0x29000000,
    0x0410457A,
    0x3800E005,
    0x3D010131,
    0x0810455D,
    0xA832600F,
    0x90104574,
    0x08000154,
    0x94104557,
    0xC6104567,
    0xC4185563,
    0x5802E00F,
    0x0FEEEA07,
    0x80000174,
    0x3420456B,
    0x5802E00F,
    0x0EEEEA07,
    0x80000174,
    0x3420456B,
    0x30004000,
    0x33000001,
    0x0400016E,
    0x38034005,
    0x3D030373,
    0x08000171,
    0x30006007,
    0x33000000,
    0x04000174,
    0x3000608F,
    0x29000000,
    0x94045177,
    0x4000608F,
    0xA010457D,
    0x0410457A,
    0x3000608F,
    0x64000101,
    0x04104411,
    0x3000608F,
    0x64000101,
    0x04104580,
    0x3000618F,
    0x42000001,
    0x04000183,
    0x38028000,
    0x32000000,
    0x08104586,
    0x280A618F,
    0x08000188,
    0x480A618F,
    0xBC00018B,
    0x0800018E,
    0x3000618F,
    0x34000001,
    0x04000005,
    0x3000618F,
    0x34000000,
    0x04000008,
    0x3000008F,
    0x0EEAED0F,
    0x36000194,
    0x38038000,
    0x34000000,
    0x08000197,
    0x38028005,
    0x29010002,
    0x0800019A,
    0x3000028F,
    0x2200209C,
    0x0400019D,
    0x3000028F,
    0x23D4509C,
    0x040001A0,
    0x2814028F,
    0x080001A2,
    0x3000028F,
    0x43010201,
    0x040001A5,
    0x3000128F,
    0x32000100,
    0x040001A8,
    0x5AEE138F,
    0x4100000F,
    0x7C0001AC,
    0x080000F9,
    0x592C138F,
    0x29000000,
    0x8C0001B0,
    0x080000F9,
    0x2000138F,
    0x94045011,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000
};


uint32_t txgain_map[96] =  {
#ifdef CONFIG_FPGA_VERIFICATION
    0x20c0c971,
    0x20c0c980,
    0x20c0c992,
    0x20c0c9a6,
    0x20c0c9bf,
    0x20c0caa5,
    0x20c0cabd,
    0x20c0cba0,
    0x20c0cbb6,
    0x20c0cbea,
    0x20c0ccc5,
    0x20c0cdac,
    0x20c0cdd0,
    0x20c0ceb2,
    0x20c0ceff,
    0x20c0cfff,
    0x20c0c922,
    0x20c0c922,
    0x20c0c922,
    0x20c0c922,
    0x20c0c922,
    0x20c0c922,
    0x20c0c922,
    0x20c0c927,
    0x20c0c92c,
    0x20c0c931,
    0x20c0c937,
    0x20c0c93f,
    0x20c0c946,
    0x20c0c94f,
    0x20c0c959,
    0x20c0c964,
    0x20c0cbee,
    0x20c0cce0,
    0x20c0ccff,
    0x20c0cde2,
    0x20c0cdfe,
    0x20c0cede,
    0x20c0cefc,
    0x20c0cfd9,
    0x20c0cff8,
    0x20c0cfff,
    0x20c0cfff,
    0x20c0cfff,
    0x20c0cfff,
    0x20c0cfff,
    0x20c0cfff,
    0x20c0cfff,
    0x20c0c97c,
    0x20c0c97c,
    0x20c0c97c,
    0x20c0c97c,
    0x20c0c97c,
    0x20c0c97c,
    0x20c0c97c,
    0x20c0c98c,
    0x20c0ca79,
    0x20c0ca89,
    0x20c0cb74,
    0x20c0cb84,
    0x20c0cb94,
    0x20c0cba8,
    0x20c0cbbb,
    0x20c0cbd2,
    0x20c0cbee,
    0x20c0cce0,
    0x20c0ccff,
    0x20c0cde2,
    0x20c0cdfe,
    0x20c0cede,
    0x20c0cefc,
    0x20c0cfd9,
    0x20c0cff8,
    0x20c0cfff,
    0x20c0cfff,
    0x20c0cfff,
    0x20c0cfff,
    0x20c0cfff,
    0x20c0cfff,
    0x20c0cfff,
    0x20c0c97c,
    0x20c0c97c,
    0x20c0c97c,
    0x20c0c97c,
    0x20c0c97c,
    0x20c0c97c,
    0x20c0c97c,
    0x20c0c98c,
    0x20c0ca79,
    0x20c0ca89,
    0x20c0cb74,
    0x20c0cb84,
    0x20c0cb94,
    0x20c0cba8,
    0x20c0cbbb,
    0x20c0cbd2,
#else
    //11b
    0x00ffd780,
    0x00ffd872,
    0x00ffd880,
    0x00ffd972,
    0x00ffd980,
    0x00ffda75,
    0x00ffda86,
    0x00ffdb77,
    0x00ffdb86,
    0x00ffdc78,
    0x00ffdc89,
    0x00ffdd79,
    0x00ffdd89,
    0x00ffde83,
    0x00ffdf79,
    0x00ffdf8b,
    0x00ffd072,
    0x00ffd072,
    0x00ffd080,
    0x00ffd172,
    0x00ffd180,
    0x00ffd272,
    0x00ffd280,
    0x00ffd36d,
    0x00ffd379,
    0x00ffd46d,
    0x00ffd479,
    0x00ffd572,
    0x00ffd580,
    0x00ffd672,
    0x00ffd680,
    0x00ffd772,
    //high
    0x00ffc87d,
    0x00ffc88b,
    0x00ffc979,
    0x00ffc989,
    0x00ffca7d,
    0x00ffca88,
    0x00ffcc5e,
    0x00ffcc69,
    0x00ffcc78,
    0x00ffcc85,
    0x00ffcd70,
    0x00ffcd80,
    0x00ffce70,
    0x00ffce80,
    0x00ffcf7d,
    0x00ffcf90,
    0x00ffc080,
    0x00ffc090,
    0x00ffc180,
    0x00ffc190,
    0x00ffc27b,
    0x00ffc28b,
    0x00ffc37b,
    0x00ffc390,
    0x00ffc485,
    0x00ffc495,
    0x00ffc579,
    0x00ffc589,
    0x00ffc679,
    0x00ffc689,
    0x00ffc780,
    0x00ffc790,
    //low
    0x00ffc87d,
    0x00ffc88b,
    0x00ffc979,
    0x00ffc989,
    0x00ffca7d,
    0x00ffca88,
    0x00ffcc5e,
    0x00ffcc69,
    0x00ffcc78,
    0x00ffcc85,
    0x00ffcd70,
    0x00ffcd80,
    0x00ffce70,
    0x00ffce80,
    0x00ffce93,
    0x00ffcf90,
    0x00ffc080,
    0x00ffc090,
    0x00ffc180,
    0x00ffc190,
    0x00ffc27b,
    0x00ffc28b,
    0x00ffc37b,
    0x00ffc390,
    0x00ffc485,
    0x00ffc495,
    0x00ffc579,
    0x00ffc589,
    0x00ffc679,
    0x00ffc689,
    0x00ffc780,
    0x00ffc790,
#endif
};

const uint32_t txgain_map_h[96] =
{
    //11b
    0xffd888, //11
    0xffd979, //12
    0xffd988, //13
    0xffda79, //14
    0xffda88, //15
    0xffdb79, //16
    0xffdb88, //17
    0xffdc72, //18
    0xffdc80, //19
    0xffdd80, //20
    0xffde66, //21
    0xffde72, //22
    0xffde80, //23
    0xffdf79, //24
    0xffdf88, //25
    0xffdf98, //26
    0xffd079, //-5
    0xffd088, //-4
    0xffd179, //-3
    0xffd188, //-2
    0xffd288, //-1
    0xffd36c, //0
    0xffd379, //1
    0xffd388, //2
    0xffd479, //3
    0xffd488, //4
    0xffd579, //5
    0xffd588, //6
    0xffd679, //7
    0xffd688, //8
    0xffd779, //9
    0xffd879, //10
    //high
    0xffc879, //8
    0xffc96b, //9
    0xffc979, //10
    0xffca6b, //11
    0xffca79, //12
    0xffcc56, //13
    0xffcc60, //14
    0xffcc6b, //15
    0xffcc79, //16
    0xffcd72, //17
    0xffce60, //18
    0xffce72, //19
    0xffcf72, //20
    0xffcf80, //21
    0xffcf90, //22
    0xffcf90, //23
    0xffc079, //-8
    0xffc16b, //-7
    0xffc179, //-6
    0xffc26b, //-5
    0xffc279, //-4
    0xffc36b, //-3
    0xffc379, //-2
    0xffc46b, //-1
    0xffc479, //0
    0xffc56b, //1
    0xffc579, //2
    0xffc66b, //3
    0xffc679, //4
    0xffc76b, //5
    0xffc779, //6
    0xffc86b, //7
    //low
    0xffc879, //8
    0xffc96b, //9
    0xffc979, //10
    0xffca6b, //11
    0xffca79, //12
    0xffcc56, //13
    0xffcc60, //14
    0xffcc6b, //15
    0xffcc79, //16
    0xffcd72, //17
    0xffce60, //18
    0xffce72, //19
    0xffcf72, //20
    0xffcf80, //21
    0xffcf90, //22
    0xffcf90, //23
    0xffc079, //-8
    0xffc16b, //-7
    0xffc179, //-6
    0xffc26b, //-5
    0xffc279, //-4
    0xffc36b, //-3
    0xffc379, //-2
    0xffc46b, //-1
    0xffc479, //0
    0xffc56b, //1
    0xffc579, //2
    0xffc66b, //3
    0xffc679, //4
    0xffc76b, //5
    0xffc779, //6
    0xffc86b, //7
};


u32 patch_tbl_wifisetting_8800dc_u01[][2] =
{
	{0x010c,0x01001E01}
};

u32 patch_tbl_wifisetting_8800dc_u02[][2] =
{
#if defined(CONFIG_SDIO_BUS_PWRCTRL)
    {0x0124,0x01011E01}
#else
	{0x0124,0x01001E01}
#endif
};

u32 adaptivity_patch_tbl_8800dc[][2] = {
    {0x000C, 0x0000320A}, //linkloss_thd
    {0x009C, 0x00000000}, //ac_param_conf
    {0x0128, 0xF6140001}, //tx_adaptivity_en
};

u32 jump_tbl[][2] =
{
#ifndef CONFIG_FOR_IPCOM
    {296, 0x180001},
    {137, 0x180011},
    {303, 0x1810f9},
    {168, 0x18186d},
    {308, 0x181bbd},
    {288, 0x1820c1},
#else
    {308, 0x181001},
    {288, 0x181031},
    {296, 0x18120d},
    {137, 0x18121d},
    {303, 0x182305},
    {168, 0x182a79},
    {258, 0x182ae1},
#endif
};

u32 jump_tbl_u02[][2] =
{
    {303, 0x00180d25},
    {168, 0x001814a5},
    {265, 0x001816b1},
    {266, 0x00181849},
    {256, 0x001818ad},
    {288, 0x00181bf9},
    {333, 0x00182d0d},
    { 26, 0x00182d45}
};

u32 patch_tbl_func[][2] =
{
#ifndef CONFIG_FOR_IPCOM
    {0x00110054, 0x0018186D}, // same as jump_tbl idx 168
    {0x0011005C, 0x0018186D}, // same as jump_tbl idx 168
#else
    {0x00110054, 0x00182A79}, // same as jump_tbl idx 168
    {0x0011005C, 0x00182A79}, // same as jump_tbl idx 168
    {0x001118D4, 0x00000011},
#endif
};

u32 patch_tbl_func_u02[][2] =
{
    {0x00110054, 0x001814a5}, // same as jump_tbl idx 168
    {0x0011005C, 0x001814a5}, // same as jump_tbl idx 168
    {0x001109c0, 0x00181e3d},
    {0x00110bb4, 0x001824e1},
    {0x00110f08, 0x00182d25},
};

u32 patch_tbl_rf_func[][2] =
{
    {0x00110bf0, 0x00180001},
};

typedef u32 (*array2_tbl_t)[2];
typedef u32 (*array3_tbl_t)[3];
static void aicwf_patch_config_8800dc(struct rwnx_hw *rwnx_hw)
{
    int ret = 0;
    int cnt = 0;
    if (rwnx_hw->mode != WIFI_MODE_RFTEST) {
        const u32 cfg_base = 0x10164;
        struct dbg_mem_read_cfm cfm;
        int i = 0;
        u32 wifisetting_cfg_addr;
        u32 ldpc_cfg_addr;
        u32 agc_cfg_addr;
        u32 txgain_cfg_addr;
        u32 jump_tbl_addr = 0;

        u32 patch_tbl_wifisetting_num;
        u32 ldpc_cfg_size = sizeof(ldpc_cfg_ram);
        u32 agc_cfg_size = sizeof(agc_cfg_ram);
        u32 txgain_cfg_size, *txgain_cfg_array;
        u32 jump_tbl_size = 0;
        u32 patch_tbl_func_num= 0 ;

        array2_tbl_t jump_tbl_base = NULL;
        array2_tbl_t patch_tbl_func_base = NULL;
		array2_tbl_t patch_tbl_wifisetting_8800dc_base = NULL;

        if (chip_sub_id == 0) {
            jump_tbl_base = jump_tbl;
            jump_tbl_size = sizeof(jump_tbl)/2;
            patch_tbl_func_base = patch_tbl_func;
            patch_tbl_func_num = sizeof(patch_tbl_func)/sizeof(u32)/2;
			patch_tbl_wifisetting_num = sizeof(patch_tbl_wifisetting_8800dc_u01)/sizeof(u32)/2;
			patch_tbl_wifisetting_8800dc_base = patch_tbl_wifisetting_8800dc_u01;
        } else if (chip_sub_id == 1 || chip_sub_id == 2) {
			patch_tbl_wifisetting_num = sizeof(patch_tbl_wifisetting_8800dc_u02)/sizeof(u32)/2;
			patch_tbl_wifisetting_8800dc_base = patch_tbl_wifisetting_8800dc_u02;
        } else {
            printk("unsupported id: %d", chip_sub_id);
            return;
        }

        if ((ret = rwnx_send_dbg_mem_read_req(rwnx_hw, cfg_base, &cfm))) {
            printk("setting base[0x%x] rd fail: %d\n", cfg_base, ret);
        }
        wifisetting_cfg_addr = cfm.memdata;

		if (chip_sub_id == 0) {
	        if ((ret = rwnx_send_dbg_mem_read_req(rwnx_hw, cfg_base + 4, &cfm))) {
	            printk("setting base[0x%x] rd fail: %d\n", cfg_base + 4, ret);
	        }
	        jump_tbl_addr = cfm.memdata;
		}

        if ((ret = rwnx_send_dbg_mem_read_req(rwnx_hw, cfg_base + 8, &cfm))) {
            printk("setting base[0x%x] rd fail: %d\n", cfg_base + 8, ret);
        }
        ldpc_cfg_addr = cfm.memdata;

        if ((ret = rwnx_send_dbg_mem_read_req(rwnx_hw, cfg_base + 0xc, &cfm))) {
            printk("setting base[0x%x] rd fail: %d\n", cfg_base + 0xc, ret);
        }
        agc_cfg_addr = cfm.memdata;

        if ((ret = rwnx_send_dbg_mem_read_req(rwnx_hw, cfg_base + 0x10, &cfm))) {
            printk("setting base[0x%x] rd fail: %d\n", cfg_base + 0x10, ret);
        }
        txgain_cfg_addr = cfm.memdata;

        printk("wifisetting_cfg_addr=%x, ldpc_cfg_addr=%x, agc_cfg_addr=%x, txgain_cfg_addr=%x,jump_tbl_addr=%x\n", wifisetting_cfg_addr, ldpc_cfg_addr, agc_cfg_addr, txgain_cfg_addr,jump_tbl_addr);

        for (cnt = 0; cnt < patch_tbl_wifisetting_num; cnt++) {
            if (chip_sub_id == 0) {
                if (patch_tbl_wifisetting_8800dc_base[cnt][0] == 0x010C)          //sdio_func_config, gpio_wakeup_en
                    func_flag_rx = false;
            } else {
                if (patch_tbl_wifisetting_8800dc_base[cnt][0] == 0x0124)          //sdio_func_config, gpio_wakeup_en
                func_flag_rx = false;
            }
            if ((ret = rwnx_send_dbg_mem_write_req(rwnx_hw, wifisetting_cfg_addr + patch_tbl_wifisetting_8800dc_base[cnt][0], patch_tbl_wifisetting_8800dc_base[cnt][1]))) {
                printk("wifisetting %x write fail\n", patch_tbl_wifisetting_8800dc_base[cnt][0]);
            }
        }
#if 1
        if (adap_test) {
            printk("%s for adaptivity test \r\n", __func__);
            int adap_patch_num = sizeof(adaptivity_patch_tbl_8800dc)/sizeof(u32)/2;
        	for(cnt = 0; cnt < adap_patch_num; cnt++)
        	{
        		if((ret = rwnx_send_dbg_mem_write_req(rwnx_hw, wifisetting_cfg_addr + adaptivity_patch_tbl_8800dc[cnt][0], adaptivity_patch_tbl_8800dc[cnt][1]))) {
        			printk("%x write fail\n", wifisetting_cfg_addr + adaptivity_patch_tbl_8800dc[cnt][0]);
        		}
        	}
        }
#endif
        if (ldpc_cfg_size > 512) {// > 0.5KB data
            for (i = 0; i < (ldpc_cfg_size - 512); i += 512) {//each time write 0.5KB
                ret = rwnx_send_dbg_mem_block_write_req(rwnx_hw, ldpc_cfg_addr + i, 512, ldpc_cfg_ram + i / 4);
                if (ret) {
                    printk("ldpc upload fail: %x, err:%d\r\n", ldpc_cfg_addr + i, ret);
                    break;
                }
            }
        }

        if (!ret && (i < ldpc_cfg_size)) {// < 0.5KB data
            ret = rwnx_send_dbg_mem_block_write_req(rwnx_hw, ldpc_cfg_addr + i, ldpc_cfg_size - i, ldpc_cfg_ram + i / 4);
            if (ret) {
                printk("ldpc upload fail: %x, err:%d\r\n", ldpc_cfg_addr + i, ret);
            }
        }
        if (agc_cfg_size > 512) {// > 0.5KB data
            for (i = 0; i < (agc_cfg_size - 512); i += 512) {//each time write 0.5KB
                ret = rwnx_send_dbg_mem_block_write_req(rwnx_hw, agc_cfg_addr + i, 512, agc_cfg_ram + i / 4);
                if (ret) {
                    printk("agc upload fail: %x, err:%d\r\n", agc_cfg_addr + i, ret);
                    break;
                }
            }
        }
        if (!ret && (i < agc_cfg_size)) {// < 0.5KB data
            ret = rwnx_send_dbg_mem_block_write_req(rwnx_hw, agc_cfg_addr + i, agc_cfg_size - i, agc_cfg_ram + i / 4);
            if (ret) {
                printk("agc upload fail: %x, err:%d\r\n", agc_cfg_addr + i, ret);
            }
        }

        #if !defined(CONFIG_FPGA_VERIFICATION)
		if ((IS_CHIP_ID_H())) {
            txgain_cfg_size = sizeof(txgain_map_h);
            txgain_cfg_array = (u32 *)txgain_map_h;
        } else {
            txgain_cfg_size = sizeof(txgain_map);
            txgain_cfg_array = (u32 *)txgain_map;
        }
        ret = rwnx_send_dbg_mem_block_write_req(rwnx_hw, txgain_cfg_addr, txgain_cfg_size, txgain_cfg_array);
        if (ret) {
            printk("txgain upload fail: %x, err:%d\r\n", txgain_cfg_addr, ret);
        }
        if (chip_sub_id == 0) {
            for (cnt = 0; cnt < jump_tbl_size/4; cnt+=1) {
                printk("%x = %x\n", jump_tbl_base[cnt][0]*4+jump_tbl_addr, jump_tbl_base[cnt][1]);
                if ((ret = rwnx_send_dbg_mem_write_req(rwnx_hw, jump_tbl_base[cnt][0]*4+jump_tbl_addr, jump_tbl_base[cnt][1]))) {
                    printk("%x write fail\n", jump_tbl_addr+8*cnt);
                }
            }

            for (cnt = 0; cnt < patch_tbl_func_num; cnt++) {
                printk("%x = %x\n", patch_tbl_func_base[cnt][0], patch_tbl_func_base[cnt][1]);
                if ((ret = rwnx_send_dbg_mem_write_req(rwnx_hw, patch_tbl_func_base[cnt][0], patch_tbl_func_base[cnt][1]))) {
                    printk("patch_tbl_func %x write fail\n", patch_tbl_func_base[cnt][0]);
                }
            }
        } else if (chip_sub_id == 1 || chip_sub_id == 2) {
            ret = aicwf_patch_table_load(rwnx_hw);
        } else {
			printk("unsupported id: %d\n", chip_sub_id);
        }
        #endif
    } else {
	    if (chip_sub_id == 0) {
            u32 patch_tbl_rf_func_num = sizeof(patch_tbl_rf_func)/sizeof(u32)/2;
            for (cnt = 0; cnt < patch_tbl_rf_func_num; cnt++) {
                if ((ret = rwnx_send_dbg_mem_write_req(rwnx_hw, patch_tbl_rf_func[cnt][0], patch_tbl_rf_func[cnt][1]))) {
                    printk("patch_tbl_rf_func %x write fail\n", patch_tbl_rf_func[cnt][0]);
                }
            }
        }
    }
}

#define AIC_PATCH_MAGIG_NUM     0x48435450 // "PTCH"
#define AIC_PATCH_MAGIG_NUM_2   0x50544348 // "HCTP"
#define AIC_PATCH_BLOCK_MAX     4

typedef struct {
    uint32_t magic_num;
    uint32_t pair_start;
    uint32_t magic_num_2;
    uint32_t pair_count;
    uint32_t block_dst[AIC_PATCH_BLOCK_MAX];
    uint32_t block_src[AIC_PATCH_BLOCK_MAX];
    uint32_t block_size[AIC_PATCH_BLOCK_MAX]; // word count
} aic_patch_t;

#define AIC_PATCH_OFST(mem) ((size_t) &((aic_patch_t *)0)->mem)
#define AIC_PATCH_ADDR(mem) ((u32)(aic_patch_str_base + AIC_PATCH_OFST(mem)))

u32 adaptivity_patch_tbl_8800d80[][2] = {
	{0x000C, 0x0000320A}, //linkloss_thd
	{0x009C, 0x00000000}, //ac_param_conf
	{0x0154, 0x00010000}, //tx_adaptivity_en
};

u32 patch_tbl_8800d80[][2] = {
	#ifdef USE_5G
	{0x00b4, 0xf3010001},
	{0x01a0, 0x00010001},
	#else
	{0x00b4, 0xf3010000},
	#endif
#ifdef CONFIG_RADAR_OR_IR_DETECT
    {0x019c, 0x00000B00}, //enable radar detect
#endif
};
#define NEW_PATCH_BUFFER_MAP    1

static int aicwifi_patch_config_8800d80(struct rwnx_hw *rwnx_hw)
{
	const u32 rd_patch_addr = RAM_FMAC_FW_ADDR + 0x0198;
	u32 aic_patch_addr;
	u32 config_base, aic_patch_str_base;
    #if (NEW_PATCH_BUFFER_MAP)
	u32 patch_buff_addr, patch_buff_base, rd_version_addr, rd_version_val;
	#endif

	uint32_t start_addr = 0x0016F800;
	u32 patch_addr = start_addr;
	u32 patch_cnt = sizeof(patch_tbl_8800d80)/sizeof(u32)/2;
	struct dbg_mem_read_cfm rd_patch_addr_cfm;
	int ret = 0;
	int cnt = 0;
	//adap test
	int adap_patch_cnt = 0;

	if (adap_test) {
        printk("%s for adaptivity test \r\n", __func__);
		adap_patch_cnt = sizeof(adaptivity_patch_tbl_8800d80)/sizeof(u32)/2;
	}

	aic_patch_addr = rd_patch_addr + 8;

	ret = rwnx_send_dbg_mem_read_req(rwnx_hw, rd_patch_addr, &rd_patch_addr_cfm);
	if (ret) {
		printk("patch rd fail\n");
		return ret;
	}

	config_base = rd_patch_addr_cfm.memdata;

	ret = rwnx_send_dbg_mem_read_req(rwnx_hw, aic_patch_addr, &rd_patch_addr_cfm);
	if (ret) {
		printk("patch str rd fail\n");
		return ret;
	}
	aic_patch_str_base = rd_patch_addr_cfm.memdata;
	#if (NEW_PATCH_BUFFER_MAP)
	rd_version_addr = RAM_FMAC_FW_ADDR + 0x01C;
	if ((ret = rwnx_send_dbg_mem_read_req(rwnx_hw, rd_version_addr, &rd_patch_addr_cfm))) {
		printk("version val[0x%x] rd fail: %d\n", rd_version_addr, ret);
		return ret;
	}
	rd_version_val = rd_patch_addr_cfm.memdata;
	printk("rd_version_val=%08X\n", rd_version_val);
	// sdiodev->fw_version_uint = rd_version_val;
	if (rd_version_val > 0x06090100) {
		patch_buff_addr = rd_patch_addr + 12;
		ret = rwnx_send_dbg_mem_read_req(rwnx_hw, patch_buff_addr, &rd_patch_addr_cfm);
		if (ret) {
			printk("patch buf rd fail\n");
			return ret;
		}
		patch_buff_base = rd_patch_addr_cfm.memdata;
		patch_addr = start_addr = patch_buff_base;
	}
	#endif

	ret = rwnx_send_dbg_mem_write_req(rwnx_hw, AIC_PATCH_ADDR(magic_num), AIC_PATCH_MAGIG_NUM);
	if (ret) {
		printk("0x%x write fail\n", AIC_PATCH_ADDR(magic_num));
		return ret;
	}

	ret = rwnx_send_dbg_mem_write_req(rwnx_hw, AIC_PATCH_ADDR(magic_num_2), AIC_PATCH_MAGIG_NUM_2);
	if (ret) {
		printk("0x%x write fail\n", AIC_PATCH_ADDR(magic_num_2));
		return ret;
	}

	ret = rwnx_send_dbg_mem_write_req(rwnx_hw, AIC_PATCH_ADDR(pair_start), patch_addr);
	if (ret) {
		printk("0x%x write fail\n", AIC_PATCH_ADDR(pair_start));
		return ret;
	}

	ret = rwnx_send_dbg_mem_write_req(rwnx_hw, AIC_PATCH_ADDR(pair_count), patch_cnt + adap_patch_cnt);
	if (ret) {
		printk("0x%x write fail\n", AIC_PATCH_ADDR(pair_count));
		return ret;
	}

	for (cnt = 0; cnt < patch_cnt; cnt++) {
		ret = rwnx_send_dbg_mem_write_req(rwnx_hw, start_addr+8*cnt, patch_tbl_8800d80[cnt][0]+config_base);
		if (ret) {
			printk("%x write fail\n", start_addr+8*cnt);
			return ret;
		}
		ret = rwnx_send_dbg_mem_write_req(rwnx_hw, start_addr+8*cnt+4, patch_tbl_8800d80[cnt][1]);
		if (ret) {
			printk("%x write fail\n", start_addr+8*cnt+4);
			return ret;
		}
	}

	if (adap_test){
		int tmp_cnt = patch_cnt + adap_patch_cnt;
		for (cnt = patch_cnt; cnt < tmp_cnt; cnt++) {
			int tbl_idx = cnt - patch_cnt;
			ret = rwnx_send_dbg_mem_write_req(rwnx_hw, start_addr+8*cnt, adaptivity_patch_tbl_8800d80[tbl_idx][0]+config_base);
			if(ret) {
				printk("%x write fail\n", start_addr+8*cnt);
				return ret;
			}
			ret = rwnx_send_dbg_mem_write_req(rwnx_hw, start_addr+8*cnt+4, adaptivity_patch_tbl_8800d80[tbl_idx][1]);
			if(ret) {
				printk("%x write fail\n", start_addr+8*cnt+4);
				return ret;
			}
		}
	}

	ret = rwnx_send_dbg_mem_write_req(rwnx_hw, AIC_PATCH_ADDR(block_size[0]), 0);
	if (ret) {
		printk("block_size[0x%x] write fail: %d\n", AIC_PATCH_ADDR(block_size[0]), ret);
		return ret;
	}
	ret = rwnx_send_dbg_mem_write_req(rwnx_hw, AIC_PATCH_ADDR(block_size[1]), 0);
	if (ret) {
		printk("block_size[0x%x] write fail: %d\n", AIC_PATCH_ADDR(block_size[1]), ret);
		return ret;
	}
	ret = rwnx_send_dbg_mem_write_req(rwnx_hw, AIC_PATCH_ADDR(block_size[2]), 0);
	if (ret) {
		printk("block_size[0x%x] write fail: %d\n", AIC_PATCH_ADDR(block_size[2]), ret);
		return ret;
	}
	ret = rwnx_send_dbg_mem_write_req(rwnx_hw, AIC_PATCH_ADDR(block_size[3]), 0);
	if (ret) {
		printk("block_size[0x%x] write fail: %d\n", AIC_PATCH_ADDR(block_size[3]), ret);
		return ret;
	}

	return 0;
}

u32 syscfg_tbl[][2] = {
	{0x40500014, 0x00000101}, // 1)
	{0x40500018, 0x00000109}, // 2)
	{0x40500004, 0x00000010}, // 3) the order should not be changed

	// def CONFIG_PMIC_SETTING
	// U02 bootrom only
	{0x40040000, 0x00001AC8}, // 1) fix panic
	{0x40040084, 0x00011580},
	{0x40040080, 0x00000001},
	{0x40100058, 0x00000000},

	{0x50000000, 0x03220204}, // 2) pmic interface init
	{0x50019150, 0x00000002}, // 3) for 26m xtal, set div1
	{0x50017008, 0x00000000}, // 4) stop wdg
};

u32 syscfg_tbl_masked[][3] = {
	{0x40506024, 0x000000FF, 0x000000DF}, // for clk gate lp_level
};

u32 rf_tbl_masked[][3] = {
	{0x40344058, 0x00800000, 0x00000000},// pll trx
};

u32 syscfg_tbl_8800dc[][2] = {
    {0x40500010, 0x00000004},
    {0x40500010, 0x00000006},//160m clk
};

u32 syscfg_tbl_8800dc_sdio_u01[][2] = {
    {0x40030000, 0x00036724}, // loop forever after assert_err
    {0x0011E800, 0xE7FE4070},
    {0x40030084, 0x0011E800},
    {0x40030080, 0x00000001},
    {0x4010001C, 0x00000000},
};

u32 syscfg_tbl_8800dc_sdio_u02[][2] = {
    {0x40030000, 0x00036DA4}, // loop forever after assert_err
    {0x0011E800, 0xE7FE4070},
    {0x40030084, 0x0011E800},
    {0x40030080, 0x00000001},
    {0x4010001C, 0x00000000},
};

u32 syscfg_tbl_masked_8800dc[][3] = {
    //#ifdef CONFIG_PMIC_SETTING
    #if defined(CONFIG_VRF_DCDC_MODE)
    {0x7000216C, (0x3 << 2), (0x1 << 2)}, // pmic_pmu_init
    {0x700021BC, (0x3 << 2), (0x1 << 2)},
    {0x70002118, ((0x7 << 4) | (0x1 << 7)), ((0x2 << 4) | (0x1 << 7))},
    {0x70002104, ((0x3F << 0) | (0x1 << 6)), ((0x2 << 0) | (0x1 << 6))},
    {0x7000210C, ((0x3F << 0) | (0x1 << 6)), ((0x2 << 0) | (0x1 << 6))},
    {0x70002170, (0xF << 0), (0x1 << 0)},
    {0x70002190, (0x3F << 0), (24 << 0)},
    {0x700021CC, ((0x7 << 4) | (0x1 << 7)), ((0x0 << 4) | (0x0 << 7))},
    {0x700010A0, (0x1 << 11), (0x1 << 11)},
    {0x70001034, ((0x1 << 20) | (0x7 << 26)), ((0x0 << 20) | (0x2 << 26))},
    {0x70001038, (0x1 << 8), (0x1 << 8)},
    {0x70001094, (0x3 << 2), (0x0 << 2)},
    {0x700021D0, ((0x1 << 5) | (0x1 << 6)), ((0x1 << 5) | (0x1 << 6))},
    {0x70001000, ((0x1 << 0) | (0x1 << 20) | (0x1 << 22)),
                 ((0x1 << 0) | (0x1 << 20) | (0x0 << 22))},
    {0x70001028, (0xf << 2), (0x1 << 2)},
    #else
    {0x7000216C, (0x3 << 2), (0x1 << 2)}, // pmic_pmu_init
    {0x700021BC, (0x3 << 2), (0x1 << 2)},
    {0x70002118, ((0x7 << 4) | (0x1 << 7)), ((0x2 << 4) | (0x1 << 7))},
    {0x70002104, ((0x3F << 0) | (0x1 << 6)), ((0x2 << 0) | (0x1 << 6))},
    {0x7000210C, ((0x3F << 0) | (0x1 << 6)), ((0x2 << 0) | (0x1 << 6))},
    {0x70002170, (0xF << 0), (0x1 << 0)},
    {0x70002190, (0x3F << 0), (24 << 0)},
    {0x700021CC, ((0x7 << 4) | (0x1 << 7)), ((0x0 << 4) | (0x0 << 7))},
    {0x700010A0, (0x1 << 11), (0x1 << 11)},
    {0x70001034, ((0x1 << 20) | (0x7 << 26)), ((0x0 << 20) | (0x2 << 26))},
    {0x70001038, (0x1 << 8), (0x1 << 8)},
    {0x70001094, (0x3 << 2), (0x0 << 2)},
    {0x700021D0, ((0x1 << 5) | (0x1 << 6)), ((0x1 << 5) | (0x1 << 6))},
    {0x70001000, ((0x1 << 0) | (0x1 << 20) | (0x1 << 22)),
                 ((0x0 << 0) | (0x1 << 20) | (0x0 << 22))},
    {0x70001028, (0xf << 2), (0x1 << 2)},
    #endif
    //#endif /* CONFIG_PMIC_SETTING */
    {0x00000000, 0x00000000, 0x00000000}, // last one
};

u32 syscfg_tbl_masked_8800dc_h[][3] = {
    {0x7000216C, ((0x3 << 2) | (0x3 << 4)), ((0x2 << 2) | (0x2 << 4))}, // pmic_pmu_init
    {0x70002138, (0xFF << 0), (0xFF << 0)},
    {0x7000213C, (0xFF << 0), (0xFF << 0)},
    {0x70002144, (0xFF << 0), (0xFF << 0)},
    {0x700021BC, (0x3 << 2), (0x1 << 2)},
    {0x70002118, ((0x7 << 4) | (0x1 << 7)), ((0x2 << 4) | (0x1 << 7))},
    {0x70002104, ((0x3F << 0) | (0x1 << 6)), ((0x2 << 0) | (0x1 << 6))},
    {0x7000210C, ((0x3F << 0) | (0x1 << 6)), ((0x2 << 0) | (0x1 << 6))},
    {0x70002170, (0xF << 0), (0x1 << 0)},
    {0x70002190, (0x3F << 0), (24 << 0)},
    {0x700021CC, ((0x7 << 4) | (0x1 << 7)), ((0x0 << 4) | (0x0 << 7))},
    {0x700010A0, (0x1 << 11), (0x1 << 11)},
    //{0x70001034, ((0x1 << 20) | (0x7 << 26)), ((0x0 << 20) | (0x2 << 26))},
    {0x70001038, (0x1 << 8), (0x1 << 8)},
    {0x70001094, (0x3 << 2), (0x0 << 2)},
    {0x700021D0, ((0x1 << 5) | (0x1 << 6)), ((0x1 << 5) | (0x1 << 6))},
    #if defined(CONFIG_VRF_DCDC_MODE)
    {0x70001000, ((0x1 << 0) | (0x1 << 20) | (0x1 << 22)),
                 ((0x1 << 0) | (0x1 << 20) | (0x0 << 22))},
    #else
    {0x70001000, ((0x1 << 0) | (0x1 << 20) | (0x1 << 22)),
                 ((0x0 << 0) | (0x1 << 20) | (0x0 << 22))},
    #endif
    {0x70001028, (0xf << 2), (0x1 << 2)},

    {0x00000000, 0x00000000, 0x00000000}, // last one
};

u32 syscfg_tbl_masked_8800dc_u01[][3] = {
    //#ifdef CONFIG_PMIC_SETTING
    {0x70001000, (0x1 << 16), (0x1 << 16)}, // for low temperature
    {0x70001028, (0x1 << 6), (0x1 << 6)},
    {0x70001000, (0x1 << 16), (0x0 << 16)},
    //#endif /* CONFIG_PMIC_SETTING */
};

u32 aicbsp_syscfg_tbl_8800d80[][2] = {
};

u32 syscfg_tbl_masked_8800d80[][3] = {
//	{0x40506024, 0x000000FF, 0x000000DF}, // for clk gate lp_level
};

u32 rf_tbl_masked_8800d80[][3] = {
//	{0x40344058, 0x00800000, 0x00000000},// pll trx
};

/**
 * @brief Check fw state, Called before system_config
 *
 * @param rwnx_hw
 * @return int 0:fw_down 1:fw_up
 */
int fw_state_check(struct rwnx_hw *rwnx_hw)
{
    int ret, fw_state;
    const u32 mem_addr = 0x40500004;
    struct dbg_mem_read_cfm rd_mem_addr_cfm;
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, mem_addr, &rd_mem_addr_cfm);
    if (ret) {
        AIC_LOG_PRINTF("%x rd fail: %d\n", mem_addr, ret);
        return -1;
    }
    fw_state = (int)(rd_mem_addr_cfm.memdata >> 4) & 0x01;
    AIC_LOG_PRINTF("fw_state:%x\n", fw_state);
    return fw_state;
}

static void system_config(struct rwnx_hw *rwnx_hw)
{
    int syscfg_num;
    int ret, cnt;
    const u32 mem_addr = 0x40500000;
    struct dbg_mem_read_cfm rd_mem_addr_cfm;
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, mem_addr, &rd_mem_addr_cfm);
    if (ret) {
        printk("%x rd fail: %d\n", mem_addr, ret);
        return;
    }
    chip_id = (u8)(rd_mem_addr_cfm.memdata >> 16);
    printk("%x=%x\n", rd_mem_addr_cfm.memaddr, rd_mem_addr_cfm.memdata);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, 0x00000004, &rd_mem_addr_cfm);
    if (ret) {
        printk("[0x00000004] rd fail: %d\n", ret);
        return;
    }
    chip_sub_id = (u8)(rd_mem_addr_cfm.memdata >> 4);
    //printk("%x=%x\n", rd_mem_addr_cfm.memaddr, rd_mem_addr_cfm.memdata);
    printk("chip_id=%x, chip_sub_id=%x\n", chip_id, chip_sub_id);

    syscfg_num = sizeof(syscfg_tbl) / sizeof(u32) / 2;
    for (cnt = 0; cnt < syscfg_num; cnt++) {
        ret = rwnx_send_dbg_mem_write_req(rwnx_hw, syscfg_tbl[cnt][0], syscfg_tbl[cnt][1]);
        if (ret) {
            printk("%x write fail: %d\n", syscfg_tbl[cnt][0], ret);
            return;
        }
    }

    syscfg_num = sizeof(syscfg_tbl_masked) / sizeof(u32) / 3;
    for (cnt = 0; cnt < syscfg_num; cnt++) {
        ret = rwnx_send_dbg_mem_mask_write_req(rwnx_hw,
            syscfg_tbl_masked[cnt][0], syscfg_tbl_masked[cnt][1], syscfg_tbl_masked[cnt][2]);
        if (ret) {
            printk("%x mask write fail: %d\n", syscfg_tbl_masked[cnt][0], ret);
            return;
        }
    }

    ret = rwnx_send_dbg_mem_mask_write_req(rwnx_hw,
                rf_tbl_masked[0][0], rf_tbl_masked[0][1], rf_tbl_masked[0][2]);
    if (ret) {
        printk("rf config %x write fail: %d\n", rf_tbl_masked[0][0], ret);
    }
}

static void system_config_8800dc(struct rwnx_hw *rwnx_hw)
{
    int syscfg_num;
    array3_tbl_t p_syscfg_msk_tbl;
    int ret, cnt;
    const u32 mem_addr = 0x40500000;
    struct dbg_mem_read_cfm rd_mem_addr_cfm;

    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, mem_addr, &rd_mem_addr_cfm);
    if (ret) {
        printk("%x rd fail: %d\n", mem_addr, ret);
        return;
    }
    chip_id = (u8)(rd_mem_addr_cfm.memdata >> 16);
    //printk("%x=%x\n", rd_mem_addr_cfm.memaddr, rd_mem_addr_cfm.memdata);
    if (((rd_mem_addr_cfm.memdata >> 25) & 0x01UL) == 0x00UL) {
        chip_mcu_id = 1;
    }

    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, 0x00000020, &rd_mem_addr_cfm);
    if (ret) {
        printk("[0x00000020] rd fail: %d\n", ret);
        return;
    }
    chip_sub_id = (u8)(rd_mem_addr_cfm.memdata);
    //printk("%x=%x\n", rd_mem_addr_cfm.memaddr, rd_mem_addr_cfm.memdata);
    printk("chip_id=%x, chip_sub_id=%x\n", chip_id, chip_sub_id);
	if (IS_CHIP_ID_H()) {
		printk("is 8800dcdw_h chip");
	}

    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, 0x40500010, &rd_mem_addr_cfm);
    printk("[0x40500010]=%x\n", rd_mem_addr_cfm.memdata);
    if (ret) {
        printk("[0x40500010] rd fail: %d\n", ret);
        return;
    }

    syscfg_num = sizeof(syscfg_tbl_8800dc) / sizeof(u32) / 2;

    for (cnt = 0; cnt < syscfg_num; cnt++) {
        ret = rwnx_send_dbg_mem_write_req(rwnx_hw, syscfg_tbl_8800dc[cnt][0], syscfg_tbl_8800dc[cnt][1]);
        if (ret) {
            printk("%x write fail: %d\n", syscfg_tbl_8800dc[cnt][0], ret);
            return;
        }
    }

    if (chip_mcu_id == 0) {
        if (chip_sub_id == 0) {
            syscfg_num = sizeof(syscfg_tbl_8800dc_sdio_u01) / sizeof(u32) / 2;
            for (cnt = 0; cnt < syscfg_num; cnt++) {
                ret = rwnx_send_dbg_mem_write_req(rwnx_hw, syscfg_tbl_8800dc_sdio_u01[cnt][0], syscfg_tbl_8800dc_sdio_u01[cnt][1]);
                if (ret) {
                     printk("%x write fail: %d\n", syscfg_tbl_8800dc_sdio_u01[cnt][0], ret);
                    return;
                }
            }
        } else if ((chip_sub_id == 1) || (chip_sub_id == 2)) {
            syscfg_num = sizeof(syscfg_tbl_8800dc_sdio_u02) / sizeof(u32) / 2;
            for (cnt = 0; cnt < syscfg_num; cnt++) {
                ret = rwnx_send_dbg_mem_write_req(rwnx_hw, syscfg_tbl_8800dc_sdio_u02[cnt][0], syscfg_tbl_8800dc_sdio_u02[cnt][1]);
                if (ret) {
                    printk("%x write fail: %d\n", syscfg_tbl_8800dc_sdio_u02[cnt][0], ret);
                    return;
                }
            }
        }
    }

    if (IS_CHIP_ID_H()) {
        syscfg_num = sizeof(syscfg_tbl_masked_8800dc_h) / sizeof(u32) / 3;
        p_syscfg_msk_tbl = syscfg_tbl_masked_8800dc_h;
    } else {
        syscfg_num = sizeof(syscfg_tbl_masked_8800dc) / sizeof(u32) / 3;
        p_syscfg_msk_tbl = syscfg_tbl_masked_8800dc;
    }

    for (cnt = 0; cnt < syscfg_num; cnt++) {
        if (p_syscfg_msk_tbl[cnt][0] == 0x00000000) {
            break;
        } else if (p_syscfg_msk_tbl[cnt][0] == 0x70001000) {
            if (chip_mcu_id == 0) {
                p_syscfg_msk_tbl[cnt][1] |= ((0x1 << 8) | (0x1 << 15)); // mask
                p_syscfg_msk_tbl[cnt][2] |= ((0x1 << 8) | (0x1 << 15));
            }
        }

        ret = rwnx_send_dbg_mem_mask_write_req(rwnx_hw,
            p_syscfg_msk_tbl[cnt][0], p_syscfg_msk_tbl[cnt][1], p_syscfg_msk_tbl[cnt][2]);
        if (ret) {
            printk("%x mask write fail: %d\n", p_syscfg_msk_tbl[cnt][0], ret);
            return;
        }
    }

    if (chip_sub_id == 0) {
        syscfg_num = sizeof(syscfg_tbl_masked_8800dc_u01) / sizeof(u32) / 3;
        for (cnt = 0; cnt < syscfg_num; cnt++) {
            ret = rwnx_send_dbg_mem_mask_write_req(rwnx_hw,
                syscfg_tbl_masked_8800dc_u01[cnt][0], syscfg_tbl_masked_8800dc_u01[cnt][1], syscfg_tbl_masked_8800dc_u01[cnt][2]);
            if (ret) {
                printk("%x mask write fail: %d\n", syscfg_tbl_masked_8800dc_u01[cnt][0], ret);
                return;
            }
        }
    }
}

static void system_config_8800d80(struct rwnx_hw *rwnx_hw)
{
    int syscfg_num;
    int ret, cnt;
    const u32 mem_addr = 0x40500000;
    struct dbg_mem_read_cfm rd_mem_addr_cfm;
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, mem_addr, &rd_mem_addr_cfm);
    if (ret) {
        printk("%x rd fail: %d\n", mem_addr, ret);
        return;
    }
    chip_id = (u8)(rd_mem_addr_cfm.memdata >> 16);
    printk("%x=%x\n", rd_mem_addr_cfm.memaddr, rd_mem_addr_cfm.memdata);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, 0x00000020, &rd_mem_addr_cfm);
    if (ret) {
        printk("[0x00000020] rd fail: %d\n", ret);
        return;
    }
    chip_sub_id = (u8)(rd_mem_addr_cfm.memdata);
    printk("flag:%x=%x\n", rd_mem_addr_cfm.memaddr, rd_mem_addr_cfm.memdata);
    printk("chip_id=%x, chip_sub_id=%x\n", chip_id, chip_sub_id);

    syscfg_num = sizeof(aicbsp_syscfg_tbl_8800d80) / sizeof(u32) / 2;
    for (cnt = 0; cnt < syscfg_num; cnt++) {
        ret = rwnx_send_dbg_mem_write_req(rwnx_hw, aicbsp_syscfg_tbl_8800d80[cnt][0], aicbsp_syscfg_tbl_8800d80[cnt][1]);
        if (ret) {
            printk("%x write fail: %d\n", aicbsp_syscfg_tbl_8800d80[cnt][0], ret);
            return;
        }
    }
}

static void sys_config_8800d80(struct rwnx_hw *rwnx_hw)
{
	int ret, cnt;
	int syscfg_num = sizeof(syscfg_tbl_masked_8800d80) / sizeof(u32) / 3;
	for (cnt = 0; cnt < syscfg_num; cnt++) {
		ret = rwnx_send_dbg_mem_mask_write_req(rwnx_hw,
			syscfg_tbl_masked_8800d80[cnt][0], syscfg_tbl_masked_8800d80[cnt][1], syscfg_tbl_masked_8800d80[cnt][2]);
		if (ret) {
			printk("%x mask write fail: %d\n", syscfg_tbl_masked_8800d80[cnt][0], ret);
			return;
		}
	}

	ret = rwnx_send_dbg_mem_mask_write_req(rwnx_hw,
				rf_tbl_masked_8800d80[0][0], rf_tbl_masked_8800d80[0][1], rf_tbl_masked_8800d80[0][2]);
	if (ret) {
		printk("rf config %x write fail: %d\n", rf_tbl_masked_8800d80[0][0], ret);
		return;
	}
}

void sys_aic_reboot(struct rwnx_hw *rwnx_hw)
{
#if 0
    int ret = rwnx_send_dbg_mem_write_req(rwnx_hw, 0x70001400, 0x1F);
    if (ret) {
        printk("%x write fail: %d\n", 0x70001400, ret);
        return;
    }
    ret = rwnx_send_dbg_mem_write_req(rwnx_hw, 0x70001408, 0x2);
    if (ret) {
        printk("%x write fail: %d\n", 0x70001408, ret);
        return;
    }
#else
    int ret = 0;
    ret = rwnx_send_dbg_start_app_req(rwnx_hw, 0, HOST_START_APP_REBOOT);
    if (ret) {
        return;
    }

#endif
}

void sys_aic_wdt(struct rwnx_hw *rwnx_hw, uint8_t cmd, uint32_t seconds)
{
    int ret = 0;

     uint32_t param =  ((cmd&0xFF)<<24) |(seconds&0xFFFFFF);
     aic_dbg("sys_aic_wdt %x %x %x\r\n", cmd,seconds, param);
    ret = rwnx_send_dbg_start_app_req(rwnx_hw, param, HOST_START_APP_FNCALL);
    if (ret) {
        return;
    }
}

#if 0
static void rf_config(struct rwnx_hw *rwnx_hw)
{
    int ret;
    ret = rwnx_send_dbg_mem_mask_write_req(rwnx_hw,
                rf_tbl_masked[0][0], rf_tbl_masked[0][1], rf_tbl_masked[0][2]);
    if (ret) {
        printk("rf config %x write fail: %d\n", rf_tbl_masked[0][0], ret);
    }
}
#endif

static int start_from_bootrom(struct rwnx_hw *rwnx_hw)
{
    int ret = 0;

    /* memory access */
#ifdef CONFIG_ROM_PATCH_EN
    const u32 rd_addr = ROM_FMAC_FW_ADDR;
    const u32 fw_addr = ROM_FMAC_FW_ADDR;
#else
    const u32 rd_addr = RAM_FMAC_FW_ADDR;
    const u32 fw_addr = RAM_FMAC_FW_ADDR;
#endif
    struct dbg_mem_read_cfm rd_cfm;
    printk("Read FW mem: %08x\n", rd_addr);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, rd_addr, &rd_cfm);
    if (ret) {
        return -1;
    }
    printk("cfm: [%08x] = %08x\n", rd_cfm.memaddr, rd_cfm.memdata);

    /* fw start */
    printk("Start app: %08x\n", fw_addr);
    ret = rwnx_send_dbg_start_app_req(rwnx_hw, fw_addr, HOST_START_APP_AUTO);
    if (ret) {
        return -1;
    }
    return 0;
}
#ifdef CONFIG_DYNAMIC_PERPWR
#define RSSI_GET_INTERVAL                (10 * 1000)   //time interval
#define RSSI_THD_0                       (-30)            //rssi 0 (dBm)
#define RSSI_THD_1                       (-60)            //rssi 1 (dBm)
#define RSSI_THD_2                       (-75)            //rssi 2 (dBm)

#define PWR_LOSS_LVL0                     (-10)             //RSSI > RSSI_THD_0
#define PWR_LOSS_LVL1                     (-5 )             //RSSI_THD_1 < RSSI <RSSI_THD_0
#define PWR_LOSS_LVL2                     (0)             //RSSI_THD_2 < RSSI <RSSI_THD_1
#define PWR_LOSS_LVL3                     (2)             //RSSI <RSSI_THD_2
void set_txpwrloss_ctrl(struct rwnx_hw *rwnx_hw, s8 value);

#endif
static rtos_task_handle sta_mgmt_hdl = NULL;
static rtos_queue sta_mgmt_queue = NULL;
static rtos_semaphore rwnx_sta_mgmt_task_exit_sem = NULL;
static bool rwnx_sta_mgmt_task_exit_flag = false;
extern void aicwf_update_txpwer_per_sta(struct sta_info_tag *sta);
void rwnx_sta_mgmt_task(void *param)
{
    struct rwnx_hw* rwnx_hw = (struct rwnx_hw *)param;

    struct sta_mgmt_msg_st msg;
    int ret;

    while (1) {
        ret = rtos_queue_read(sta_mgmt_queue, &msg, -1, false);
        #ifdef PLATFORM_SUNPLUS_ECOS
        if (rwnx_sta_mgmt_task_exit_flag) {
            break;
        }
        #endif
        if (ret == 0) {
            switch (msg.msg_id) {
                case APM_STA_LOSS:
                {
                    ret = wlan_ap_disassociate_sta(msg.mac_addr);
                    if (ret < 0) {
                        aic_dbg("wlan_ap_disassociate_sta fail, ret=%d\n", ret);
                    }
                }
                break;
                #ifdef CONFIG_DYNAMIC_PERPWR
                case STA_TX_PWR_UPDATE:
                {
                    struct sta_info_tag *sta = NULL;
                    struct fhost_vif_tag *fhost_vif = &fhost_env.vif[0];
                    
                    #define RWNX_VIF_TYPE(vif) fhost_vif->mac_vif->type
                    if (RWNX_VIF_TYPE(fhost_vif) == VIF_MONITOR) {
                        return;
                    } else if ((RWNX_VIF_TYPE(fhost_vif) == VIF_STA)) {
                        // printk("%s: sta mode\n", __func__);
                        if(fhost_vif->ap_id < NX_REMOTE_STA_MAX) {
                            sta = vif_mgmt_get_sta_by_staid(fhost_vif->ap_id);
                        }
                    } else {
                        // printk("%s: AP mode\n", __func__);
                        sta = vif_mgmt_get_sta_by_addr(msg.mac_addr);
                    }
                    aicwf_update_txpwer_per_sta(sta);
                }
                break;
                #endif /* CONFIG_DYNAMIC_PERPWR */
             case PRO_DISC_REQ:
                {
                    ret = user_wps_button_pushed();
                    if (ret < 0) {
                        aic_dbg("user_wps_button_pushed fail, ret=%d\n", ret);
                    }
                }
                break;
                default:
                break;
            }

        }
    }
exit:
    printk("Exit rwnx_sta_mgmt_task\r\n");
    #ifdef PLATFORM_GX_ECOS
    rtos_semaphore_signal(rwnx_sta_mgmt_task_exit_sem, false);
    #endif
}

int rwnx_sta_mgmt_init(struct rwnx_hw *rwnx_hw)
{
    int ret;

    if (sta_mgmt_hdl) {
        aic_dbg("Err: rwnx_sta_mgmt_task exist\n");
        return -1;
    }

    ret = rtos_queue_create(sizeof(struct sta_mgmt_msg_st), 40, &sta_mgmt_queue, "sta_mgmt_queue");
    if (ret) {
        aic_dbg("Err: sta_mgmt_queue create fail, ret=%d\n", ret);
        return -2;
    }
    if (rtos_semaphore_create(&rwnx_sta_mgmt_task_exit_sem, "rwnx_sta_mgmt_task_exit_sem", 0x7FFFFFFF, 0)) {
        aic_dbg("rwnx_sta_mgmt_task_exit_sem create fail\n");
        return -3;
    }
    ret = rtos_task_create(rwnx_sta_mgmt_task, "rwnx_sta_mgmt_task", RWNX_STA_MGMT_TASK,
                    rwnx_sta_mgmt_stack_size, (void *)rwnx_hw, rwnx_sta_mgmt_priority, &sta_mgmt_hdl);
    if (ret) {
        aic_dbg("Err: rwnx_sta_mgmt_task create fail, ret=%d\n", ret);
        return -4;
    }

    return 0;
}

int rwnx_sta_mgmt_deinit(void)
{
    uint32_t msg;

    if (sta_mgmt_hdl) {
        rwnx_sta_mgmt_task_exit_flag = true;
        int ret = rtos_queue_write(sta_mgmt_queue, "rwnx_sta_mgmt_task Exit Signal", -1, false);
        rtos_semaphore_wait(rwnx_sta_mgmt_task_exit_sem, -1);
        rwnx_sta_mgmt_task_exit_flag = false;
        rtos_task_delete(sta_mgmt_hdl);
        sta_mgmt_hdl = NULL;
    }
    // flush apm_staloss queue
    if (rtos_queue_cnt(sta_mgmt_queue) > 0) {
        AIC_LOG_PRINTF("sta_mgmt_queue cnt:%d\n", rtos_queue_cnt(sta_mgmt_queue));
    }
    while (!rtos_queue_is_empty(sta_mgmt_queue)) {
        rtos_queue_read(sta_mgmt_queue, &msg, 30, false);
        AIC_LOG_PRINTF("sta_mgmt_queue msg:%X\n", msg);
    }
    rtos_semaphore_delete(rwnx_sta_mgmt_task_exit_sem);
    rtos_queue_delete(sta_mgmt_queue);
    sta_mgmt_queue = NULL;

    return 0;
}

int rwnx_apm_staloss_notify(struct mm_apm_staloss_ind *ind)
{
    int ret;
    struct sta_mgmt_msg_st msg;
    msg.msg_id = APM_STA_LOSS;
    memcpy(msg.mac_addr, ind->mac_addr, 6);
    ret = rtos_queue_write(sta_mgmt_queue, &msg, -1, false);

    return ret;
}

int rwnx_p2p_disc_req(uint8_t *mac_addr)
{
    int ret;
    struct sta_mgmt_msg_st msg;
    msg.msg_id = PRO_DISC_REQ;
    //memcpy(msg.mac_addr, mac_addr, 6);
    ret = rtos_queue_write(sta_mgmt_queue, &msg, -1, false);

    return ret;
}
static int start_from_bootrom_8800dc(struct rwnx_hw *rwnx_hw)
{
    int ret = 0;
    u32 rd_addr;
    u32 fw_addr;
    u32 boot_type;
    struct dbg_mem_read_cfm rd_cfm;

    /* memory access */
    if(rwnx_hw->mode == WIFI_MODE_RFTEST){
        rd_addr = RAM_LMAC_FW_ADDR;
        fw_addr = RAM_LMAC_FW_ADDR;
    }
    else{
        rd_addr = RAM_FMAC_FW_ADDR;
        fw_addr = RAM_FMAC_FW_ADDR;
    }

    printk("Read FW mem: %08x\n", rd_addr);
    if ((ret = rwnx_send_dbg_mem_read_req(rwnx_hw, rd_addr, &rd_cfm))) {
        return -1;
    }
    printk("cfm: [%08x] = %08x\n", rd_cfm.memaddr, rd_cfm.memdata);

    if(rwnx_hw->mode != WIFI_MODE_RFTEST){
        boot_type = HOST_START_APP_DUMMY;
    } else {
        boot_type = HOST_START_APP_AUTO;
        /* for rftest, sdio rx switch to func1 before start_app_req */
        func_flag_rx = false;
    }
    /* fw start */
    printk("Start app: %08x, %d\n", fw_addr, boot_type);
    if ((ret = rwnx_send_dbg_start_app_req(rwnx_hw, fw_addr, boot_type))) {
        return -1;
    }
    return 0;
}

int rwnx_fdrv_init(struct rwnx_hw *rwnx_hw)
{
    int ret = 0;
    struct mm_set_rf_calib_cfm rf_calib_cfm;
    struct mm_set_stack_start_cfm set_start_cfm;
    struct me_config_req me_config;
    struct mm_start_req start;
    struct mac_addr base_mac_addr;

    AIC_LOG_PRINTF("rwnx_fdrv_init enter\n");

    ret = rwnx_sta_mgmt_init(rwnx_hw);
    if (ret) {
        goto err_platon;
    }

#if defined(CONFIG_AIC8801)
    if (rwnx_hw->chipid == PRODUCT_ID_AIC8801) {
        if (fw_state_check(rwnx_hw) == 0x1) {
            AIC_LOG_PRINTF("fw already loaded\n");
        }
    }
 #endif /* CONFIG_AIC8801 */
    AIC_LOG_PRINTF("chipid %x\n",rwnx_hw->chipid );
    // system config
    if (rwnx_hw->chipid == PRODUCT_ID_AIC8801) {
#if defined(CONFIG_AIC8801)
        system_config(rwnx_hw);
#endif /* CONFIG_AIC8801 */
    }  else if (rwnx_hw->chipid == PRODUCT_ID_AIC8800DC || rwnx_hw->chipid == PRODUCT_ID_AIC8800DW)
        system_config_8800dc(rwnx_hw);
    else if (rwnx_hw->chipid == PRODUCT_ID_AIC8800D80)
        system_config_8800d80(rwnx_hw);
extern int aicbt_init(struct rwnx_hw *rwnx_hw);
#ifdef CONFIG_BT_SUPPORT
    AIC_LOG_PRINTF("aicbt_init enter\n");
    aicbt_init(rwnx_hw);
#endif

    // platform on(load fw)
    ret = rwnx_platform_on(rwnx_hw);
    if (ret) {
        goto err_platon;
    }

#ifdef ADAP_TEST
    adap_test = 1;
#endif

    // patch config
    if (rwnx_hw->chipid == PRODUCT_ID_AIC8801) {
#if defined(CONFIG_AIC8801)
    patch_config(rwnx_hw);
#endif
    }else if (rwnx_hw->chipid == PRODUCT_ID_AIC8800DC || rwnx_hw->chipid == PRODUCT_ID_AIC8800DW) {
        aicwf_patch_config_8800dc(rwnx_hw);
    } else if (rwnx_hw->chipid == PRODUCT_ID_AIC8800D80) {
        ret = aicwifi_patch_config_8800d80(rwnx_hw);
        if (ret) {
            printk("AIC aicwifi_patch_config_8800d80 fail\n");
        }
        //sys_config_8800d80(rwnx_hw);
    }

    // start from bootrom
    if (rwnx_hw->chipid == PRODUCT_ID_AIC8801 || rwnx_hw->chipid == PRODUCT_ID_AIC8800D80)
        ret = start_from_bootrom(rwnx_hw);
    else if (rwnx_hw->chipid == PRODUCT_ID_AIC8800DC || rwnx_hw->chipid == PRODUCT_ID_AIC8800DW)
        ret = start_from_bootrom_8800dc(rwnx_hw);
    if (ret) {
        goto err_lmac_reqs;
    }

    // release sdio function2
    if (rwnx_hw->chipid == PRODUCT_ID_AIC8800DC || rwnx_hw->chipid == PRODUCT_ID_AIC8800DW) {
        if ((rwnx_hw->mode != WIFI_MODE_RFTEST)) {
            func_flag_tx = false;
            sdio_release_func2();
        }
    }

#ifdef CONFIG_GPIOINT_WAKEUPPIN
    if (rwnx_hw->chipid == PRODUCT_ID_AIC8800D80) {
        u32 memdata_temp = 0x00000006;
        //ret = rwnx_send_dbg_mem_write_req(rwnx_hw, 0x40504084, 0x00000006);
        ret = rwnx_send_dbg_mem_block_write_req(rwnx_hw, 0x40504084, 4, &memdata_temp);
        if (ret) {
            printk("[0x40504084] write fail: %d\n", ret);
            return;
        } else {
            printk("[0x40504084] write succsee: %d\n", ret);
        }

        struct dbg_mem_read_cfm rd_addr_cfm;
        ret = rwnx_send_dbg_mem_read_req(rwnx_hw, 0x40504084, &rd_addr_cfm);
        if (ret) {
            printk(" rd fail\n");
        }
        printk("addr [0x40504084] = %x\n", rd_addr_cfm.memdata);
    }
#endif /* CONFIG_GPIOINT_WAKEUPPIN */

#ifdef USE_5G
    if (rwnx_hw->chipid == PRODUCT_ID_AIC8801) {
        ret = rwnx_send_set_stack_start_req(rwnx_hw, 1, 0, CO_BIT(5), 0, &set_start_cfm);
    }
#else
    if (rwnx_hw->chipid == PRODUCT_ID_AIC8801) {
        ret = rwnx_send_set_stack_start_req(rwnx_hw, 1, 0, 0, 0, &set_start_cfm);
    }
#endif
    else if (rwnx_hw->chipid == PRODUCT_ID_AIC8800DC ||
            rwnx_hw->chipid == PRODUCT_ID_AIC8800DW){
        ret = rwnx_send_set_stack_start_req(rwnx_hw, 1, 0, 0, 0, &set_start_cfm);
        set_start_cfm.is_5g_support = false;
    } else {
        ret = rwnx_send_set_stack_start_req(rwnx_hw, 1, 0, CO_BIT(5), 0, &set_start_cfm);
    }

    // ic rf init
    if (rwnx_hw->chipid == PRODUCT_ID_AIC8801) {
        ret = rwnx_send_txpwr_idx_req(rwnx_hw);
        if (ret) {
            goto err_platon;
        }
        ret = rwnx_send_txpwr_ofst_req(rwnx_hw);
        if (ret) {
            goto err_platon;
        }
        if (rwnx_hw->mode != WIFI_MODE_RFTEST) {
            ret = rwnx_send_rf_calib_req(rwnx_hw, &rf_calib_cfm);
            if (ret) {
                goto err_platon;
            }
        }
    } else if(rwnx_hw->chipid == PRODUCT_ID_AIC8800DC ||
                rwnx_hw->chipid == PRODUCT_ID_AIC8800DW) {
        ret = aicwf_set_rf_config_8800dc(rwnx_hw, &rf_calib_cfm);
        if (ret) {
            goto err_platon;
        }
    } else if (rwnx_hw->chipid == PRODUCT_ID_AIC8800D80) {
        ret = aicwf_set_rf_config_8800d80(rwnx_hw, &rf_calib_cfm);
        if (ret) {
            goto err_platon;
        }
    }
    /* Reset FW */
    ret = rwnx_send_reset(rwnx_hw);
    if (ret) {
        goto err_lmac_reqs;
    }
    ret = rwnx_send_version_req(rwnx_hw, &rwnx_hw->version_cfm);
    if (ret) {
        goto err_lmac_reqs;
    }
    /* Set parameters to firmware */
    fhost_config_prepare(&me_config, &start, &base_mac_addr, true);

    if (rwnx_hw->chipid == PRODUCT_ID_AIC8801 ||
        ((rwnx_hw->chipid == PRODUCT_ID_AIC8800DC||
        rwnx_hw->chipid == PRODUCT_ID_AIC8800DW ||
        rwnx_hw->chipid == PRODUCT_ID_AIC8800D80) && rwnx_hw->mode != WIFI_MODE_RFTEST)){
        rwnx_send_me_config_req(rwnx_hw, &me_config);
        rwnx_send_me_chan_config_req(rwnx_hw, &fhost_chan);
    }

    if (rwnx_hw->mode != WIFI_MODE_RFTEST) {
        rwnx_send_start(rwnx_hw, &start);
    }

#ifdef CONFIG_SDIO_BUS_PWRCTRL
    aicwf_sdio_bus_pwrctl_timer_task_init();
#endif /* CONFIG_SDIO_BUS_PWRCTRL */

    AIC_LOG_PRINTF("rwnx_fdrv_init exit\n");
    return ret;

err_lmac_reqs:
    printk("err_lmac_reqs\n");
    return ret;

err_platon:
    printk("err_platon\n");
    return ret;
}

#ifdef CONFIG_RWNX_RADAR
extern struct rwnx_hw *g_rwnx_hw;
#define RWNX_CH_NOT_SET 0xFF
/**
 * Link channel ctxt to a vif and thus increments count for this context.
 */
void rwnx_chanctx_link(struct fhost_vif_tag *vif, u8 ch_idx,
					   struct cfg80211_chan_def *chandef)
{
	struct rwnx_chanctx *ctxt;

	if (ch_idx >= NX_CHAN_CTXT_CNT) {
		aic_dbg("Invalid channel ctxt id %d", ch_idx);
		return;
	}

	vif->chan_index = ch_idx;
	ctxt = &g_rwnx_hw->chanctx_table[ch_idx];
	ctxt->count++;

	// For now chandef is NULL for STATION interface
	if (chandef) {
		if (!ctxt->chan_def.chan.center_freq)
			ctxt->chan_def = *chandef;
		else {
			// TODO. check that chandef is the same as the one already
			// set for this ctxt
		}
	}
    // aic_dbg("%s, %x ch_idx %d  %d\r\n", __func__, ctxt->chan_def.chan, ch_idx, ctxt->chan_def.chan.flags);
}

/**
 * Unlink channel ctxt from a vif and thus decrements count for this context
 */
void rwnx_chanctx_unlink(struct fhost_vif_tag *vif)
{
	struct rwnx_chanctx *ctxt;

	if (vif->chan_index == RWNX_CH_NOT_SET)
		return;

	ctxt = &g_rwnx_hw->chanctx_table[vif->chan_index];

	if (ctxt->count == 0) {
		aic_dbg("Chan ctxt ref count is already 0");
	} else {
		ctxt->count--;
	}

	if (ctxt->count == 0) {
		if (vif->chan_index == g_rwnx_hw->cur_chanctx) {
			/* If current chan ctxt is no longer linked to a vif
			   disable radar detection (no need to check if it was activated) */
			rwnx_radar_detection_enable(&g_rwnx_hw->radar,
										RWNX_RADAR_DETECT_DISABLE,
										RWNX_RADAR_RIU);
		}
		/* set chan to null, so that if this ctxt is relinked to a vif that
		   don't have channel information, don't use wrong information */
		ctxt->chan_def.chan.center_freq = 0;
	}
	vif->chan_index = RWNX_CH_NOT_SET;
}

int rwnx_send_apm_start_cac_req(struct rwnx_hw *rwnx_hw, uint8_t vif_index,
								struct aic_80211_chan_def *chandef,
								struct apm_start_cac_cfm *cfm);
int rwnx_start_radar_detection(struct aic_80211_chan_def *chandef)
{
	struct rwnx_hw *rwnx_hw = g_rwnx_hw;
	struct fhost_vif_tag *vif = fhost_from_mac_vif(0);
	struct apm_start_cac_cfm cfm;
    u32 cac_time_ms = 60000;

	rwnx_radar_start_cac(&rwnx_hw->radar, cac_time_ms, vif, chandef);

    if(rwnx_hw->radar.dpd[RWNX_RADAR_RIU]->region == NL80211_DFS_JP) {
        rwnx_radar_switch_mic_band(&rwnx_hw->radar, chandef->chan.center_freq);
    }
	rwnx_send_apm_start_cac_req(rwnx_hw, 0, chandef, &cfm);
	if (cfm.status == CO_OK) {
		//rtos_mutex_lock(rwnx_hw->cb_lock);
		rwnx_chanctx_link(vif, cfm.ch_idx, chandef);
		if (rwnx_hw->cur_chanctx == vif->chan_index)
			rwnx_radar_detection_enable(&rwnx_hw->radar,
										RWNX_RADAR_DETECT_REPORT,
										RWNX_RADAR_RIU);
		//rtos_mutex_unlock(rwnx_hw->cb_lock);
	} else {
		return -1;
	}

	return 0;
}
#endif /* CONFIG_RWNX_RADAR */

#ifdef CONFIG_DYNAMIC_PERPWR
#define RSSI_GET_INTERVAL                (10 * 1000)   //time interval


#define RSSI_THD_0                       (-20)         //rssi 0 (dBm)
#define RSSI_THD_1                       (-30)         //rssi 1 (dBm)
#define RSSI_THD_2                       (-75)         //rssi 2 (dBm)

#define PWR_LOSS_LVL0                     (-10)        //RSSI > RSSI_THD_0
#define PWR_LOSS_LVL1                     (-5 )        //RSSI_THD_1 < RSSI <RSSI_THD_0
#define PWR_LOSS_LVL2                     (0)          //RSSI_THD_2 < RSSI <RSSI_THD_1
#define PWR_LOSS_LVL3                     (0)//(2)          //RSSI <RSSI_THD_2

#define PWR_DELAY_TIME                   (10 * 1000)   //pwr reduced latency time (ms)
extern int rwnx_update_txpwer(uint8_t *addr);
void rssi_update_txpwrloss(uint8_t *sta_addr, s8_l rssi)
{
	int rssi_avg;
    struct sta_info_tag *sta = NULL;
    struct fhost_vif_tag *fhost_vif = &fhost_env.vif[0];

    #define RWNX_VIF_TYPE(vif) fhost_vif->mac_vif->type
    if (RWNX_VIF_TYPE(fhost_vif) == VIF_MONITOR) {
        return;
    } else if ((RWNX_VIF_TYPE(fhost_vif) == VIF_STA)) {
        // printk("%s: sta mode\n", __func__);
        if(fhost_vif->ap_id < NX_REMOTE_STA_MAX) {
            sta = vif_mgmt_get_sta_by_staid(fhost_vif->ap_id);
        }
    } else {
        // printk("%s: AP mode\n", __func__);
        sta = vif_mgmt_get_sta_by_addr(sta_addr);
    }
	rssi_avg = (sta->rssi_save + rssi) / 2;
	sta->rssi_save = rssi_avg;

    // printk("New RSSI: %d, Average RSSI: %d\n", rssi, rssi_avg);

	if(rssi_avg > RSSI_THD_0) {
		if (sta->per_pwrloss != PWR_LOSS_LVL0) {
			if (((rtos_now(0) - sta->last_jiffies) < PWR_DELAY_TIME) && (sta->per_pwrloss >= PWR_LOSS_LVL0)) {
				//printk("delay p1\n");
				return;
			}
			rwnx_update_txpwer(sta_addr);
			sta->per_pwrloss = PWR_LOSS_LVL0;
			sta->last_jiffies = rtos_now(0);
		}
	} else if ((rssi_avg > RSSI_THD_1) && (rssi_avg <= RSSI_THD_0)) {
		if (sta->per_pwrloss != PWR_LOSS_LVL1) {
			if (((rtos_now(0) - sta->last_jiffies) < PWR_DELAY_TIME) && (sta->per_pwrloss >= PWR_LOSS_LVL1)) {
				//printk("delay p2\n");
				return;
			}
			rwnx_update_txpwer(sta_addr);
			sta->per_pwrloss = PWR_LOSS_LVL1;
			sta->last_jiffies = rtos_now(0);
		}
	} else if ((rssi_avg > RSSI_THD_2) && (rssi_avg <= RSSI_THD_1)) {
		if (sta->per_pwrloss != PWR_LOSS_LVL2) {
			if (((rtos_now(0) - sta->last_jiffies) < PWR_DELAY_TIME) && (sta->per_pwrloss >= PWR_LOSS_LVL2)) {
				//printk("delay p3\n");
				return;
			}
			rwnx_update_txpwer(sta_addr);
			sta->per_pwrloss = PWR_LOSS_LVL2;
			sta->last_jiffies = rtos_now(0);
		}
	} else if (rssi_avg <= RSSI_THD_2) {
		if (sta->per_pwrloss != PWR_LOSS_LVL3) {
			rwnx_update_txpwer(sta_addr);
			sta->per_pwrloss = PWR_LOSS_LVL3;
			sta->last_jiffies = rtos_now(0);
		}
	}
}

extern int rwnx_send_txpwr_per_sta_req(struct rwnx_hw *rwnx_hw, struct sta_info_tag *sta);
void aicwf_update_txpwer_per_sta(struct sta_info_tag *sta)
{
	// printk("txpwr_per_sta: id: %d\n", sta->staid);
    extern struct rwnx_hw *g_rwnx_hw;
	rwnx_send_txpwr_per_sta_req(g_rwnx_hw, sta);
}


int rwnx_update_txpwer(uint8_t *addr)
{
    int ret;
    struct sta_mgmt_msg_st msg;
    msg.msg_id = STA_TX_PWR_UPDATE;
    memcpy(msg.mac_addr, addr, 6);
    ret = rtos_queue_write(sta_mgmt_queue, &msg, 30, false);

    return ret;
}
#endif /* CONFIG_DYNAMIC_PERPWR */
