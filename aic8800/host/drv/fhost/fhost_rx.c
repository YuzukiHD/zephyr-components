/**
 ****************************************************************************************
 *
 * @file fhost_rx.c
 *
 * @brief Implementation of the fully hosted RX task.
 *
 * Copyright (C) RivieraWaves 2017-2019
 *
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @addtogroup FHOST_RX
 * @{
 ****************************************************************************************
 */
/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "fhost_rx.h"
#include "fhost_rx_def.h"
#include "fhost.h"
#include "co_endian.h"
#include "co_utils.h"
#include "cfgrwnx.h"
#include "rwnx_config.h"
#include "rwnx_rx.h"
#include "rwnx_msg_rx.h"
#include "mac_frame.h"
#include "fhost_cntrl.h"
#include "rwnx_utils.h"
#include "rwnx_main.h"
#include "fhost_tx.h"
#ifdef CONFIG_SDIO_SUPPORT
#include "sdio_def.h"
#endif
#ifdef CONFIG_USB_SUPPORT
#endif
#include "net_al.h"
#include "wifi.h"
#include "platform.h"
#include "endian.h"

/*
 * GLOBAL VARIABLES
 ****************************************************************************************
 */
struct fhost_rx_env_tag fhost_rx_env;
static rtos_semaphore fhost_rx_task_exit_sem = NULL;
static bool fhost_rx_task_exit_flag = false;

/// RFC1042 LLC/SNAP Header
const struct llc_snap_short llc_rfc1042_hdr = {
                                                  0xAAAA, // DSAP LSAP
                                                  0x0003, // Control/Prot0
                                                  0x0000, // Prot1 and 2
                                              };

/// Bridge-Tunnel LLC/SNAP Header
const struct llc_snap_short llc_bridge_tunnel_hdr = {
                                                        0xAAAA, // DSAP LSAP
                                                        0x0003, // Control/Prot0
                                                        0xF800, // Prot1 and 2
                                                    };
#if 0
/// Pool of RX buffers
static struct fhost_rx_buf_tag fhost_rx_buf_mem[FHOST_RX_BUF_CNT] __SHAREDRAM;
#if NX_UF_EN
/// Pool of UF buffers
static struct fhost_rx_uf_buf_tag fhost_rx_uf_buf_mem[FHOST_RX_BUF_CNT] __SHAREDRAM;
#endif // NX_UF_EN
#endif

static rtos_task_handle fhost_rx_task_hdl = NULL;
#ifdef CONFIG_FHOST_RX_ASYNC
#define RX_ASYNC_DESC_CNT    80
static struct fhost_rx_async_desc_tag rx_async_desc_pool[RX_ASYNC_DESC_CNT];
#ifdef CONFIG_SDIO_SUPPORT
static struct sdio_buf_node_s *rx_frame_in_process = NULL;
static uint8 * rx_in_process_buf = NULL;
#endif
#ifdef CONFIG_USB_SUPPORT
static struct aicwf_usb_buf *rx_frame_in_process = NULL;
#endif
static bool rx_frame_to_async = false;
#endif

/*
 * FUNCTIONS
 ****************************************************************************************
 */
/**
 ****************************************************************************************
 * @brief Push a RX buffer to the WiFi task.
 * This buffer can then be used by the WiFi task to copy a received MPDU.
 *
 * @param[in] net_buf   Pointer to the RX buffer to push
 ****************************************************************************************
 */
void fhost_rx_buf_push(void *net_buf)
{
    struct fhost_rx_buf_tag *buf = net_buf;

    //aic_dbg("<%s>, buf %x\r\n", __func__, buf);
    buf->info.pattern = RX_BUFFER_PUSHED;
    //ipc_host_rxbuf_push(&ipc_env, (uint32_t)buf, (uint32_t)&(buf->net_hdr.hdr));
}

void fhost_rx_buf_free(void *net_buf)
{
    fhost_rx_buf_push(net_buf);
}


/**
 ****************************************************************************************
 * @brief Forward a RX buffer containing a A-MSDU to the networking stack.
 *
 * @param[in] buf Pointer to the RX buffer to forward
 ****************************************************************************************
 */
static void fhost_rx_amsdu_forward(struct fhost_rx_buf_tag *buf)
{
#ifdef NX_AMSDU
    struct rx_info *info =&buf->info;
    uint8_t vif_idx = (info->flags & RX_FLAGS_VIF_INDEX_MSK) >> RX_FLAGS_VIF_INDEX_OFT;
    struct fhost_vif_tag *fhost_vif = fhost_env.mac2fhost_vif[vif_idx];
    net_if_t *net_if = &fhost_vif->net_if;
    uint8_t *payload;
    uint16_t len;
    int subframe_idx = 0;
    struct llc_snap *llc_snap;
    uint8_t offset;

    do
    {
        offset = 0;

        // Get payload pointer
        payload = (uint8_t *)buf->payload;

        // Get the subframe length
        len = co_ntohs(co_read16(&payload[LLC_ETHERTYPE_LEN_OFT])) + LLC_ETHER_HDR_LEN;

        // Map LLC/SNAP structure on buffer
        llc_snap = (struct llc_snap *)&payload[LLC_ETHER_HDR_LEN];
        if ((!memcmp(llc_snap, &llc_rfc1042_hdr, sizeof(llc_rfc1042_hdr))
             //&& (llc_snap->proto_id != LLC_ETHERTYPE_AARP) - Appletalk depracated ?
             && (llc_snap->proto_id != LLC_ETHERTYPE_IPX))
            || (!memcmp(llc_snap, &llc_bridge_tunnel_hdr, sizeof(llc_bridge_tunnel_hdr))))
        {
            // Packet becomes
            /********************************************
             *  DA  |  SA  |  SNAP->ETHERTYPE  |  DATA  |
             ********************************************/
            // We remove the LLC/SNAP, so adjust the length and the offset
            len -= LLC_802_2_HDR_LEN;
            offset = LLC_802_2_HDR_LEN;

            // Move the source/dest addresses at the right place
            MAC_ADDR_CPY(&payload[offset + MAC_ADDR_LEN], &payload[MAC_ADDR_LEN]);
            MAC_ADDR_CPY(&payload[offset], &payload[0]);

            // No need to copy the Ethertype which is already in place
        }

        // Forward to the networking stack
        net_if_input((net_buf_rx_t *)buf, net_if, &payload[offset], len, fhost_rx_buf_free);

        // Check if we may still have some subframes
        if (subframe_idx == (NX_MAX_MSDU_PER_RX_AMSDU - 1))
            break;

        // Get next subframe
        #if NX_AMSDU_DEAGG
        buf = (struct fhost_rx_buf_tag *)info->amsdu_hostids[subframe_idx++];
        #endif
    } while (buf != NULL);
#endif
}

/**
****************************************************************************************
* @brief Forward a MGMT frame to the registered callback.
*
* @param[in] buf Pointer to the RX buffer to forward
****************************************************************************************
*/
static void fhost_rx_mgmt_buf_forward(struct fhost_rx_buf_tag *buf)
{
   struct fhost_frame_info info;
   //int8_t rx_rssi[2];

   if (fhost_rx_env.mgmt_cb == NULL)
       return;

   info.payload = (uint8_t *)buf->payload;
   info.length = buf->info.vect.frmlen;
   info.freq = PHY_INFO_CHAN(buf->info.phy_info);
   //info.rssi = hal_desc_get_rssi(&buf->info.vect.rx_vec_1, rx_rssi);

   fhost_rx_env.mgmt_cb(&info, fhost_rx_env.mgmt_cb_arg);
}

uint8_t machdr_len_get(uint16_t frame_cntl)
{
    // MAC Header length
    uint8_t mac_hdr_len = MAC_SHORT_MAC_HDR_LEN;

    // Check if Address 4 field is present (FDS and TDS set to 1)
    if ((frame_cntl & (MAC_FCTRL_TODS | MAC_FCTRL_FROMDS))
                                    == (MAC_FCTRL_TODS | MAC_FCTRL_FROMDS))
    {
        mac_hdr_len += (MAC_LONG_MAC_HDR_LEN - MAC_SHORT_MAC_HDR_LEN);
    }

    // Check if QoS Control Field is present
    if (IS_QOS_DATA(frame_cntl))
    {
        mac_hdr_len += (MAC_LONG_QOS_MAC_HDR_LEN - MAC_LONG_MAC_HDR_LEN);
    }

    // Check if HT Control Field is present (Order bit set to 1)
    if (frame_cntl & MAC_FCTRL_ORDER)
    {
        mac_hdr_len += (MAC_LONG_QOS_HTC_MAC_HDR_LEN - MAC_LONG_QOS_MAC_HDR_LEN);
    }

    return (mac_hdr_len);
}

static uint8_t fhost_mac2ethernet(void *buf)
{
    struct fhost_rx_buf_tag *rx_buf = (struct fhost_rx_buf_tag *)buf;
    uint8_t *frame = (uint8_t *)rx_buf->payload;
    uint32_t statinfo = rx_buf->info.vect.statinfo;
    struct mac_hdr *machdr_ptr = (struct mac_hdr *)frame;
    struct mac_addr da;
    struct mac_addr sa;
    // LLC/SNAP part of the PDU
    struct llc_snap *llc_snap;
    struct mac_eth_hdr *eth_hdr;
    // Compute MAC Header Length (will IV length + EIV length if present)
    uint8_t machdr_len = 0;
    uint8_t payl_offset = 0;

    //aic_dbg("fhost_mac2ethernet, len %d\r\n", rx_buf->info.vect.frmlen); //1542
    //dump_b(frame, 128);

    //aic_dbg("fhost_mac2ethernet, fctl %x, %x, %x\r\n", machdr_ptr->fctl, machdr_ptr->fctl & MAC_FCTRL_TYPE_MASK, MAC_FCTRL_DATA_T);
    //if(MAC_FCTRL_DATA_T == (machdr_ptr->fctl & MAC_FCTRL_TYPE_MASK))
    //    aic_dbg("S %d\r\n", machdr_ptr->seq >> 4);

    if ((machdr_ptr->fctl & MAC_FCTRL_TYPE_MASK) != MAC_FCTRL_DATA_T)
        return payl_offset;

    // Get DA
    if (machdr_ptr->fctl & MAC_FCTRL_TODS)
    {
        MAC_ADDR_CPY(&da, &machdr_ptr->addr3);
    }
    else
    {
        MAC_ADDR_CPY(&da, &machdr_ptr->addr1);
    }

    // Get SA
    if (machdr_ptr->fctl & MAC_FCTRL_FROMDS)
    {

        MAC_ADDR_CPY(&sa, &machdr_ptr->addr3);
    }
    else
    {
        MAC_ADDR_CPY(&sa, &machdr_ptr->addr2);
    }

    //dump_buf(&sa, 6);
    //dump_buf(&da, 6);

    machdr_len = machdr_len_get(machdr_ptr->fctl);
    //aic_dbg("fhost_mac2ethernet, machdr_len %d\r\n", machdr_len);
    //aic_dbg("fhost_mac2ethernet, statinfo %08x\r\n", statinfo);

    switch (statinfo & RX_HD_DECRSTATUS)
    {
        case RX_HD_DECR_CCMP128:
        case RX_HD_DECR_TKIP:
            machdr_len += MAC_IV_LEN + MAC_EIV_LEN;
            break;
        case RX_HD_DECR_WEP:
            machdr_len += MAC_IV_LEN;
            break;
        default:
            break;
    }

    payl_offset = machdr_len + sizeof(struct llc_snap) - sizeof(struct mac_eth_hdr);
    //aic_dbg("fhost_mac2ethernet, payl_offset %d\r\n", payl_offset);

    // Pointer to the payload - Skip MAC Header
    llc_snap = (struct llc_snap *)((uint16_t *)machdr_ptr + (machdr_len >> 1));
    //dump_buf(llc_snap, 8);
    /********************************
     *  DA  |  SA  |  LEN  |  DATA  |
     ********************************/
    /*
     * Ethernet Header will start 7 half-words (MAC Address length is 6 bytes and Length
     * field is 2 bytes) before LLC Snap
     */
    eth_hdr = (struct mac_eth_hdr *)((uint16_t *)llc_snap - 3);

    // Set length (Initial length - MAC Header Length)
    //eth_hdr->len = co_htons(rx_buf->info.vect.frmlen - machdr_len);
    // Set DA and SA in the Ethernet Header
    MAC_ADDR_CPY(&eth_hdr->da, &da);
    MAC_ADDR_CPY(&eth_hdr->sa, &sa);

    //aic_dbg("fhost_ethernet, len %d\r\n", eth_hdr->len);
    //dump_buf(eth_hdr, 128);

    return payl_offset;
}
#if NX_BEACONING
static __maybe_unused uint8_t fhost_frame2others(void *buf, struct mac_addr *mac)
{
    struct fhost_rx_buf_tag *rx_buf = (struct fhost_rx_buf_tag *)buf;
    uint8_t *frame = (uint8_t *)rx_buf->payload;
    struct mac_hdr *machdr_ptr = (struct mac_hdr *)frame;
    struct mac_addr da;
    struct mac_addr sa;

    //AIC_LOG_PRINTF("fhost_frame2others %x\r\n", machdr_ptr->fctl);

    if ((machdr_ptr->fctl & MAC_FCTRL_TYPE_MASK) != MAC_FCTRL_DATA_T)
        return false;

    // Get DA
    if (machdr_ptr->fctl & MAC_FCTRL_TODS)
    {
        MAC_ADDR_CPY(&da, &machdr_ptr->addr3);
    }
    else
    {
        MAC_ADDR_CPY(&da, &machdr_ptr->addr1);
    }

    // Get SA
    if (machdr_ptr->fctl & MAC_FCTRL_FROMDS)
    {

        MAC_ADDR_CPY(&sa, &machdr_ptr->addr3);
    }
    else
    {
        MAC_ADDR_CPY(&sa, &machdr_ptr->addr2);
    }

    if(!(MAC_ADDR_CMP(&da, mac))) {
        return true;
    }

    return false;
}
#endif



/**
 ****************************************************************************************
 * @brief Forward a RX buffer to the networking stack.
 *
 * @param[in] buf Pointer to the RX buffer to forward
 ****************************************************************************************
 */
#ifndef CONFIG_RX_NOCOPY
void fhost_rx_buf_forward(struct fhost_rx_buf_tag *buf)
{
    struct rx_info *info =&buf->info;
    uint8_t payl_offset = 0;

    //rwnx_data_dump("info", info, sizeof(struct rx_info));
    //rwnx_data_dump("payload", buf->payload, 8);

    // Check if the buffer can be forwarded as is
    if (info->flags & RX_FLAGS_IS_AMSDU_BIT)
    {
        // Packet is a A-MSDU, forward each MSDU to the networking stack
        fhost_rx_amsdu_forward((struct fhost_rx_buf_tag *)buf);
    }
    else if (info->flags & RX_FLAGS_NON_MSDU_MSK)
    {
        fhost_rx_mgmt_buf_forward(buf);
        fhost_rx_buf_free(buf);
    }
    else
    {
        uint8_t vif_idx = (info->flags & RX_FLAGS_VIF_INDEX_MSK) >> RX_FLAGS_VIF_INDEX_OFT;
        struct fhost_vif_tag *fhost_vif = fhost_env.mac2fhost_vif[vif_idx];
        if (!fhost_vif) {
            aic_dbg("%s %d\r\n", __func__, vif_idx);
            fhost_rx_buf_free(buf);
            return;
        }
        net_if_t *net_if = &fhost_vif->net_if;

        // Data frame
        // MAC802.11 -> 802.3
        payl_offset = fhost_mac2ethernet(buf);

        struct mac_eth_hdr* ethhdr = (struct mac_eth_hdr*)((uint8_t *)buf->payload + payl_offset);
        uint8_t *ethdata = (uint8_t *)buf->payload + payl_offset + sizeof(struct mac_eth_hdr);
        #ifdef CONFIG_PING_DUMP
        if (co_ntohs(ethhdr->type) == LLC_ETHERTYPE_IP) {
            if (ethdata[9] == 0x01) {
                char ipaddr_str[44];
                uint8_t type = ethdata[20];
                uint8_t code = ethdata[21];
                sprintf(ipaddr_str, "src:%d.%d.%d.%d dst:%d.%d.%d.%d",
                    ethdata[12], ethdata[13], ethdata[14], ethdata[15],
                    ethdata[16], ethdata[17], ethdata[18], ethdata[19]);
                if ((type == 0x08) || (type == 0x00)) {
                    uint16_t sn = ethdata[27] | (ethdata[26] << 8);
                    char echo_str[16];
                    if (type == 0x08) {
                       sprintf(echo_str, "%s", "echo request");
                    } else {
                        sprintf(echo_str, "%s", "echo reply");
                    }
                    AIC_LOG_PRINTF("ICMP rx %s sn:%d, %s\n", echo_str, sn, ipaddr_str);
                } else {
                    AIC_LOG_PRINTF("ICMP rx type:%d, code:%d, %s\n", type, code, ipaddr_str);
                }
            }
        } else if (co_ntohs(ethhdr->type) == LLC_ETHERTYPE_IPV6) {
            //todo
        }
        #endif
        if (co_ntohs(ethhdr->type) == LLC_ETHERTYPE_EAP_T) {
            aic_dbg("R eapol, len %d(%d)\r\n",  (info->vect.frmlen - payl_offset), rtos_now(0));
            net_eth_receive((uint8_t *)buf->payload + payl_offset, (info->vect.frmlen - payl_offset), net_if);
        } else {
            // Forward to the networking stack
            rx_eth_data_process((uint8_t *)buf->payload + payl_offset, info->vect.frmlen - 
                                                                payl_offset,  net_if);
        }
    }
}
#else
void fhost_rx_buf_forward(struct fhost_rx_buf_tag *buf, struct pbuf *p_buf)
{
    struct rx_info *info =&buf->info;
    uint8_t payl_offset = 0;

    // uint8_t *frame = (uint8_t *)buf->payload;
    // struct mac_hdr *machdr_ptr = (struct mac_hdr *)frame;
    //rwnx_data_dump("info", info, sizeof(struct rx_info));
    //rwnx_data_dump("payload", buf->payload, 8);

    // Check if the buffer can be forwarded as is
    if (info->flags & RX_FLAGS_IS_AMSDU_BIT)
    {
        // Packet is a A-MSDU, forward each MSDU to the networking stack
        fhost_rx_amsdu_forward((struct fhost_rx_buf_tag *)buf);
    }
    else if (info->flags & RX_FLAGS_NON_MSDU_MSK)
    {
        fhost_rx_mgmt_buf_forward(buf);
        fhost_rx_buf_free(buf);
    }
    else
    {
        uint8_t vif_idx = (info->flags & RX_FLAGS_VIF_INDEX_MSK) >> RX_FLAGS_VIF_INDEX_OFT;
        struct fhost_vif_tag *fhost_vif = fhost_env.mac2fhost_vif[vif_idx];
        if (!fhost_vif) {
            aic_dbg("%s %d\r\n", __func__, vif_idx);
            if (NULL == p_buf) {
                fhost_rx_buf_free(buf);
            } else {
                pbuf_free(p_buf);
            }
            return;
        }
        net_if_t *net_if = &fhost_vif->net_if;

        // Data frame
        // MAC802.11 -> 802.3
        payl_offset = fhost_mac2ethernet(buf);

        struct mac_eth_hdr* ethhdr = (struct mac_eth_hdr*)((uint8_t *)buf->payload + payl_offset);
        #ifdef CONFIG_PING_DUMP
        uint8_t *ethdata = (uint8_t *)buf->payload + payl_offset + sizeof(struct mac_eth_hdr);
        if (co_ntohs(ethhdr->type) == LLC_ETHERTYPE_IP) {
            if (ethdata[9] == 0x01) {
                char ipaddr_str[44];
                uint8_t type = ethdata[20];
                uint8_t code = ethdata[21];
                sprintf(ipaddr_str, "src:%d.%d.%d.%d dst:%d.%d.%d.%d",
                    ethdata[12], ethdata[13], ethdata[14], ethdata[15],
                    ethdata[16], ethdata[17], ethdata[18], ethdata[19]);
                if ((type == 0x08) || (type == 0x00)) {
                    uint16_t sn = ethdata[27] | (ethdata[26] << 8);
                    char echo_str[16];
                    if (type == 0x08) {
                       sprintf(echo_str, "%s", "echo request");
                    } else {
                        sprintf(echo_str, "%s", "echo reply");
                    }
                    AIC_LOG_PRINTF("ICMP rx %s sn:%d, %s\n", echo_str, sn, ipaddr_str);
                } else {
                    AIC_LOG_PRINTF("ICMP rx type:%d, code:%d, %s\n", type, code, ipaddr_str);
                }
            }
        } else if (co_ntohs(ethhdr->type) == LLC_ETHERTYPE_IPV6) {
            //todo
        }
        #endif
        if (co_ntohs(ethhdr->type) == LLC_ETHERTYPE_EAP_T) {
            aic_dbg("R eapol, len %d(%d)\r\n",  (info->vect.frmlen - payl_offset), rtos_now(0));
            net_eth_receive((uint8_t *)buf->payload + payl_offset, (info->vect.frmlen - payl_offset), net_if);
        } else {
            // Forward to the networking stack
            if (buf->info.reserved[0] == 0xDB5F) {
                // rwnx_data_dump("$$Pbuf", p_buf->payload, p_buf->len);
                pbuf_header(p_buf, -(sizeof(struct rx_info) + payl_offset));
                // rwnx_data_dump("Pbuf", p_buf->payload, p_buf->len);
                // aic_dbg("Sq %d %d %d %d %d\r\n", (machdr_ptr->seq >> MAC_SEQCTRL_NUM_OFT), info->vect.frmlen, payl_offset, p_buf->len, sizeof(struct rx_info));
                rx_eth_data_process((uint8_t *)p_buf, info->vect.frmlen - 
                                                                payl_offset,  net_if, true);
            } else {
                rx_eth_data_process((uint8_t *)buf->payload + payl_offset, info->vect.frmlen - 
                                                                payl_offset,  net_if, false);
            }
        }
    }
}
#endif
#ifdef CONFIG_MONITOR_RADIO_HEADER
/**
 * rwnx_rx_vector_convert - Convert a legacy RX vector into a new RX vector format
 *
 * @rwnx_hw: main driver data.
 * @rx_vect1: Rx vector 1 descriptor of the received frame.
 * @rx_vect2: Rx vector 2 descriptor of the received frame.
 */
static void rwnx_rx_vector_convert(struct rwnx_hw *rwnx_hw,
								   struct rx_vector_1 *rx_vect1,
								   struct rx_vector_2 *rx_vect2)
{
	struct rx_vector_1_old rx_vect1_leg;
	struct rx_vector_2_old rx_vect2_leg;
	// u32_l phy_vers = rwnx_hw->version_cfm.version_phy_2;

	// Check if we need to do the conversion. Only if old modem is used
	// if (__MDM_MAJOR_VERSION(phy_vers) > 0) {
	// 	rx_vect1->rssi1 = rx_vect1->rssi_leg;
	// 	return;
	// }

	// Copy the received vector locally
	memcpy(&rx_vect1_leg, rx_vect1, sizeof(struct rx_vector_1_old));

	// Reset it
	memset(rx_vect1, 0, sizeof(struct rx_vector_1));

	// Perform the conversion
	rx_vect1->format_mod = rx_vect1_leg.format_mod;
	rx_vect1->ch_bw = rx_vect1_leg.ch_bw;
	rx_vect1->antenna_set = rx_vect1_leg.antenna_set;
	rx_vect1->leg_length = rx_vect1_leg.leg_length;
	rx_vect1->leg_rate = rx_vect1_leg.leg_rate;
	rx_vect1->rssi1 = rx_vect1_leg.rssi1;

	switch (rx_vect1->format_mod) {
	case FORMATMOD_NON_HT:
	case FORMATMOD_NON_HT_DUP_OFDM:
		rx_vect1->leg.lsig_valid = rx_vect1_leg.lsig_valid;
		rx_vect1->leg.chn_bw_in_non_ht = rx_vect1_leg.num_extn_ss;
		rx_vect1->leg.dyn_bw_in_non_ht = rx_vect1_leg.dyn_bw;
		break;
	case FORMATMOD_HT_MF:
	case FORMATMOD_HT_GF:
		rx_vect1->ht.aggregation = rx_vect1_leg.aggregation;
		rx_vect1->ht.fec = rx_vect1_leg.fec_coding;
		rx_vect1->ht.lsig_valid = rx_vect1_leg.lsig_valid;
		rx_vect1->ht.length = rx_vect1_leg.ht_length;
		rx_vect1->ht.mcs = rx_vect1_leg.mcs;
		rx_vect1->ht.num_extn_ss = rx_vect1_leg.num_extn_ss;
		rx_vect1->ht.short_gi = rx_vect1_leg.short_gi;
		rx_vect1->ht.smoothing = rx_vect1_leg.smoothing;
		rx_vect1->ht.sounding = rx_vect1_leg.sounding;
		rx_vect1->ht.stbc = rx_vect1_leg.stbc;
		break;
	case FORMATMOD_VHT:
		rx_vect1->vht.beamformed = !rx_vect1_leg.smoothing;
		rx_vect1->vht.fec = rx_vect1_leg.fec_coding;
		rx_vect1->vht.length = rx_vect1_leg.ht_length | rx_vect1_leg._ht_length << 8;
		rx_vect1->vht.mcs = rx_vect1_leg.mcs & 0x0F;
		rx_vect1->vht.nss = rx_vect1_leg.stbc ? rx_vect1_leg.n_sts/2 : rx_vect1_leg.n_sts;
		rx_vect1->vht.doze_not_allowed = rx_vect1_leg.doze_not_allowed;
		rx_vect1->vht.short_gi = rx_vect1_leg.short_gi;
		rx_vect1->vht.sounding = rx_vect1_leg.sounding;
		rx_vect1->vht.stbc = rx_vect1_leg.stbc;
		rx_vect1->vht.group_id = rx_vect1_leg.group_id;
		rx_vect1->vht.partial_aid = rx_vect1_leg.partial_aid;
		rx_vect1->vht.first_user = rx_vect1_leg.first_user;
		break;
	}

	if (!rx_vect2)
		return;

	// Copy the received vector 2 locally
	memcpy(&rx_vect2_leg, rx_vect2, sizeof(struct rx_vector_2_old));

	// Reset it
	memset(rx_vect2, 0, sizeof(struct rx_vector_2));

	rx_vect2->rcpi1 = rx_vect2_leg.rcpi;
	rx_vect2->rcpi2 = rx_vect2_leg.rcpi;
	rx_vect2->rcpi3 = rx_vect2_leg.rcpi;
	rx_vect2->rcpi4 = rx_vect2_leg.rcpi;

	rx_vect2->evm1 = rx_vect2_leg.evm1;
	rx_vect2->evm2 = rx_vect2_leg.evm2;
	rx_vect2->evm3 = rx_vect2_leg.evm3;
	rx_vect2->evm4 = rx_vect2_leg.evm4;
}
static inline unsigned int generic_hweight32(unsigned int w) {
    unsigned int res = (w & 0x55555555) + ((w >> 1) & 0x55555555);
    res = (res & 0x33333333) + ((res >> 2) & 0x33333333);
    res = (res & 0x0F0F0F0F) + ((res >> 4) & 0x0F0F0F0F);
    res = (res & 0x00FF00FF) + ((res >> 8) & 0x00FF00FF);
    return (res & 0x0000FFFF) + ((res >> 16) & 0x0000FFFF);
}
unsigned char hweight8(unsigned char n) {
    n = (n & 0x55) + ((n >> 1) & 0x55); // 每两个位一组相加
    n = (n & 0x33) + ((n >> 2) & 0x33); // 每四个位一组相加
    n = (n & 0x0F) + (n >> 4);         // 每八位一组相加
    return n;
}
#define ALIGN(x, align) (((x) + (align) - 1) & ~((align) - 1))

/**
 * rwnx_rx_rtap_hdrlen - Return radiotap header length
 *
 * @rxvect: Rx vector used to fill the radiotap header
 * @has_vend_rtap: boolean indicating if vendor specific data is present
 *
 * Compute the length of the radiotap header based on @rxvect and vendor
 * specific data (if any).
 */
static u8 rwnx_rx_rtap_hdrlen(struct rx_vector_1 *rxvect,
							  bool has_vend_rtap)
{
	u8 rtap_len;

	/* Compute radiotap header length */
	rtap_len = sizeof(struct ieee80211_radiotap_header) + 8;

	// Check for multiple antennas
	if (generic_hweight32(rxvect->antenna_set) > 1)
		// antenna and antenna signal fields
		rtap_len += 4 * hweight8(rxvect->antenna_set);

	// TSFT
	if (!has_vend_rtap) {
		rtap_len = ALIGN(rtap_len, 8);
		rtap_len += 8;
	}

	// IEEE80211_HW_SIGNAL_DBM
	rtap_len++;

	// Check if single antenna
	if (generic_hweight32(rxvect->antenna_set) == 1)
		rtap_len++; //Single antenna

	// padding for RX FLAGS
	rtap_len = ALIGN(rtap_len, 2);

    // aic_dbg("rtap_len %x\r\n", rtap_len);
    // aic_dbg("format %d\r\n", rxvect->format_mod);
	// Check for HT frames
	if ((rxvect->format_mod == FORMATMOD_HT_MF) ||
		(rxvect->format_mod == FORMATMOD_HT_GF))
		rtap_len += 3;

	// Check for AMPDU
	if (!(has_vend_rtap) && ((rxvect->format_mod >= FORMATMOD_VHT) ||
							 ((rxvect->format_mod > FORMATMOD_NON_HT_DUP_OFDM) &&
													 (rxvect->ht.aggregation)))) {
		rtap_len = ALIGN(rtap_len, 4);
		rtap_len += 8;
	}

	// Check for VHT frames
	if (rxvect->format_mod == FORMATMOD_VHT) {
		rtap_len = ALIGN(rtap_len, 2);
		rtap_len += 12;
	}

	// Check for HE frames
	if (rxvect->format_mod == FORMATMOD_HE_SU) {
		rtap_len = ALIGN(rtap_len, 2);
		rtap_len += sizeof(struct ieee80211_radiotap_he);
	}

	// Check for multiple antennas
	if (generic_hweight32(rxvect->antenna_set) > 1) {
		// antenna and antenna signal fields
		rtap_len += 2 * hweight8(rxvect->antenna_set);
	}

	// Check for vendor specific data
	if (has_vend_rtap) {
		/* vendor presence bitmap */
		rtap_len += 4;
		/* alignment for fixed 6-byte vendor data header */
		rtap_len = ALIGN(rtap_len, 2);
	}

	return rtap_len;
}
static void put_unaligned_le16(unsigned short val, unsigned char *p)
{
	*p++ = val;
	*p++ = val >> 8;
}

static void put_unaligned_le32(unsigned int val, void *p)
{
	unsigned char *ptr = (unsigned char *)p;

	ptr[0] = (val & 0xFF);
	ptr[1] = ((val >> 8) & 0xFF);
	ptr[2] = ((val >> 16) & 0xFF);
	ptr[3] = ((val >> 24) & 0xFF);
}

static void put_unaligned_le64(uint64_t value, void *dest) {
    uint8_t *ptr = (uint8_t*)dest;
    ptr[0] = (uint8_t)(value >> 0);
    ptr[1] = (uint8_t)(value >> 8);
    ptr[2] = (uint8_t)(value >> 16);
    ptr[3] = (uint8_t)(value >> 24);
    ptr[4] = (uint8_t)(value >> 32);
    ptr[5] = (uint8_t)(value >> 40);
    ptr[6] = (uint8_t)(value >> 48);
    ptr[7] = (uint8_t)(value >> 56);
}
// #define cpu_to_le16(d)                 (d)
// #define cpu_to_le32(d)                 (d)
#ifndef IEEE80211_MAX_CHAINS
#define IEEE80211_MAX_CHAINS 4
#endif

#define IEEE80211_CHAN_2GHZ       0x0080  // 2.4GHz频段
#define IEEE80211_CHAN_5GHZ       0x0100  // 5GHz频段
#define IEEE80211_CHAN_6GHZ       0x0200  // 6GHz频段（Wi-Fi 6E）
#define IEEE80211_CHAN_OFDM       0x0040  // OFDM调制（802.11a/g/n/ac/ax）
#define IEEE80211_CHAN_CCK        0x0020  // CCK调制（802.11b）
#define IEEE80211_CHAN_HT40PLUS   0x0800  // HT40上边带
#define IEEE80211_CHAN_HT40MINUS  0x0400  // HT40下边带
#define IEEE80211_CHAN_RADAR      0x0010  // 雷达检测信道
#define IEEE80211_CHAN_DYN        0x0020  // 动态DFS信道
#define IEEE80211_CHAN_NO_IR      0x0002  // 禁止初始发射（需先监听）
#define IEEE80211_CHAN_DISABLED   0x0001  // 信道禁用
#define IEEE80211_CHAN_NO_HT40    0x0040  // 禁用HT40模式
#define IEEE80211_CHAN_NO_80MHZ   0x1000  // 禁用80MHz带宽
#define IEEE80211_CHAN_NO_160MHZ  0x2000  // 禁用160MHz带宽
#define IEEE80211_CHAN_NO_20MHZ   0x8000  // 禁用20MHz基础模式
#define IEEE80211_CHAN_PSD        0x4000  // 功率谱密度限制（Wi-Fi 6）

/* MCS 字段的存在标志 (known) */
#define IEEE80211_RADIOTAP_MCS_HAVE_MCS   0x01  // MCS 索引存在
#define IEEE80211_RADIOTAP_MCS_HAVE_BW    0x02  // 带宽信息存在
#define IEEE80211_RADIOTAP_MCS_HAVE_GI    0x04  // Guard Interval 存在
#define IEEE80211_RADIOTAP_MCS_HAVE_FMT   0x08  // 格式（HT/VHT）存在
#define IEEE80211_RADIOTAP_MCS_HAVE_FEC   0x10  // LDPC 前向纠错存在
#define IEEE80211_RADIOTAP_MCS_HAVE_STBC  0x20  // 空时分组码存在
#define IEEE80211_RADIOTAP_MCS_HAVE_NESS  0x40  // 空间流数量信息存在

/* MCS 标志位 (flags) */
#define IEEE80211_RADIOTAP_MCS_BW_MASK    0x03  // 带宽掩码
#define IEEE80211_RADIOTAP_MCS_BW_20      0    // 20MHz 带宽
#define IEEE80211_RADIOTAP_MCS_BW_40      1    // 40MHz 带宽
#define IEEE80211_RADIOTAP_MCS_BW_20L     2    // 20MHz 低边带
#define IEEE80211_RADIOTAP_MCS_BW_20U     3    // 20MHz 高边带
#define IEEE80211_RADIOTAP_MCS_BW_80      4    // 80MHz 带宽 (802.11ac)
#define IEEE80211_RADIOTAP_MCS_BW_160     5    // 160MHz 带宽 (802.11ac)

#define IEEE80211_RADIOTAP_MCS_SGI        0x04  // 短保护间隔 (Short Guard Interval)
#define IEEE80211_RADIOTAP_MCS_LDPC       0x08  // LDPC 编码使用
#define IEEE80211_RADIOTAP_MCS_STBC_MASK  0x30  // STBC 掩码
#define IEEE80211_RADIOTAP_MCS_STBC_SHIFT 4     // STBC 移位值

#define IEEE80211_RADIOTAP_MCS_FMT_GF     0x40  // Greenfield 格式 (HT-only)
#define IEEE80211_RADIOTAP_MCS_FMT_LGI    0x80  // VHT 长保护间隔

/* IEEE80211 Radiotap VHT known flags */
#define IEEE80211_RADIOTAP_VHT_KNOWN_STBC            0x0001
#define IEEE80211_RADIOTAP_VHT_KNOWN_TXOP_PS_NA      0x0002  
#define IEEE80211_RADIOTAP_VHT_KNOWN_GI              0x0004
#define IEEE80211_RADIOTAP_VHT_KNOWN_SGI_NSYM_MCS    0x0008
#define IEEE80211_RADIOTAP_VHT_KNOWN_LDPC_EXTRA_OFDM 0x0010
#define IEEE80211_RADIOTAP_VHT_KNOWN_BEAMFORMED      0x0020
#define IEEE80211_RADIOTAP_VHT_KNOWN_BANDWIDTH       0x0040
#define IEEE80211_RADIOTAP_VHT_KNOWN_GROUP_ID        0x0080
#define IEEE80211_RADIOTAP_VHT_KNOWN_PARTIAL_AID     0x0100
/* IEEE80211 Radiotap VHT flags 字段定义 */
#define IEEE80211_RADIOTAP_VHT_FLAG_SGI              0x01  // 短保护间隔 (400ns)
#define IEEE80211_RADIOTAP_VHT_FLAG_STBC             0x02  // 空时分组码启用
#define IEEE80211_RADIOTAP_VHT_FLAG_TXOP_PS_NA       0x04  // TXOP节能模式
#define IEEE80211_RADIOTAP_VHT_FLAG_SGI_NSYM_MCS     0x08  // SGI NSYM MCS扩展
#define IEEE80211_RADIOTAP_VHT_FLAG_LDPC_EXTRA_OFDM  0x10  // LDPC额外OFDM符号
#define IEEE80211_RADIOTAP_VHT_FLAG_BEAMFORMED       0x20  // 波束成形启用
/* 文件: include/linux/ieee80211.h 或 net/ieee80211_radiotap.h */
#define IEEE80211_RADIOTAP_CODING_LDPC_USER0  0x01


static inline uint8_t find_first_set_ll(uint64_t x) {
    if (x == 0) return 0;
    uint8_t pos = 1;
    while ((x & 1) == 0) {
        x >>= 1;
        pos++;
    }
    return pos;
}

// 替代原宏
#define __bf_shf(x) (find_first_set_ll(x) - 1)
#define FIELD_PREP(_mask, _val) \
    (((uint32_t)(_val) << __bf_shf(_mask)) & (_mask))
/**
 * rwnx_rx_add_rtap_hdr - Add radiotap header to sk_buff
 *
 * @rwnx_hw: main driver data
 * @skb: skb received (will include the radiotap header)
 * @rxvect: Rx vector
 * @phy_info: Information regarding the phy
 * @hwvect: HW Info (NULL if vendor specific data is available)
 * @rtap_len: Length of the radiotap header
 * @vend_rtap_len: radiotap vendor length (0 if not present)
 * @vend_it_present: radiotap vendor present
 *
 * Builds a radiotap header and add it to @skb.
 */
static void rwnx_rx_add_rtap_hdr(struct rwnx_hw *rwnx_hw,
								 void *skb,
								 struct rx_vector_1 *rxvect,
								 struct phy_channel_info_desc *phy_info,
								 struct hw_vect *hwvect,
								 int rtap_len,
								 u8 vend_rtap_len,
								 u32 vend_it_present)
{
	struct ieee80211_radiotap_header *rtap;
	u8 *pos, rate_idx;
	__le32 *it_present;
	u32 it_present_val = 0;
	bool fec_coding = false;
	bool short_gi = false;
	bool stbc = false;
	bool aggregation = false;

	rtap = (struct ieee80211_radiotap_header *)skb;//(struct ieee80211_radiotap_header *)skb_push(skb, rtap_len);
	memset((u8 *) rtap, 0, rtap_len);

	rtap->it_version = 0;
	rtap->it_pad = 0;
	rtap->it_len = cpu_to_le16(rtap_len + vend_rtap_len);

	it_present = &rtap->it_present;

	// Check for multiple antennas
	if (generic_hweight32(rxvect->antenna_set) > 1) {
		// int chain;
		unsigned long chains = rxvect->antenna_set;

		//for_each_set_bit(chain, &chains, IEEE80211_MAX_CHAINS) {
        for (int e = 0; e < IEEE80211_MAX_CHAINS; e++) {
            if (chains & BIT(e)) {
                it_present_val |=
                    BIT(IEEE80211_RADIOTAP_EXT) |
                    BIT(IEEE80211_RADIOTAP_RADIOTAP_NAMESPACE);
                put_unaligned_le32(it_present_val, it_present);
                it_present++;
                it_present_val = BIT(IEEE80211_RADIOTAP_ANTENNA) |
                                BIT(IEEE80211_RADIOTAP_DBM_ANTSIGNAL);
            }
		}
	}

	// Check if vendor specific data is present
	if (vend_rtap_len) {
		it_present_val |= BIT(IEEE80211_RADIOTAP_VENDOR_NAMESPACE) |
						  BIT(IEEE80211_RADIOTAP_EXT);
		put_unaligned_le32(it_present_val, it_present);
		it_present++;
		it_present_val = vend_it_present;
	}

	put_unaligned_le32(it_present_val, it_present);
	pos = (void *)(it_present + 1);

	// IEEE80211_RADIOTAP_TSFT
	if (hwvect) {
		rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_TSFT);
		// padding
		while ((pos - (u8 *)rtap) & 7)
			*pos++ = 0;
		put_unaligned_le64((((u64)le32_to_cpu(hwvect->tsf_hi) << 32) +
							(u64)le32_to_cpu(hwvect->tsf_lo)), pos);
        // aic_dbg("TS %lx\r\n", (((u64)le32_to_cpu(hwvect->tsf_hi) << 32) +
		// 					(u64)le32_to_cpu(hwvect->tsf_lo)));
        // aic_dbg("tsf_hi %x, tsf_lo %x\r\n", hwvect->tsf_hi, hwvect->tsf_lo);
		pos += 8;
	}

	// IEEE80211_RADIOTAP_FLAGS
	rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_FLAGS);
	if (hwvect && (!hwvect->frm_successful_rx))
		*pos |= IEEE80211_RADIOTAP_F_BADFCS;
	if (!rxvect->pre_type
			&& (rxvect->format_mod <= FORMATMOD_NON_HT_DUP_OFDM))
		*pos |= IEEE80211_RADIOTAP_F_SHORTPRE;
	pos++;

	// IEEE80211_RADIOTAP_RATE
	// check for HT, VHT or HE frames
	if (rxvect->format_mod >= FORMATMOD_HE_SU) {
		rate_idx = rxvect->he.mcs;
		fec_coding = rxvect->he.fec;
		stbc = rxvect->he.stbc;
		aggregation = true;
		*pos = 0;
	} else if (rxvect->format_mod == FORMATMOD_VHT) {
		rate_idx = rxvect->vht.mcs;
		fec_coding = rxvect->vht.fec;
		short_gi = rxvect->vht.short_gi;
		stbc = rxvect->vht.stbc;
		aggregation = true;
		*pos = 0;
	} else if (rxvect->format_mod > FORMATMOD_NON_HT_DUP_OFDM) {
		rate_idx = rxvect->ht.mcs;
		fec_coding = rxvect->ht.fec;
		short_gi = rxvect->ht.short_gi;
		stbc = rxvect->ht.stbc;
		aggregation = rxvect->ht.aggregation;
		*pos = 0;
	} else {
        #define DIV_ROUND_UP(a, b) (((a) + (b) - 1) / (b))
        extern u16 tx_legrates_lut_rate[];
        extern struct rwnx_legrate legrates_lut[];
		// struct ieee80211_supported_band *band =
		// 		rwnx_hw->wiphy->bands[phy_info->phy_band];
		rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_RATE);
		rate_idx = legrates_lut[rxvect->leg_rate].idx;
		// if (phy_info->phy_band == NL80211_BAND_5GHZ)
		// 	rate_idx -= 4;  /* rwnx_ratetable_5ghz[0].hw_value == 4 */
		*pos = DIV_ROUND_UP(tx_legrates_lut_rate[rate_idx], 5);
        // aic_dbg("leg_rate %d %x\r\n", rxvect->leg_rate, *pos);
	}
	pos++;

	// IEEE80211_RADIOTAP_CHANNEL
	rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_CHANNEL);
	put_unaligned_le16(phy_info->phy_prim20_freq, pos);
	pos += 2;

	if (phy_info->phy_band == NL80211_BAND_5GHZ)
		put_unaligned_le16(IEEE80211_CHAN_OFDM | IEEE80211_CHAN_5GHZ, pos);
	else if (rxvect->format_mod > FORMATMOD_NON_HT_DUP_OFDM)
		put_unaligned_le16(IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ, pos);
	else
		put_unaligned_le16(IEEE80211_CHAN_CCK | IEEE80211_CHAN_2GHZ, pos);
	pos += 2;

	if (generic_hweight32(rxvect->antenna_set) == 1) {
		// IEEE80211_RADIOTAP_DBM_ANTSIGNAL
		rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL);
		*pos++ = rxvect->rssi1;

		// IEEE80211_RADIOTAP_ANTENNA
		rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_ANTENNA);
		*pos++ = rxvect->antenna_set;
	}

	// IEEE80211_RADIOTAP_LOCK_QUALITY is missing
	// IEEE80211_RADIOTAP_DB_ANTNOISE is missing

	// IEEE80211_RADIOTAP_RX_FLAGS
	rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_RX_FLAGS);
	// 2 byte alignment
	if ((pos - (u8 *)rtap) & 1)
		*pos++ = 0;
	put_unaligned_le16(0, pos);
	//Right now, we only support fcs error (no RX_FLAG_FAILED_PLCP_CRC)
	pos += 2;

    // aic_dbg("rtap->it_present %08x\r\n", rtap->it_present);
	// Check if HT
	if ((rxvect->format_mod == FORMATMOD_HT_MF)
			|| (rxvect->format_mod == FORMATMOD_HT_GF)) {
		rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_MCS);
		*pos++ = IEEE80211_RADIOTAP_MCS_HAVE_MCS |
				 IEEE80211_RADIOTAP_MCS_HAVE_GI |
				 IEEE80211_RADIOTAP_MCS_HAVE_BW;
		*pos = 0;
		if (short_gi)
			*pos |= IEEE80211_RADIOTAP_MCS_SGI;
		if (rxvect->ch_bw  == PHY_CHNL_BW_40)
			*pos |= IEEE80211_RADIOTAP_MCS_BW_40;
		if (rxvect->format_mod == FORMATMOD_HT_GF)
			*pos |= IEEE80211_RADIOTAP_MCS_FMT_GF;
		if (fec_coding)
			*pos |= IEEE80211_RADIOTAP_MCS_HAVE_FEC;//IEEE80211_RADIOTAP_MCS_FEC_LDPC;
		// #if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
		// *pos++ |= stbc << 5;
		// #else
		*pos++ |= stbc << IEEE80211_RADIOTAP_MCS_STBC_SHIFT;
		// #endif
		*pos++ = rate_idx;
	}

	// check for HT or VHT frames
	if (aggregation && hwvect) {
		// 4 byte alignment
		while ((pos - (u8 *)rtap) & 3)
			pos++;
		rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_AMPDU_STATUS);
		put_unaligned_le32(hwvect->ampdu_cnt, pos);
		pos += 4;
		put_unaligned_le32(0, pos);
		pos += 4;
	}

	// Check for VHT frames
	if (rxvect->format_mod == FORMATMOD_VHT) {
		u16 vht_details = IEEE80211_RADIOTAP_VHT_KNOWN_GI |
						  IEEE80211_RADIOTAP_VHT_KNOWN_BANDWIDTH;
		u8 vht_nss = rxvect->vht.nss + 1;

		rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_VHT);

		if ((rxvect->ch_bw == PHY_CHNL_BW_160)
				&& phy_info->phy_center2_freq)
			vht_details &= ~IEEE80211_RADIOTAP_VHT_KNOWN_BANDWIDTH;
		put_unaligned_le16(vht_details, pos);
		pos += 2;

		// flags
		if (short_gi)
			*pos |= IEEE80211_RADIOTAP_VHT_FLAG_SGI;
		if (stbc)
			*pos |= IEEE80211_RADIOTAP_VHT_FLAG_STBC;
		pos++;

		// bandwidth
		if (rxvect->ch_bw == PHY_CHNL_BW_40)
			*pos++ = 1;
		if (rxvect->ch_bw == PHY_CHNL_BW_80)
			*pos++ = 4;
		else if ((rxvect->ch_bw == PHY_CHNL_BW_160)
				&& phy_info->phy_center2_freq)
			*pos++ = 0; //80P80
		else if  (rxvect->ch_bw == PHY_CHNL_BW_160)
			*pos++ = 11;
		else // 20 MHz
			*pos++ = 0;

		// MCS/NSS
		*pos = (rate_idx << 4) | vht_nss;
		pos += 4;
		if (fec_coding)
			// #if LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0)
			// *pos |= 0x01;
			// #else
			*pos |= IEEE80211_RADIOTAP_CODING_LDPC_USER0;
			// #endif
		pos++;
		// group ID
		pos++;
		// partial_aid
		pos += 2;
	}

	// Check for HE frames
	if (rxvect->format_mod == FORMATMOD_HE_SU) {
		struct ieee80211_radiotap_he he;
		#define HE_PREP(f, val) cpu_to_le16(FIELD_PREP(IEEE80211_RADIOTAP_HE_##f, val))
		#define D1_KNOWN(f) cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_##f##_KNOWN)
		#define D2_KNOWN(f) cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA2_##f##_KNOWN)

		he.data1 = D1_KNOWN(DATA_MCS) | D1_KNOWN(BSS_COLOR) | D1_KNOWN(BEAM_CHANGE) |
				   D1_KNOWN(UL_DL) | D1_KNOWN(CODING) |  D1_KNOWN(STBC) |
				   D1_KNOWN(BW_RU_ALLOC) | D1_KNOWN(DOPPLER) | D1_KNOWN(DATA_DCM);
		he.data2 = D2_KNOWN(GI) | D2_KNOWN(TXBF);

		if (stbc) {
			he.data6 |= HE_PREP(DATA6_NSTS, 2);
			he.data3 |= HE_PREP(DATA3_STBC, 1);
		} else {
			he.data6 |= HE_PREP(DATA6_NSTS, rxvect->he.nss);
		}

		he.data3 |= HE_PREP(DATA3_BSS_COLOR, rxvect->he.bss_color);
		he.data3 |= HE_PREP(DATA3_BEAM_CHANGE, rxvect->he.beam_change);
		he.data3 |= HE_PREP(DATA3_UL_DL, rxvect->he.uplink_flag);
		he.data3 |= HE_PREP(DATA3_BSS_COLOR, rxvect->he.bss_color);
		he.data3 |= HE_PREP(DATA3_DATA_MCS, rxvect->he.mcs);
		he.data3 |= HE_PREP(DATA3_DATA_DCM, rxvect->he.dcm);
		he.data3 |= HE_PREP(DATA3_CODING, rxvect->he.fec);

		he.data5 |= HE_PREP(DATA5_GI, rxvect->he.gi_type);
		he.data5 |= HE_PREP(DATA5_TXBF, rxvect->he.beamformed);
		he.data5 |= HE_PREP(DATA5_LTF_SIZE, rxvect->he.he_ltf_type + 1);

		switch (rxvect->ch_bw) {
		case PHY_CHNL_BW_20:
			he.data5 |= HE_PREP(DATA5_DATA_BW_RU_ALLOC,
						IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_20MHZ);
			break;
		case PHY_CHNL_BW_40:
			he.data5 |= HE_PREP(DATA5_DATA_BW_RU_ALLOC,
						IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_40MHZ);
			break;
		case PHY_CHNL_BW_80:
			he.data5 |= HE_PREP(DATA5_DATA_BW_RU_ALLOC,
						IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_80MHZ);
			break;
		case PHY_CHNL_BW_160:
			he.data5 |= HE_PREP(DATA5_DATA_BW_RU_ALLOC,
						IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_160MHZ);
			break;
		default:
			aic_dbg("Invalid SU BW %d\n", rxvect->ch_bw);
		}

		he.data6 |= HE_PREP(DATA6_DOPPLER, rxvect->he.doppler);

		/* ensure 2 byte alignment */
		while ((pos - (u8 *)rtap) & 1)
			pos++;
		rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_HE);
		memcpy(pos, &he, sizeof(he));
		pos += sizeof(he);
	}

	// Rx Chains
	if (generic_hweight32(rxvect->antenna_set) > 1) {
		// int chain;
		unsigned long chains = rxvect->antenna_set;
		u8 rssis[4] = {rxvect->rssi1, rxvect->rssi1, rxvect->rssi1, rxvect->rssi1};

		//for_each_set_bit(chain, &chains, IEEE80211_MAX_CHAINS) {
        for (int e = 0; e < IEEE80211_MAX_CHAINS; e++) {
            if (chains & BIT(e)) {
                *pos++ = rssis[e];
                *pos++ = e;
            }
		}
	}
}
#endif /* CONFIG_MONITOR_RADIO_HEADER * /

/**
 ****************************************************************************************
 * @brief Call registered monitor callback for the received buffer.
 *
 * Extract useful information from RX buffer and call the monitor callback with this as
 * parameter. Returns immediately if no monitor callback is registered.
 *
 * @param[in] buf  Pointer to the RX buffer
 * @param[in] uf   Whether frame has been decoded or not by the modem.
 *                 (false: decoded frame, true: unsupported frame)
 ****************************************************************************************
 */
void fhost_rx_monitor_cb(void *buf, bool uf)
{
    struct fhost_frame_info info;
    //int8_t rx_rssi[2];

    if (fhost_rx_env.monitor_cb == NULL)
        return;

    #if NX_UF_EN
    if (uf) {
        uint8_t rx_format;
        struct rx_vector_desc *rx_vector = (struct rx_vector_desc *)buf;
        struct uf_rx_vector_1 *uf_rx_v1= (struct uf_rx_vector_1 *)(&rx_vector->rx_vec_1);
        info.payload = NULL;
        rx_format = uf_rx_v1->format_mod;
        if(2 == rx_format) { // HT
            info.length = uf_rx_v1->ht.length - 8 * uf_rx_v1->ht.aggregation;
        } else if(4 == rx_format) { // VHT
            info.length = hal_desc_get_vht_length(&rx_vector->rx_vec_1); //psdu length
        }
        info.rssi = uf_rx_v1->rssi1;
        info.freq = PHY_INFO_CHAN(rx_vector->phy_info);
        // uint16_t radio_hdr_len = rwnx_rx_rtap_hdrlen((struct rx_vector_1 *) (&rx_buf->info.vect.recvec1a), true);
        // info.radiotap_hdr = (uint8_t *)rtos_malloc(radio_hdr_len);

        // if (info.radiotap_hdr) {
        //     extern struct rwnx_hw *g_rwnx_hw;

        //     rwnx_rx_add_rtap_hdr(g_rwnx_hw, info.radiotap_hdr, &rx_buf->info.vect.recvec1a,
		// 				 &rx_buf->info.phy_info, &rx_buf->info.vect,
		// 				 radio_hdr_len, 0, 0);
        // }
        // info.radio_hdr_len = radio_hdr_len;
    } else
    #endif /* NX_UF_EN */
    {
        struct fhost_rx_buf_tag *rx_buf = (struct fhost_rx_buf_tag *)buf;
        info.payload = (uint8_t *)rx_buf->payload;
        info.length = rx_buf->info.vect.frmlen;
        info.freq = PHY_INFO_CHAN(rx_buf->info.phy_info);
        #ifdef CONFIG_MONITOR_RADIO_HEADER
        uint16_t radio_hdr_len = rwnx_rx_rtap_hdrlen((struct rx_vector_1 *) (&rx_buf->info.vect.recvec1a), false);
        info.radiotap_hdr = (uint8_t *)rtos_malloc(radio_hdr_len);

        if (info.radiotap_hdr) {
            extern struct rwnx_hw *g_rwnx_hw;

            rwnx_rx_add_rtap_hdr(g_rwnx_hw, info.radiotap_hdr, &rx_buf->info.vect.recvec1a,
						 &rx_buf->info.phy_info, &rx_buf->info.vect,
						 radio_hdr_len, 0, 0);
        }
        info.radio_hdr_len = radio_hdr_len;
        #endif /* CONFIG_MONITOR_RADIO_HEADER */
        //info.rssi = hal_desc_get_rssi(&buf->info.vect.rx_vec_1, rx_rssi);
    }

    fhost_rx_env.monitor_cb(&info, fhost_rx_env.monitor_cb_arg);
}

void fhost_rx_set_mgmt_cb(cb_fhost_rx cb, void *arg)
{
    fhost_rx_env.mgmt_cb = cb;
    fhost_rx_env.mgmt_cb_arg = arg;
}

void fhost_rx_set_monitor_cb(cb_fhost_rx cb, void *arg)
{
    fhost_rx_env.monitor_cb = cb;
    fhost_rx_env.monitor_cb_arg = arg;
}
extern net_buf_tx_t *get_net_tx_buf_pkt_hdr(void);
int fhost_rx_data_resend(net_if_t *net_if, struct fhost_rx_buf_tag *buf,
                         struct mac_addr *da, struct mac_addr *sa, uint8_t machdr_len)
{
    #ifdef CONFIG_TX_NOCOPY
    struct pbuf* p;
    uint8_t *frame = (uint8_t *)buf->payload;
    uint32_t statinfo = buf->info.vect.statinfo;
    struct mac_hdr *machdr_ptr = (struct mac_hdr *)frame;
    struct mac_eth_hdr *eth_hdr;
    uint8_t payl_offset = 0;
    net_buf_tx_t *sent_pkt;
    payl_offset = machdr_len + sizeof(struct llc_snap) - sizeof(struct mac_eth_hdr);
    // net_buf = net_buf_tx_alloc(frame + payl_offset, buf->info.vect.frmlen - payl_offset);

    p = (uint8_t *)pbuf_alloc(PBUF_RAW, (buf->info.vect.frmlen - payl_offset), PBUF_POOL);
    if (p == NULL) {
        aic_dbg("net buf alloc fail\r\n");
        return -1;
    }
    eth_hdr = (struct mac_eth_hdr *)(p->payload);
    MAC_ADDR_CPY(&eth_hdr->da, da);
    MAC_ADDR_CPY(&eth_hdr->sa, sa);
    memcpy((p->payload + sizeof(*eth_hdr)), frame + payl_offset, buf->info.vect.frmlen - payl_offset);

    sent_pkt = get_net_tx_buf_pkt_hdr();
    if (!sent_pkt) {
        aic_dbg("%s get buf fail\n", __func__);
        return -1;
    }
    sent_pkt->data_ptr = p;
    sent_pkt->data_len = p->tot_len;
    sent_pkt->net_id   = 0;
    sent_pkt->pkt_type = 0;
    //eth_hdr->type = eth_type; // already exist
    //AIC_LOG_PRINTF("%s pkt %x %x, t:%x\n", __func__, net_buf, net_buf->data_ptr, net_buf->pkt_type);
    fhost_tx_start(net_if, sent_pkt, NULL, NULL);
    #else
    uint8_t *frame = (uint8_t *)buf->payload;
    uint32_t statinfo = buf->info.vect.statinfo;
    struct mac_hdr *machdr_ptr = (struct mac_hdr *)frame;
    struct mac_eth_hdr *eth_hdr;
    uint8_t payl_offset = 0;
    net_buf_tx_t *net_buf;
    payl_offset = machdr_len + sizeof(struct llc_snap) - sizeof(struct mac_eth_hdr);
    net_buf = net_buf_tx_alloc(frame + payl_offset, buf->info.vect.frmlen - payl_offset);
    if (net_buf == NULL) {
        aic_dbg("net buf alloc fail\r\n");
        return -1;
    }
    // fill ether header after alloc
    eth_hdr = (struct mac_eth_hdr *)(net_buf->data_ptr);
    MAC_ADDR_CPY(&eth_hdr->da, da);
    MAC_ADDR_CPY(&eth_hdr->sa, sa);
    //eth_hdr->type = eth_type; // already exist
    //AIC_LOG_PRINTF("%s pkt %x %x, t:%x\n", __func__, net_buf, net_buf->data_ptr, net_buf->pkt_type);
    fhost_tx_start(net_if, net_buf, NULL, NULL);
    #endif
    return 0;
}

#if (FHOST_RX_SW_VER == 3)
static bool aicwf_another_ptk(uint8_t *data, uint32_t len)
{
    uint16_t aggr_len = 0;
    if (data == NULL || len == 0) {
        return false;
    }
    aggr_len = (*data | (*(data + 1) << 8));
    if (aggr_len == 0) {
        return false;
    }
    if (aggr_len > len) {
        AIC_LOG_PRINTF("%s error:%d/%d\n", __func__, aggr_len, len);
        return false;
    }
    return true;
}

#ifdef CONFIG_SDIO_SUPPORT
void fhost_rx_task(void *arg)
{
    AIC_LOG_PRINTF("fhost_rx_task\n");
    while (1) {
        struct sdio_buf_node_s *node = NULL;
        int ret = rtos_semaphore_wait(fhost_rx_env.rxq_trigg, -1);
        //AIC_LOG_PRINTF("aft rxq sema\n");

        if (ret < 0) {
            AIC_LOG_PRINTF("wait fhost rxq trigg fail: ret=%d\n", ret);
        }
        node = fhost_rxframe_dequeue();
        if (node) {
            uint8_t *buf_raw;
            uint8_t *data = node->buf;
            uint32_t len = node->buf_len;
            if (data == NULL) {
                AIC_LOG_PRINTF("err: rx data null: node=%p\n", node);
            }
            while (aicwf_another_ptk(data, len)) {
                uint16_t pkt_len = (*data | (*(data + 1) << 8));
                uint16_t aggr_len;
                if((data[2] & SDIO_TYPE_CFG) != SDIO_TYPE_CFG) { // type : data
                    struct fhost_rx_buf_tag *buf = (struct fhost_rx_buf_tag *)data;
                    //AIC_LOG_PRINTF("[task] rx data: %p, %p, %p, %d\n", rx_frame_in_process, rx_in_process_buf, buf, pkt_len);
                    //rwnx_data_dump("buffer_rx", buffer_rx, (pkt_len < 32) ? 32 : pkt_len);
                    #if (AICWF_RX_REORDER)
                    rwnx_rxdataind_aicwf(buf);
                    #else
                    #ifdef CONFIG_RX_NOCOPY
                    fhost_rx_buf_forward(buf, NULL);
                    #else
                    fhost_rx_buf_forward(buf);
                    #endif /* CONFIG_RX_NOCOPY */
                    #endif
                    aggr_len = sizeof(struct rx_info) + pkt_len;
                    aggr_len = (aggr_len + (RX_ALIGNMENT - 1)) & ~(RX_ALIGNMENT - 1);
                } else {
                    uint8_t *msg = data;
                    uint8_t type = *(msg + 2) & 0x7f;
                    if (type == SDIO_TYPE_CFG_CMD_RSP) {
                        //AIC_LOG_PRINTF("[task] rx cmd rsp\n");
                        struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)arg;
                        rwnx_rx_handle_msg(rwnx_hw, (struct e2a_msg *)(msg + 4));
                    } else if (type == SDIO_TYPE_CFG_DATA_CFM) {
                        fhost_tx_cfm_push((u32 *)(msg + 4));
                        //AIC_LOG_PRINTF("[task] rx data cfm\n");
                    } else {
                        AIC_LOG_PRINTF("unsupported type:%x\n", type);
                    }
                    aggr_len = (pkt_len + 4 + (RX_ALIGNMENT - 1)) & ~(RX_ALIGNMENT - 1);
                }
                data += aggr_len;
                len -= aggr_len;
                if ((uint32_t)(data - node->buf) > node->buf_len) {
                    AIC_LOG_PRINTF("%s len error:%d/%d\n", __func__, (uint32_t)(data - node->buf), node->buf_len);
                    break;
                }
            }
            //aic_dbg("deq:%p,%d, node:%p\n",node->buf,node->buf_len, node);
            sdio_buf_free(node);
			#ifdef CONFIG_SDIO_BUS_PWRCTRL
            rtos_mutex_lock(sdio_dev.rx_sema, -1);
            sdio_dev.rx_cnt--;
            //AIC_LOG_PRINTF("%s, rx_cnt:%d", __func__, sdio_dev.rx_cnt);
            rtos_mutex_unlock(sdio_dev.rx_sema);
            #endif /* CONFIG_SDIO_BUS_PWRCTRL */
        } else {
            AIC_LOG_PRINTF("rxq triggered but queue is empty\n");
        }

#ifdef CONFIG_SDIO_BUS_PWRCTRL
        //AIC_LOG_PRINTF("%s", __func__);
        aicwf_sdio_bus_pwr_stctl(&sdio_dev, SDIO_BUS_ACTIVE);
#endif /* CONFIG_SDIO_BUS_PWRCTRL */
    }
}
#endif

#ifdef CONFIG_USB_SUPPORT
void fhost_rx_task(void *arg)
{
    AIC_LOG_PRINTF("fhost_rx_task\n");
    #ifdef CONFIG_FHOST_RX_TASK_TS
    uint32_t start_time = 0;
    uint32_t end_time = 0;
    static volatile uint8_t rx_count = 0;
    #endif
#ifdef CONFIG_USB_MSG_IN_EP
    static volatile uint8_t ep_type = 0; // 0: data ep, 1: msg ep
#endif
    while (1) {
        struct aicwf_usb_buf *node = NULL;
        int ret = rtos_semaphore_wait(fhost_rx_env.rxq_trigg, -1);
        #ifdef PLATFORM_SUNPLUS_ECOS
        if (fhost_rx_task_exit_flag) {
            break;
        }
        #endif
            #ifdef CONFIG_FHOST_RX_TASK_TS
            rx_count++;
            start_time = (uint32_t)rtos_now(0);
            aic_dbg("rxin:%u/%u\n", start_time, rx_count);
            #endif
            if (ret < 0) {
                AIC_LOG_PRINTF("wait fhost rxq trigg fail: ret=%d\n", ret);
            }
            node = fhost_rxframe_dequeue();
            if (node) {
                #ifdef CONFIG_FHOST_RX_ASYNC
                rx_frame_in_process = node;
                rx_frame_to_async = false;
                #endif
                struct aic_sk_buff *skb = node->skb;
                if (skb == NULL) {
                    AIC_LOG_PRINTF("err: rx skb null: node=%p\n", node);
                }
                uint8_t *data = skb->data;
                uint32_t len = skb->len;
                if (data == NULL) {
                    AIC_LOG_PRINTF("err: rx data null: node=%p\n", node);
                }
                if ((data[2] & USB_TYPE_CFG) != USB_TYPE_CFG) { // type : data
                    #ifdef CONFIG_USB_MSG_IN_EP
                    ep_type = 0;
                    #endif
                    struct fhost_rx_buf_tag *buf = (struct fhost_rx_buf_tag *)data;
                    //AIC_LOG_PRINTF("[task] rx data: %d, %d\n", data_len, pkt_len);
                    //rwnx_data_dump("buffer_rx", buffer_rx, (pkt_len < 32) ? 32 : pkt_len);
                    #if (AICWF_RX_REORDER)
                    rwnx_rxdataind_aicwf(buf);
                    #else
                    fhost_rx_buf_forward(buf);
                    #endif
                } else {
                    uint8_t *msg = data;
                    uint8_t type = *(msg + 2) & 0x7f;
                    if (type == USB_TYPE_CFG_CMD_RSP) {
                        #ifdef CONFIG_USB_MSG_IN_EP
                        ep_type = 1;
                        #endif
                        //AIC_LOG_PRINTF("[task] rx cmd rsp\n");
                        struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)arg;
                        rwnx_rx_handle_msg(rwnx_hw, (struct e2a_msg *)(msg + 4));
                    } else if (type == USB_TYPE_CFG_DATA_CFM) {
                        #ifdef CONFIG_USB_MSG_IN_EP
                        ep_type = 0;
                        #endif
                        fhost_tx_cfm_push((u32 *)(msg + 4));
                        AIC_LOG_PRINTF("[task] rx data cfm\n");
                    } else {
                        AIC_LOG_PRINTF("unsupported type:%x\n", type);
                    }
                }
#ifdef CONFIG_USB_MSG_IN_EP
                if (g_aic_usb_dev->chipid != PRODUCT_ID_AIC8801 &&
                    g_aic_usb_dev->chipid != PRODUCT_ID_AIC8800D81) {
#ifdef CONFIG_FHOST_RX_ASYNC
                    if (!rx_frame_to_async) {
                        ep_type? aicwf_usb_msg_rx_buf_put(g_aic_usb_dev, node) : aicwf_usb_rx_buf_put(g_aic_usb_dev, node);
                    }
                    rx_frame_in_process = NULL;
                    rx_frame_to_async = false;
#else
                    ep_type? aicwf_usb_msg_rx_buf_put(g_aic_usb_dev, node) : aicwf_usb_rx_buf_put(g_aic_usb_dev, node);
#endif
                    ep_type? aicwf_usb_msg_rx_submit_all_urb(g_aic_usb_dev) : aicwf_usb_rx_submit_all_urb(g_aic_usb_dev);
                }
#else
#ifdef CONFIG_FHOST_RX_ASYNC
                if (!rx_frame_to_async) {
                    aicwf_usb_rx_buf_put(g_aic_usb_dev, node);
                }
                rx_frame_in_process = NULL;
                rx_frame_to_async = false;
#else
                aicwf_usb_rx_buf_put(g_aic_usb_dev, node);
#endif
                aicwf_usb_rx_submit_all_urb(g_aic_usb_dev);
#endif
            } else {
                AIC_LOG_PRINTF("rxq triggered but queue is empty\n");
            }
#ifdef CONFIG_FHOST_RX_TASK_TS
            AIC_LOG_PRINTF("rxout:%u/%u\n", end_time, rx_count);
#endif
    }
exit:
    AIC_LOG_PRINTF("Exit fhost_rx_task\r\n");
    #ifdef PLATFORM_GX_ECOS
    rtos_semaphore_signal(fhost_rx_task_exit_sem, false);
    #endif
}
#endif

void fhost_rx_init(struct rwnx_hw *rwnx_hw)
{
    int idx, ret;

    AIC_LOG_PRINTF("fhost_rx_init\n");
    ret = rtos_mutex_create(&fhost_rx_env.rxq.mutex, "fhost_rx_env.rxq.mutex");
    if (ret) {
        aic_dbg("fhost rxq mutex create fail: %d\n", ret);
        return;
    }
    co_list_init(&fhost_rx_env.rxq.list);
    #ifdef CONFIG_SDIO_SUPPORT
    ret = rtos_semaphore_create(&fhost_rx_env.rxq_trigg, "fhost_rx_env.rxq_trigg", SDIO_RX_BUF_COUNT, 0);
    #endif
    #ifdef CONFIG_USB_SUPPORT
    ret = rtos_semaphore_create(&fhost_rx_env.rxq_trigg, "fhost_rx_env.rxq_trigg", AICWF_USB_RX_URBS, 0);
    #endif
    if (ret) {
        aic_dbg("fhost rxq_trigg create fail: %d\n", ret);
        return;
    }
    if (rtos_semaphore_create(&fhost_rx_task_exit_sem, "fhost_rx_task_exit_sem", 0x7FFFFFFF, 0)) {
        AIC_LOG_PRINTF("fhost_rx_task_exit_sem create fail\n");
        return;
    }
    #ifdef CONFIG_FHOST_RX_ASYNC
    int i = 0;
    ret = rtos_mutex_create(&fhost_rx_env.rxq_async_free.mutex, "fhost_rx_env.rxq_async_free.mutex");
    if (ret) {
        aic_dbg("fhost rxq_async_free mutex create fail: %d\n", ret);
        return;
    }
    co_list_init(&fhost_rx_env.rxq_async_free.list);
    memset(&rx_async_desc_pool, 0, sizeof(rx_async_desc_pool));
    for (i = 0; i < RX_ASYNC_DESC_CNT ; i++) {
        co_list_push_back(&fhost_rx_env.rxq_async_free.list, &rx_async_desc_pool[i].hdr);
    }

    ret = rtos_mutex_create(&fhost_rx_env.rxq_async_post.mutex, "fhost_rx_env.rxq_async_post.mutex");
    if (ret) {
        aic_dbg("fhost rxq_async_post mutex create fail: %d\n", ret);
        return;
    }
    co_list_init(&fhost_rx_env.rxq_async_post.list);
    #endif

    ret = rtos_task_create(fhost_rx_task, "fhost_rx_task", FHOST_RX_TASK,
                           fhost_rx_stack_size, (void *)rwnx_hw, fhost_rx_priority,
                           &fhost_rx_task_hdl);
    if (ret || (fhost_rx_task_hdl == NULL)) {
        AIC_LOG_PRINTF("fhost wlanrx task create fail,%d\n",ret);
        return;
    }
    #if (AICWF_RX_REORDER)
    rwnx_reord_init();
    #endif
}

void fhost_rx_deinit(struct rwnx_hw *rwnx_hw)
{
    if (fhost_rx_task_hdl) {
        fhost_rx_task_exit_flag = true;
        rtos_semaphore_signal(fhost_rx_env.rxq_trigg, false);
        rtos_semaphore_wait(fhost_rx_task_exit_sem, -1);
        fhost_rx_task_exit_flag = false;
        rtos_task_delete(fhost_rx_task_hdl);
        fhost_rx_task_hdl = NULL;
    }

    #ifdef CONFIG_FHOST_RX_ASYNC
    struct fhost_rx_async_desc_tag *async_desc = NULL;

    do {
        async_desc = fhost_rx_async_post_dequeue();
        if (async_desc == NULL) {
            break;
        }
        if (async_desc->from_heap) {
            rtos_free(async_desc);
        }
    } while (1);
    rtos_mutex_delete(fhost_rx_env.rxq_async_post.mutex);

    rtos_mutex_lock(fhost_rx_env.rxq_async_free.mutex, -1);
    struct fhost_rx_async_desc_tag *desc = NULL;
    do {
        desc = (struct fhost_rx_async_desc_tag *)co_list_pop_front(&fhost_rx_env.rxq_async_free.list);
        if (desc == NULL) {
            break;
        }
        if (desc->from_heap) {
            rtos_free(desc);
        }
    } while (1);
    rtos_mutex_unlock(fhost_rx_env.rxq_async_free.mutex);
    rtos_mutex_delete(fhost_rx_env.rxq_async_free.mutex);
    #endif
#ifdef CONFIG_USB_SUPPORT
    struct aicwf_usb_buf *usb_buf = NULL;
#else
	struct sdio_buf_node_s *node;
#endif
    do {
#ifdef CONFIG_USB_SUPPORT
        usb_buf = fhost_rxframe_dequeue();
        if (usb_buf == NULL) {
            break;
        }
        if (usb_buf->urb) {
            usb_free_urb(usb_buf->urb);
            usb_buf->urb = NULL;
        } else {
            AIC_LOG_PRINTF("%s urb null\n", __func__);
        }
        if (usb_buf->skb) {
            aic_dev_kfree_skb_any(usb_buf->skb);
            usb_buf->skb = NULL;
        } else {
            AIC_LOG_PRINTF("%s skb null\n", __func__);
        }
#else
        node = fhost_rxframe_dequeue();
        if (node) {
            sdio_buf_free(node);
            node = NULL;
        } else
            break;
#endif
    } while (1);
    if (fhost_rx_task_exit_sem) {
        rtos_semaphore_delete(fhost_rx_task_exit_sem);
    }
    if (fhost_rx_env.rxq_trigg) {
        rtos_semaphore_delete(fhost_rx_env.rxq_trigg);
    }
    rtos_mutex_delete(fhost_rx_env.rxq.mutex);
    #if (AICWF_RX_REORDER)
    rwnx_reord_deinit();
    #endif
}

#ifdef CONFIG_SDIO_SUPPORT
struct sdio_buf_node_s * fhost_rxframe_dequeue(void)
{
    struct sdio_buf_node_s *node;
    //rtos_mutex_lock(fhost_rx_env.rxq.mutex, -1);
    rtos_entercritical();
    node = (struct sdio_buf_node_s *)co_list_pop_front(&fhost_rx_env.rxq.list);
    //printf("de: %d, time: %d\n", co_list_cnt(&fhost_rx_env.rxq.list), rtos_now(false));
    ///rtos_mutex_unlock(fhost_rx_env.rxq.mutex);
    rtos_exitcritical();
    return node;
}

void fhost_rxframe_enqueue(struct sdio_buf_node_s *node)
{
    //rtos_mutex_lock(fhost_rx_env.rxq.mutex, -1);
    rtos_entercritical();
    co_list_push_back(&fhost_rx_env.rxq.list, &node->hdr);
    //printf("en: %d, time: %d\n", co_list_cnt(&fhost_rx_env.rxq.list), rtos_now(false));
    //rtos_mutex_unlock(fhost_rx_env.rxq.mutex);
    rtos_exitcritical();
}
#endif

#ifdef CONFIG_USB_SUPPORT
struct aicwf_usb_buf * fhost_rxframe_dequeue(void)
{
    struct aicwf_usb_buf *node;
    //rtos_mutex_lock(fhost_rx_env.rxq.mutex, -1);
    rtos_entercritical();
    node = (struct aicwf_usb_buf *)co_list_pop_front(&fhost_rx_env.rxq.list);
    //printf("de: %d, time: %d\n", co_list_cnt(&fhost_rx_env.rxq.list), rtos_now(false));
    //rtos_mutex_unlock(fhost_rx_env.rxq.mutex);
    rtos_exitcritical();
    return node;
}

void fhost_rxframe_enqueue(struct aicwf_usb_buf *node)
{
    //rtos_mutex_lock(fhost_rx_env.rxq.mutex, -1);
    rtos_entercritical();
    co_list_push_back(&fhost_rx_env.rxq.list, &node->hdr);
    //printf("en: %d, time: %d\n", co_list_cnt(&fhost_rx_env.rxq.list), rtos_now(false));
    //rtos_mutex_unlock(fhost_rx_env.rxq.mutex);
    rtos_exitcritical();
}
#endif

#ifdef CONFIG_FHOST_RX_ASYNC
struct fhost_rx_async_desc_tag * fhost_rx_async_desc_alloc(void)
{
    bool from_heap = false;
    struct fhost_rx_async_desc_tag *desc;
    rtos_mutex_lock(fhost_rx_env.rxq_async_free.mutex, -1);
    //rtos_entercritical();
    desc = (struct fhost_rx_async_desc_tag *)co_list_pop_front(&fhost_rx_env.rxq_async_free.list);
    rtos_mutex_unlock(fhost_rx_env.rxq_async_free.mutex);
    //rtos_exitcritical();

    if (desc == NULL) {
        desc = rtos_malloc(sizeof(struct fhost_rx_async_desc_tag));
        if (desc) {
            from_heap = true;
        }
    }

    if (desc) {
        memset(desc, 0, sizeof(struct fhost_rx_async_desc_tag));
        desc->from_heap = from_heap;
    }

    return desc;
}

void fhost_rx_async_desc_free(struct fhost_rx_async_desc_tag *desc)
{
    if (!desc->from_heap) {
        rtos_mutex_lock(fhost_rx_env.rxq_async_free.mutex, -1);
        //rtos_entercritical();
        co_list_push_back(&fhost_rx_env.rxq_async_free.list, &desc->hdr);
        rtos_mutex_unlock(fhost_rx_env.rxq_async_free.mutex);
        //rtos_exitcritical();
    } else {
        rtos_free(desc);
    }
}

struct fhost_rx_async_desc_tag * fhost_rx_async_post_dequeue(void)
{
    struct fhost_rx_async_desc_tag *desc;
    rtos_mutex_lock(fhost_rx_env.rxq_async_post.mutex, -1);
    //rtos_entercritical();
    desc = (struct fhost_rx_async_desc_tag *)co_list_pop_front(&fhost_rx_env.rxq_async_post.list);
    rtos_mutex_unlock(fhost_rx_env.rxq_async_post.mutex);
    //rtos_exitcritical();

    return desc;
}

void fhost_rx_async_post_enqueue(struct fhost_rx_async_desc_tag *desc)
{
    rtos_mutex_lock(fhost_rx_env.rxq_async_post.mutex, -1);
    //rtos_entercritical();
    co_list_push_back(&fhost_rx_env.rxq_async_post.list, &desc->hdr);
    rtos_mutex_unlock(fhost_rx_env.rxq_async_post.mutex);
    //rtos_exitcritical();
}

uint32_t fhost_rx_async_post_cnt(bool lock)
{
    uint32_t cnt;

    if (lock) {
        rtos_mutex_lock(fhost_rx_env.rxq_async_post.mutex, -1);
        //rtos_entercritical();
    }

    cnt = co_list_cnt(&fhost_rx_env.rxq_async_post.list);

    if (lock) {
        rtos_mutex_unlock(fhost_rx_env.rxq_async_post.mutex);
        //rtos_exitcritical();
    }

    return cnt;
}

void *fhost_rx_frame_in_process_get(void)
{
    return (void *)rx_frame_in_process;
}

#ifdef CONFIG_REORD_FORWARD_LIST
void *fhost_rx_frame_match(struct fhost_rx_buf_tag *buf)
{
    void *frame = NULL;
#ifdef CONFIG_USB_SUPPORT
    if (rx_frame_in_process) {
        struct fhost_rx_buf_tag *buf_in_process = (struct fhost_rx_buf_tag *)rx_frame_in_process->skb->data;
        if (buf_in_process == buf) {
            frame = rx_frame_in_process;
        }
    }
#elif CONFIG_SDIO_SUPPORT
    if (rx_in_process_buf) {
        if (buf == rx_in_process_buf) {
            frame = rx_in_process_buf;
        }
    }
#endif
    return frame;
}
#else
bool fhost_rx_frame_match(struct fhost_rx_buf_tag *buf, struct fhost_rx_async_desc_tag *desc)
{
    bool match = false;

    if (rx_frame_in_process) {
        #ifdef CONFIG_USB_SUPPORT
        struct fhost_rx_buf_tag *buf_in_process = (struct fhost_rx_buf_tag *)rx_frame_in_process->skb->data;
        if (buf_in_process == buf) {
            match = true;
            rx_frame_to_async = true;
            desc->frame_ptr = (void *)rx_frame_in_process;
            desc->frame_type = RX_ASYNC_RX_FRAME;
        }
        #elif CONFIG_SDIO_SUPPORT
        struct fhost_rx_buf_tag *buf_in_process = (struct fhost_rx_buf_tag *)rx_frame_in_process->buf;
        if (buf_in_process == buf) {
            match = true;
            rx_frame_to_async = true;
            desc->frame_ptr = (void *)rx_frame_in_process;
            desc->frame_type = RX_ASYNC_RX_FRAME;
        }
        #endif
    }

    return match;
}
#endif
#endif

#endif
