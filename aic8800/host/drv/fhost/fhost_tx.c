/**
 ****************************************************************************************
 *
 * @file fhost_tx.c
 *
 * @brief Implementation of the fully hosted TX task.
 *
 * Copyright (C) RivieraWaves 2017-2019
 *
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @addtogroup FHOST_TX
 * @{
 ****************************************************************************************
 */
/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "fhost_tx.h"
#include "fhost.h"

#include "mac.h"
#include "rwnx_msg_tx.h"
#include "rwnx_utils.h"
#include "co_math.h"
#include "co_utils.h"
#include "co_endian.h"
#include "wlan_if.h"
//#include "log.h"
#ifdef CONFIG_USB_SUPPORT
#include "usb_port.h"
#include "../../platform/sp_usb/porting.h"
#elif CONFIG_SDIO_SUPPORT
#include "sdio_port.h"
#include "sdio_def.h"
#include "../../platform/aw_sdio/porting.h"
extern struct aic_sdio_dev sdio_dev;
#endif

#define FHOST_TX_MSG_USE_QUEUE 1



/// Type of packet to send
enum fhost_tx_buf_type
{
    /// Packet with an ethernet header (from the network stack)
    IEEE802_3,
    /// Packet with a wifi header
    IEEE802_11,
};

/*
 * GLOBAL VARIABLES
 ****************************************************************************************
 */
struct fhost_tx_env_tag fhost_tx_env = {NULL,};
#ifdef CFG_SOFTAP
uint16_t ap_mgmt_seqnbr = 0;
#endif /* CFG_SOFTAP */
#ifdef CONFIG_TX_NOCOPY
#define TXBUF_CNT 500
#else
#define TXBUF_CNT 100
#endif
/// List element for the free TX desc
struct co_list tx_desc_free_list = {NULL,};
rtos_mutex tx_desc_free_mutex = NULL;
uint32_t tx_desc_free_cnt = 0;
/// List element for the TX desc that needs confirmation
struct co_list tx_desc_cfm_list = {NULL,};
rtos_mutex tx_desc_cfm_mutex = NULL;
uint32_t tx_desc_cfm_cnt = 0;
struct fhost_tx_desc_tag tx_desc_array[TXBUF_CNT] = {{{NULL,},},};

const uint8_t mac_tid2ac[TID_MAX] =
{
    AC_BE,    // TID0
    AC_BK,    // TID1
    AC_BK,    // TID2
    AC_BE,    // TID3
    AC_VI,    // TID4
    AC_VI,    // TID5
    AC_VO,    // TID6
    AC_VO,    // TID7
    AC_VO     // TIDMGT
};


#ifndef ETH_P_PAE
#define ETH_P_PAE 0x888E /* Port Access Entity (IEEE 802.1X) */
#endif /* ETH_P_PAE */

/// message queue
static rtos_queue fhost_tx_task_queue = NULL;
static rtos_semaphore fhost_tx_task_sema = NULL;
static rtos_task_handle fhost_tx_task = NULL;
#ifdef CONFIG_FHOST_TX_SCHEDULE_SEPERATE
static struct co_list fhost_tx_list;
#endif

/*
 * FUNCTIONS
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @brief Retrieve the destination station index from the destination MAC address and VIF
 * information.
 *
 * @param[in] vif Pointer to the VIF structure
 * @param[in] dst_addr Pointer to the destination MAC address in the Ethernet header
 *
 * @return The station index
 ****************************************************************************************
 */
static uint8_t fhost_tx_get_staid(struct fhost_vif_tag *vif, struct mac_addr *dst_addr)
{
    uint8_t sta_id = INVALID_STA_IDX;

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

/**
 ****************************************************************************************
 * @brief Retrieve the TX queue index to use for a transmission
 *
 * If the sta_id is valid, then the function returns the TXQ associated to the given
 * sta_id/tid pair. In this case vif_idx is not used.
 * If sta_id is not valid, then the function returns the TXQ associated to the VIF and
 * in this case tid is not used
 * If neither sta_id nor vif_idx is valid then NULL is returned.
 *
 * @param[in] vif_idx  Index of the MAC VIF
 * @param[in] sta_id   Index of the destination STA
 * @param[in] tid      TID to use for the transmission
 *
 * @return The associated TXQ or NULL if cannot find the txq
 ****************************************************************************************
 */
static struct fhost_tx_queue_tag *fhost_tx_get_txq(uint8_t vif_idx, uint8_t sta_id,
                                                   uint8_t tid)
{
    uint32_t txq_id = 0;
    //AIC_LOG_PRINTF("fhost_tx_get_txq %d %d %d\r\n", vif_idx, sta_id, tid);

    if (sta_id < STA_MAX)
    {
        if (tid > TID_MAX)
            tid = 0;

        txq_id = (sta_id * TID_MAX + tid);
    }
    else if (vif_idx < NX_VIRT_DEV_MAX)
    {
        txq_id = (STA_MAX * TID_MAX) + vif_idx;
    }
    else
    {
        return NULL;
    }

    ASSERT_ERR(txq_id < FHOST_TXQ_CNT);
    return &fhost_tx_env.tx_q[txq_id];
}

/// Mapping table of access category and traffic ID
static const int fhost_tx_ac2tid[3] = {
    [AC_BK] = TID_2,
    [AC_BE] = TID_0,
    [AC_VI] = TID_5,
};
extern struct rwnx_hw *cntrl_rwnx_hw;

/**
 ****************************************************************************************
 * @brief Send traffic information update to Wifi Task
 *
 * No need to wait confirmation from Wifi Task for this
 *
 * @param[in] sta_id    Station Index
 * @param[in] tx_avail  Whether traffic is available or not
 * @param[in] ps_type   Type of PS traffic updated
 ****************************************************************************************
 */
void fhost_tx_ps_traffic_ind(uint8_t sta_id, bool tx_avail,
                                      enum fhost_tx_ps_type ps_type)
{
    struct me_traffic_ind_req ind;
    ind.sta_idx = sta_id;
    ind.tx_avail = tx_avail;
    ind.uapsd = (ps_type == PS_UAPSD);

    rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_ME, ME_TRAFFIC_IND_REQ, sizeof(struct me_traffic_ind_req), &ind, 1, ME_TRAFFIC_IND_CFM, NULL);
}

/**
 ****************************************************************************************
 * @brief Queue one buffer in the ready list of a TXQ
 *
 * @param[in] txq   TXQ structure
 * @param[in] desc  Descriptor of the buffer to queue
 ****************************************************************************************
 */
 void fhost_tx_queue(struct fhost_tx_queue_tag *txq,
                             struct fhost_tx_desc_tag *desc)
{
    #if NX_BEACONING
    uint8_t sta_id = desc->txdesc.host.staid;

    txq->nb_ready++;
    if (sta_id != INVALID_STA_IDX)
    {
        struct fhost_tx_sta_traffic *tfc = &fhost_tx_env.traffic[sta_id];
        //aic_dbg("tfc->ps_ready[txq->ps_type] %d\r\n", tfc->ps_ready[txq->ps_type]);
        if (tfc->ps_ready[txq->ps_type] >= 0)
        {
            tfc->ps_ready[txq->ps_type]++;
            if (tfc->ps_ready[txq->ps_type] == 1)
                fhost_tx_ps_traffic_ind(sta_id, true, txq->ps_type);
        }
    }
    #endif
    co_list_push_back(&txq->ready, &desc->hdr);
}

/**
 ****************************************************************************************
 * @brief Dequeue the oldest buffer from the ready list of a TXQ
 *
 * @param[in] txq  TXQ structure
 * @return The oldest pkt queued for this TXQ (may be NULL)
 ****************************************************************************************
 */
__INLINE struct fhost_tx_desc_tag *fhost_tx_dequeue(struct fhost_tx_queue_tag *txq)
{
    return (struct fhost_tx_desc_tag *)co_list_pop_front(&txq->ready);
}

/**
 ****************************************************************************************
 * @brief Dequeue the oldest buffer from the ready list of a TXQ and update its
 * descriptor before pushing it WIFI Task.
 *
 * @param[in] txq  TXQ structure
 * @return The oldest pkt queued for this TXQ
 ****************************************************************************************
 */
struct fhost_tx_desc_tag *fhost_tx_dequeue_for_push(struct fhost_tx_queue_tag *txq)
{
    struct fhost_tx_desc_tag *desc = fhost_tx_dequeue(txq);
    ASSERT_ERR(desc);

    #if NX_BEACONING
    uint8_t sta_id = desc->txdesc.host.staid;

    if (sta_id != INVALID_STA_IDX)
    {
        struct fhost_tx_sta_traffic *tfc = &fhost_tx_env.traffic[sta_id];
        if (tfc->sp_cnt[txq->ps_type] >= 0)
        {
            tfc->sp_cnt[txq->ps_type]--;
            if ((tfc->sp_cnt[txq->ps_type] == 0) &&
                (txq->ps_type == PS_UAPSD))
                desc->txdesc.host.flags |= TXU_CNTRL_EOSP;

            if ((tfc->sp_cnt[txq->ps_type] > 0) ||
                (tfc->ps_ready[txq->ps_type] > 0))
                desc->txdesc.host.flags |= TXU_CNTRL_MORE_DATA;
            else {
                fhost_tx_ps_traffic_ind(sta_id, false, txq->ps_type);
            }
        }
    }
    txq->nb_ready--;
    #endif

    // LMAC firmware expects a TID of 0xFF for non Qos frame
    if (desc->txdesc.host.tid == TID_MGT)
        desc->txdesc.host.tid = 0xFF;

    return desc;
}

/**
 ****************************************************************************************
 * @brief return the downgraded tid which corresponds VIF
 *
 * @param[in] vif Pointer to the VIF structure
 * @param[in] tid Current TID
 *
 * @return The downgraded TID
 ****************************************************************************************
 */
static uint8_t fhost_tx_downgrade_ac(struct fhost_vif_tag *vif, uint8_t tid)
{
    int8_t ac = mac_tid2ac[tid];

    while ((vif->acm & CO_BIT(ac)) && (ac > AC_BK))
    {
        ac--;
        tid = fhost_tx_ac2tid[ac];
    }
    return tid;
}

/**
 ****************************************************************************************
 * @brief Add a TX queue to the scheduling process.
 * If the queue is already in the scheduling process, do nothing. If no queue is currently
 * in the scheduling process, the TXQ pushed will be the first scheduled. Otherwise the
 * TXQ is scheduled right after the queue currently scheduled.
 *
 * @param[in] txq Pointer to the TX queue to add to the scheduling process
 * @param[in] sched Pointer to the scheduling element
 ****************************************************************************************
 */
static void fhost_tx_add_txq_to_sched(struct fhost_tx_queue_tag *txq,
                                      struct fhost_tx_queue_sched_tag *sched)
{
    // Nothing to do if the queue is already in the scheduling list
    if (txq->status & TXQ_IN_HWQ_LIST)
        return;

    // Check if some TXQs are already part of this scheduling element
    if (sched->current)
    {
        struct fhost_tx_queue_tag *current = sched->current;

        // Insert the TXQ right after the currently scheduled queue
        txq->next = current->next;
        txq->prev = current;
        current->next->prev = txq;
        current->next = txq;
    }
    else
    {
        // No TXQ being scheduled now, add this one
        sched->current = txq;
        txq->next = txq;
        txq->prev = txq;
    }

    // Update the status
    txq->status |= TXQ_IN_HWQ_LIST;
    //AIC_LOG_PRINTF("%s, txq %x %x\r\n", __func__, txq, txq->status);
}

/**
 ****************************************************************************************
 * @brief Remove a TX queue from the scheduling process.
 * The TX queue is extracted from the sheduling list. If the TX queue is the one currently
 * scheduled, then the pointer to the current queue is updated.
 *
 * @param[in] txq Pointer to the TX queue to remove from the scheduling process
 * @param[in] sched Pointer to the scheduling element
 ****************************************************************************************
 */
static void fhost_tx_del_txq_from_sched(struct fhost_tx_queue_tag *txq,
                                     struct fhost_tx_queue_sched_tag *sched)
{
    // Sanity check - The queue shall be in the scheduling list
    ASSERT_ERR(txq->status & TXQ_IN_HWQ_LIST);

    // Check if the TXQ is alone in the scheduling element
    if (txq->next == txq)
    {
        sched->current = NULL;
    }
    else
    {
        // Extract the element from the scheduling list and update the current one
        txq->next->prev = txq->prev;
        txq->prev->next = txq->next;
        if (sched->current == txq)
        {
            sched->current = txq->next;
        }
    }

    // Reset the status of the queue
    txq->status &= ~TXQ_IN_HWQ_LIST;
    //AIC_LOG_PRINTF("%s, txq %x %x\r\n", __func__, txq, txq->status);
}

static __maybe_unused bool fhost_tx_time_cmp(uint32_t time1, uint32_t time2)
{
    uint32_t diff = time1 - time2;

    return (((int32_t)diff) < 0);
}

/**
 ****************************************************************************************
 * @brief Schedule the TX queues ready on a specific access category.
 * This function goes through the queues attached to the scheduling element of this
 * access category and pushes as many packets as possible to the MAC.
 *
 * @param[in] ac Access category to be scheduled
 ****************************************************************************************
 */
#if 0//ndef CONFIG_FHOST_TX_SCHEDULE_SEPERATE
static void fhost_tx_schedule(int ac)
{
    #ifdef CONFIG_TX_SCHEDULE
    aic_dbg("tsd:%d\n", ac);
    #endif
    int ret = 0;
#ifdef CONFIG_SDIO_SUPPORT
    struct aic_sdio_dev *sdiodev = &sdio_dev;
#endif
    struct fhost_tx_queue_sched_tag *sched = &fhost_tx_env.sched[ac];
    struct fhost_tx_queue_tag *first_txq = sched->current;
    //AIC_LOG_PRINTF("%s ac %d %x\n", __FUNCTION__, ac, first_txq);

    //ASSERT_ERR(first_txq != NULL);
    if(first_txq == NULL)
        return;

    // Loop until no queues are present anymore in the scheduling list, or until we
    // reach the queue that was scheduled first
    do
    {
        struct fhost_tx_queue_tag *txq = sched->current;
        //static uint32_t t = 0;
        //t = rtos_now(0);

        // Go through the TX queue and push for transmission as many packets as possible
        while (1)
        {
            struct txdesc *txdesc;
            struct fhost_tx_desc_tag *desc;

            // Check if we can still get data from the TX queue
            if (co_list_is_empty(&txq->ready))
            {
                // Queue empty or no credits anymore, remove TXQ from scheduling
                // This will also update the currently scheduled queue
                //GLOBAL_INT_DISABLE();
                if(txq->status & TXQ_IN_HWQ_LIST)
                    fhost_tx_del_txq_from_sched(txq, sched);
                //GLOBAL_INT_RESTORE();
                break;
            }
#ifdef CONFIG_SDIO_SUPPORT
            else {
                if (sdiodev->fw_avail_bufcnt <= 20)
                    sdiodev->fw_avail_bufcnt = aicwf_sdio_flow_ctrl();
                //aic_dbg("#*fw_avail_bufcnt:%d\n", sdiodev->fw_avail_bufcnt);
            }
#endif
#if 0
            if (co_list_cnt(&txq->ready) > IPC_FW_TXBUF_CNT) {
                aic_dbg("warning: TxQ cnt %d\r\n", co_list_cnt(&txq->ready));
            }

            if (!rwnx_check_ipc_txbuf()) {
                if (rwnx_tx_post_list_cnt()) {
                    ipc_host_txdesc_push(ac);
                }
                if (fhost_tx_time_cmp((t + TX_WAIT_FW_BUFFER_TIME_MS), rtos_now(0))) {
                    //GLOBAL_INT_DISABLE();
                    if (txq->status & TXQ_IN_HWQ_LIST)
                        fhost_tx_del_txq_from_sched(txq, sched);
                    //GLOBAL_INT_RESTORE();

                    struct fhost_tx_desc_tag *desc = NULL;
                    desc = fhost_tx_dequeue(txq);
                    fhost_tx_desc_netbuf_free(desc);
                    break;
                }
                #if defined(CONFIG_RWNX_LWIP) && defined(CFG_HOSTIF)
                #else /* CONFIG_RWNX_LWIP && CFG_HOSTIF */
                rtos_task_suspend(1);
                #endif /* CONFIG_RWNX_LWIP && CFG_HOSTIF */
                continue;
            }
#endif
#ifdef CONFIG_SDIO_SUPPORT
            while (!co_list_is_empty(&txq->ready)) {
                if (sdiodev->fw_avail_bufcnt <= DATA_FLOW_CTRL_THRESH) {
                    sdiodev->fw_avail_bufcnt = aicwf_sdio_flow_ctrl();
                } else {
                    ret = aicwf_sdio_send_check();
                    if (ret != 0)
                        continue;
#endif
                    // Get the packet descriptor from the TX queue and update it if needed
                    desc = fhost_tx_dequeue_for_push(txq);
                    txdesc = &desc->txdesc;
                    //sdiodev->tx_pktcnt--;
                    //aic_dbg("#*do:%d\n", sdiodev->tx_pktcnt);
                    uint16_t tx_len = 0;
                    uint8_t *tx_buf = NULL;

                    net_buf_tx_t *net_buf = (net_buf_tx_t *)txdesc->host.packet_addr;

                    #if 0
                    if (net_buf->data_len == txdesc->host.packet_len) { //802.11
                        tx_len = (txdesc->host.packet_len + SDIO_HOSTDESC_SIZE);
                        tx_buf = rtos_malloc(tx_len);
                        if (!tx_buf) {
                            AIC_LOG_PRINTF("Tx buf malloc fail\n");
                            break;
                        }

                        memset(tx_buf, 0 , tx_len);
                        memcpy(tx_buf, &txdesc->host, SDIO_HOSTDESC_SIZE);
                        *(uint32_t *)tx_buf = txdesc->host.tid;
                        memcpy((tx_buf + SDIO_HOSTDESC_SIZE), net_buf->data_ptr, txdesc->host.packet_len);

                    } else
                    #endif
                    {
                        #ifdef CONFIG_PING_DUMP
                        //see fhost_tx_req, it modified net_buf->data_ptr
                        if (net_buf->data_len != txdesc->host.packet_len) {
                        struct mac_eth_hdr *ethhdr = (struct mac_eth_hdr *)((uint8_t *)net_buf->data_ptr - sizeof(struct mac_eth_hdr));
                            uint8_t *ethdata = (uint8_t *)ethhdr + sizeof(struct mac_eth_hdr);
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
                                        AIC_LOG_PRINTF("ICMP tx2 %s sn:%d, %s\n", echo_str, sn, ipaddr_str);
                                    } else {
                                        AIC_LOG_PRINTF("ICMP tx2 type:%d, code:%d, %s\n", type, code, ipaddr_str);
                                    }
                                }
                            } else if (co_ntohs(ethhdr->type) == LLC_ETHERTYPE_IPV6) {
                                //todo
                            }
                        }
                        #endif
                        tx_buf = net_buf->data_ptr - (SDIO_HOSTDESC_SIZE);
                        tx_len = (txdesc->host.packet_len + SDIO_HOSTDESC_SIZE);
                        uint32_t pkt_addr = txdesc->host.packet_addr;
                        if (txdesc->host.tid < TID_MAX) {
                            txdesc->host.ac = mac_tid2ac[txdesc->host.tid];
                        } else {
                            txdesc->host.ac = 0;
                        }
                        memcpy(tx_buf, &txdesc->host, SDIO_HOSTDESC_SIZE);
                        txdesc->host.packet_addr = pkt_addr;

                        //*(uint32_t *)tx_buf = (uint32_t)txdesc->host.tid;
                        //aic_dbg("%s done\n", __func__);
                        //rwnx_data_dump("tx_buf", tx_buf, 64);
                        //rwnx_data_dump("tx data", net_buf->data_ptr, 16);
                        if (txdesc->host.flags & TXU_CNTRL_MGMT) {
                            struct mac_hdr *mac_hdr = (struct mac_hdr *)net_buf->data_ptr;
                            //aic_dbg("%s mgmt %x %d %d\r\n", __func__, (mac_hdr->fctl & MAC_FCTRL_SUBT_MASK), net_buf->data_len,rtos_now(0));
                        }
                    }
                    //see fhost_tx_req, it modified net_buf->data_ptr
                    if (net_buf->data_len != txdesc->host.packet_len) {
                        net_buf->data_ptr -= sizeof(struct mac_eth_hdr);
                    }

                    // Push the packet to the WiFi Core
                    //(Check if the TXQ is alone in the scheduling element & list is NULL)
#ifdef CONFIG_SDIO_SUPPORT
                    ret = aicwf_sdio_send(tx_buf, tx_len, (co_list_is_empty(&txq->ready) && (txq->next == txq)));
#endif
#ifdef CONFIG_USB_SUPPORT
                    ret = aicwf_usb_bus_txdata(g_aic_usb_dev, tx_buf, tx_len);
#endif
                    if(ret) {
                        aic_dbg("txdata unexpected no buf\r\n");
                    }

                    if (txdesc->host.hostid & TXU_CNTRL_NEED_CFM) {
                        fhost_tx_enqueue_cfm_list(desc);
                    } else {
                        fhost_tx_desc_netbuf_free(desc);
                    }

                    #if NX_BEACONING
                    if (txq->limit && (--txq->limit == 0))
                    {
                        //GLOBAL_INT_DISABLE();
                        if (txq->status & TXQ_IN_HWQ_LIST)
                            fhost_tx_del_txq_from_sched(txq, sched);
                        //GLOBAL_INT_RESTORE();
                        break;
                    }
                    #endif
#ifdef CONFIG_SDIO_SUPPORT
                }
            }
#endif
        }
    } while (sched->current && (sched->current != first_txq));
}
#else
static void fhost_tx_schedule(int ac)
{
    #ifdef CONFIG_TX_SCHEDULE
    aic_dbg("tsd:%d\n", ac);
    #endif
    int ret = 0;
    struct fhost_tx_queue_sched_tag *sched = &fhost_tx_env.sched[ac];
    struct fhost_tx_queue_tag *first_txq = sched->current;
    //AIC_LOG_PRINTF("%s ac %d %x\n", __FUNCTION__, ac, first_txq);

    //ASSERT_ERR(first_txq != NULL);
    if(first_txq == NULL)
        return;

    // Loop until no queues are present anymore in the scheduling list, or until we
    // reach the queue that was scheduled first
    do
    {
        struct fhost_tx_queue_tag *txq = sched->current;
        //static uint32_t t = 0;
        //t = rtos_now(0);

        // Go through the TX queue and push for transmission as many packets as possible
        while (1)
        {
            struct fhost_tx_desc_tag *desc;

            // Check if we can still get data from the TX queue
            if (co_list_is_empty(&txq->ready))
            {
                // Queue empty or no credits anymore, remove TXQ from scheduling
                // This will also update the currently scheduled queue
                //GLOBAL_INT_DISABLE();
                if(txq->status & TXQ_IN_HWQ_LIST)
                    fhost_tx_del_txq_from_sched(txq, sched);
                //GLOBAL_INT_RESTORE();
                break;
            }

            // Get the packet descriptor from the TX queue and update it if needed
            desc = fhost_tx_dequeue_for_push(txq);
            #ifdef CONFIG_FHOST_TX_AC_SCHEDULE
            FHOST_TX_AC_LOCK();
            co_list_push_back(&fhost_tx_env.tx_ac[ac], &desc->hdr);
            fhost_tx_env.tx_ac_count++;
            FHOST_TX_AC_UNLOCK();
            rtos_semaphore_signal(fhost_tx_task_sema, false);
            #else
            co_list_push_back(&fhost_tx_list, &desc->hdr);
            #endif

            #if NX_BEACONING
            if (txq->limit && (--txq->limit == 0))
            {
                //GLOBAL_INT_DISABLE();
                if (txq->status & TXQ_IN_HWQ_LIST)
                    fhost_tx_del_txq_from_sched(txq, sched);
                //GLOBAL_INT_RESTORE();
                break;
            }
            #endif
        }
    } while (sched->current && (sched->current != first_txq));
}

static __maybe_unused void fhost_tx_schedule2(void)
{
    int ret = 0;
    struct txdesc *txdesc;
    struct fhost_tx_desc_tag *desc;
#ifdef CONFIG_SDIO_SUPPORT
    struct aic_sdio_dev *sdiodev = &sdio_dev;
#endif

    while (!co_list_is_empty(&fhost_tx_list)) {
#ifdef CONFIG_SDIO_SUPPORT
        if (sdiodev->fw_avail_bufcnt <= DATA_FLOW_CTRL_THRESH) {
            sdiodev->fw_avail_bufcnt = aicwf_sdio_flow_ctrl();
            if (sdiodev->fw_avail_bufcnt <= DATA_FLOW_CTRL_THRESH) {
                continue;
            }
        }

        ret = aicwf_sdio_send_check();
        if (ret != 0) {
            continue;
        }
#endif

        desc = (struct fhost_tx_desc_tag *)co_list_pop_front(&fhost_tx_list);
        txdesc = &desc->txdesc;
        uint16_t tx_len = 0;
        uint8_t *tx_buf = NULL;

        net_buf_tx_t *net_buf = (net_buf_tx_t *)txdesc->host.packet_addr;

        {
            #ifdef CONFIG_PING_DUMP
            //see fhost_tx_req, it modified net_buf->data_ptr
            if (net_buf->data_len != txdesc->host.packet_len) {
            struct mac_eth_hdr *ethhdr = (struct mac_eth_hdr *)((uint8_t *)net_buf->data_ptr - sizeof(struct mac_eth_hdr));
                uint8_t *ethdata = (uint8_t *)ethhdr + sizeof(struct mac_eth_hdr);
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
                            AIC_LOG_PRINTF("ICMP tx2 %s sn:%d, %s\n", echo_str, sn, ipaddr_str);
                        } else {
                            AIC_LOG_PRINTF("ICMP tx2 type:%d, code:%d, %s\n", type, code, ipaddr_str);
                        }
                    }
                } else if (co_ntohs(ethhdr->type) == LLC_ETHERTYPE_IPV6) {
                    //todo
                }
            }
            #endif
            tx_buf = net_buf->data_ptr - (SDIO_HOSTDESC_SIZE);
            tx_len = (txdesc->host.packet_len + SDIO_HOSTDESC_SIZE);
            uint32_t pkt_addr = txdesc->host.packet_addr;
            if (txdesc->host.tid < TID_MAX) {
                txdesc->host.ac = mac_tid2ac[txdesc->host.tid];
            } else {
                txdesc->host.ac = 0;
            }
            memcpy(tx_buf, &txdesc->host, SDIO_HOSTDESC_SIZE);
            txdesc->host.packet_addr = pkt_addr;

            //*(uint32_t *)tx_buf = (uint32_t)txdesc->host.tid;
            //aic_dbg("%s done\n", __func__);
            //rwnx_data_dump("tx_buf", tx_buf, 64);
            //rwnx_data_dump("tx data", net_buf->data_ptr, 16);
        }
        //see fhost_tx_req, it modified net_buf->data_ptr
        if (net_buf->data_len != txdesc->host.packet_len) {
            net_buf->data_ptr -= sizeof(struct mac_eth_hdr);
        }

        // Push the packet to the WiFi Core
        #ifdef CONFIG_SDIO_SUPPORT
        ret = aicwf_sdio_send(tx_buf, tx_len, co_list_is_empty(&fhost_tx_list));
        #endif
        if(ret) {
            aic_dbg("txdata unexpected no buf\r\n");
        }

        if (txdesc->host.hostid & TXU_CNTRL_NEED_CFM) {
            fhost_tx_enqueue_cfm_list(desc);
        } else {
            fhost_tx_desc_netbuf_free(desc);
        }
    }
}

static __maybe_unused void fhost_tx_list_flush(void)
{
    struct fhost_tx_desc_tag *desc;

    while (!co_list_is_empty(&fhost_tx_list)) {
        desc = (struct fhost_tx_desc_tag *)co_list_pop_front(&fhost_tx_list);
        if (desc) {
            fhost_tx_desc_netbuf_free(desc);
        }
    }
}

void fhost_tx_list_flush_sta(struct mac_addr *sta_addr)
{
    struct fhost_tx_desc_tag *desc, *next_desc;
    struct txdesc *txdesc;

    aic_dbg("sta_addr: %x %x %x[%d]\r\n", sta_addr->array[0], sta_addr->array[1],
            sta_addr->array[2], co_list_is_empty(&fhost_tx_list));

    desc = (struct fhost_tx_desc_tag *)co_list_pick(&fhost_tx_list);
    while (desc) {
        txdesc = &desc->txdesc;
        next_desc = (struct fhost_tx_desc_tag *)co_list_next(&desc->hdr);
        if (MAC_ADDR_CMP(&txdesc->host.eth_dest_addr, sta_addr)) {
            co_list_extract(&fhost_tx_list, (struct co_list_hdr *)desc);
            fhost_tx_desc_netbuf_free(desc);
        }
        desc = next_desc;
    }
    aic_dbg("TxL flush end\r\n");
}
#ifdef CONFIG_TX_NOCOPY
int aicwf_sdio_send_pbuf(struct pbuf *pkt_data, u32 pkt_len, bool last);
extern void print_hex_dump_bytes(const void *addr, unsigned int len);
static void fhost_tx_ac_schedule(void)
{
    int ret = 0;
    struct txdesc *txdesc;
    struct fhost_tx_desc_tag *desc;
#ifdef CONFIG_SDIO_SUPPORT
    struct aic_sdio_dev *sdiodev = &sdio_dev;
#endif
    uint32_t tx_ac_count;
    uint32_t sched_count = 0;

    while (1) {
#ifdef CONFIG_SDIO_SUPPORT
        if (sdiodev->fw_avail_bufcnt <= DATA_FLOW_CTRL_THRESH) {
            sdiodev->fw_avail_bufcnt = aicwf_sdio_flow_ctrl();
            if (sdiodev->fw_avail_bufcnt <= DATA_FLOW_CTRL_THRESH) {
                continue;
            }
        }

        ret = aicwf_sdio_send_check();
        if (ret != 0) {
            continue;
        }
#endif

        FHOST_TX_AC_LOCK();
        for (int ac = AC_MAX - 1; ac >= AC_BK; ac--) {
            desc = (struct fhost_tx_desc_tag *)co_list_pop_front(&fhost_tx_env.tx_ac[ac]);
            if (desc) {
                fhost_tx_env.tx_ac_count--;
                sched_count++;
                if (sched_count > 1) {
                    rtos_semaphore_wait(fhost_tx_task_sema, 0);
                }
                break;
            }
        }
        FHOST_TX_AC_UNLOCK();

        if (desc == NULL) {
            break;
        }

        txdesc = &desc->txdesc;
        uint16_t tx_len = 0;
        uint8_t *tx_buf = NULL;

        net_buf_tx_t *net_buf = (net_buf_tx_t *)txdesc->host.packet_addr;

        if (0xFF == net_buf->pkt_type) { // mgmt
            tx_buf = net_buf->data_ptr - (SDIO_HOSTDESC_SIZE);
            tx_len = (txdesc->host.packet_len + SDIO_HOSTDESC_SIZE);
            uint32_t pkt_addr = txdesc->host.packet_addr;
            if (txdesc->host.tid < TID_MAX) {
                txdesc->host.ac = mac_tid2ac[txdesc->host.tid];
            } else {
                txdesc->host.ac = 0;
            }
            memcpy(tx_buf, &txdesc->host, SDIO_HOSTDESC_SIZE);
            txdesc->host.packet_addr = pkt_addr;

            //*(uint32_t *)tx_buf = (uint32_t)txdesc->host.tid;
            //aic_dbg("%s done\n", __func__);
            //rwnx_data_dump("tx_buf", tx_buf, 64);
            //rwnx_data_dump("tx data", net_buf->data_ptr, 16);
            if (txdesc->host.flags & TXU_CNTRL_MGMT) {
                struct mac_hdr *mac_hdr = (struct mac_hdr *)net_buf->data_ptr;

                if ((mac_hdr->fctl & MAC_FCTRL_SUBT_MASK) != MAC_FCTRL_PROBERSP_ST)
                aic_dbg("S mgmt %x %d %d\r\n",  (mac_hdr->fctl & MAC_FCTRL_SUBT_MASK), net_buf->data_len,rtos_now(0));
            }
            FHOST_TX_AC_LOCK();
            tx_ac_count = fhost_tx_env.tx_ac_count;
            FHOST_TX_AC_UNLOCK();
            // Push the packet to the WiFi Core
            #ifdef CONFIG_SDIO_SUPPORT
            ret = aicwf_sdio_send(tx_buf, tx_len, (0 == tx_ac_count));
            #endif
            if(ret) {
                aic_dbg("txdata unexpected no buf\r\n");
            }
        } else { // data
            // tx_buf = net_buf->data_ptr - (SDIO_HOSTDESC_SIZE);
            // tx_len = (txdesc->host.packet_len + SDIO_HOSTDESC_SIZE);

            struct pbuf *temp_pbuf = (struct pbuf *)net_buf->data_ptr;
//aic_dbg("%s %x p_buf->tot_len = %d\n", __func__, temp_pbuf, temp_pbuf->tot_len);
//print_hex_dump_bytes(temp_pbuf->payload, temp_pbuf->tot_len);
            if (pbuf_header(temp_pbuf, SDIO_HOSTDESC_SIZE)) {
                // Sanity check - we shall have enough space in the buffer
                ASSERT_ERR(0);
            }
            if (txdesc->host.tid < TID_MAX) {
                txdesc->host.ac = mac_tid2ac[txdesc->host.tid];
            } else {
                txdesc->host.ac = 0;
            }
            memcpy(temp_pbuf->payload, &txdesc->host, SDIO_HOSTDESC_SIZE);
            //txdesc->host.packet_addr = pkt_addr;

            //*(uint32_t *)tx_buf = (uint32_t)txdesc->host.tid;
            //aic_dbg("%s done\n", __func__);
            //rwnx_data_dump("tx_buf", tx_buf, 64);
            //rwnx_data_dump("tx data", net_buf->data_ptr, 16);

            //see fhost_tx_req, it modified net_buf->data_ptr
            // if (net_buf->data_len != txdesc->host.packet_len) {
            //     net_buf->data_ptr -= sizeof(struct mac_eth_hdr);
            // }
            FHOST_TX_AC_LOCK();
            tx_ac_count = fhost_tx_env.tx_ac_count;
            FHOST_TX_AC_UNLOCK();
            // Push the packet to the WiFi Core
            ret = aicwf_sdio_send_pbuf(temp_pbuf, temp_pbuf->tot_len, (0 == tx_ac_count));
            if(ret) {
                aic_dbg("txdata unexpected no buf\r\n");
            }

            // net_buf->data_ptr += buf_len_adjust;
            if (pbuf_header(temp_pbuf, (sizeof(struct mac_eth_hdr)- SDIO_HOSTDESC_SIZE)))
            {
                // Sanity check - we shall have enough space in the buffer
                ASSERT_ERR(0);
            }
        }

        if (txdesc->host.hostid & TXU_CNTRL_NEED_CFM) {
            fhost_tx_enqueue_cfm_list(desc);
        } else {
            fhost_tx_desc_netbuf_free(desc);
        }

        //only process 1 packet for now
        break;
    }
} 
#else
static void fhost_tx_ac_schedule(void)
{
    int ret = 0;
    struct txdesc *txdesc;
    struct fhost_tx_desc_tag *desc;
#ifdef CONFIG_SDIO_SUPPORT
    struct aic_sdio_dev *sdiodev = &sdio_dev;
#endif
    uint32_t tx_ac_count;
    uint32_t sched_count = 0;

    while (1) {
#ifdef CONFIG_SDIO_SUPPORT
        if (sdiodev->fw_avail_bufcnt <= DATA_FLOW_CTRL_THRESH) {
            sdiodev->fw_avail_bufcnt = aicwf_sdio_flow_ctrl();
            if (sdiodev->fw_avail_bufcnt <= DATA_FLOW_CTRL_THRESH) {
                continue;
            }
        }

#ifdef CONFIG_SDIO_BUS_PWRCTRL
        aicwf_sdio_bus_pwr_stctl(sdiodev, SDIO_BUS_ACTIVE);
#endif /* CONFIG_SDIO_BUS_PWRCTRL */

        ret = aicwf_sdio_send_check();
        if (ret != 0) {
            continue;
        }
#endif /* CONFIG_SDIO_SUPPORT */

        FHOST_TX_AC_LOCK();
        for (int ac = AC_MAX - 1; ac >= AC_BK; ac--) {
            desc = (struct fhost_tx_desc_tag *)co_list_pop_front(&fhost_tx_env.tx_ac[ac]);
            if (desc) {
                fhost_tx_env.tx_ac_count--;
                sched_count++;
                if (sched_count > 1) {
                    rtos_semaphore_wait(fhost_tx_task_sema, 0);
                }
                break;
            }
        }
        FHOST_TX_AC_UNLOCK();

        if (desc == NULL) {
            break;
        }

        txdesc = &desc->txdesc;
        uint16_t tx_len = 0;
        uint8_t *tx_buf = NULL;

        net_buf_tx_t *net_buf = (net_buf_tx_t *)txdesc->host.packet_addr;

        {
            #ifdef CONFIG_PING_DUMP
            //see fhost_tx_req, it modified net_buf->data_ptr
            if (net_buf->data_len != txdesc->host.packet_len) {
            struct mac_eth_hdr *ethhdr = (struct mac_eth_hdr *)((uint8_t *)net_buf->data_ptr - sizeof(struct mac_eth_hdr));
                uint8_t *ethdata = (uint8_t *)ethhdr + sizeof(struct mac_eth_hdr);
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
                            AIC_LOG_PRINTF("ICMP tx2 %s sn:%d, %s\n", echo_str, sn, ipaddr_str);
                        } else {
                            AIC_LOG_PRINTF("ICMP tx2 type:%d, code:%d, %s\n", type, code, ipaddr_str);
                        }
                    }
                } else if (co_ntohs(ethhdr->type) == LLC_ETHERTYPE_IPV6) {
                    //todo
                }
            }
            #endif

            tx_buf = net_buf->data_ptr - (SDIO_HOSTDESC_SIZE);
            tx_len = (txdesc->host.packet_len + SDIO_HOSTDESC_SIZE);
            uint32_t pkt_addr = txdesc->host.packet_addr;
            if (txdesc->host.tid < TID_MAX) {
                txdesc->host.ac = mac_tid2ac[txdesc->host.tid];
            } else {
                txdesc->host.ac = 0;
            }
            memcpy(tx_buf, &txdesc->host, SDIO_HOSTDESC_SIZE);
            txdesc->host.packet_addr = pkt_addr;

            //*(uint32_t *)tx_buf = (uint32_t)txdesc->host.tid;
            //aic_dbg("%s done\n", __func__);
            //rwnx_data_dump("tx_buf", tx_buf, 64);
            //rwnx_data_dump("tx data", net_buf->data_ptr, 16);
            if (txdesc->host.flags & TXU_CNTRL_MGMT) {
                struct mac_hdr *mac_hdr = (struct mac_hdr *)net_buf->data_ptr;
                if ((mac_hdr->fctl & MAC_FCTRL_SUBT_MASK) != MAC_FCTRL_PROBERSP_ST)
                aic_dbg("S mgmt %x %d %d\r\n",  (mac_hdr->fctl & MAC_FCTRL_SUBT_MASK), net_buf->data_len,rtos_now(0));
            }
        }
        //see fhost_tx_req, it modified net_buf->data_ptr
        if (net_buf->data_len != txdesc->host.packet_len) {
            net_buf->data_ptr -= sizeof(struct mac_eth_hdr);
        }

		#ifdef CONFIG_SDIO_BUS_PWRCTRL
        rtos_mutex_lock(sdio_dev.tx_sema, -1);
        sdio_dev.tx_pktcnt--;
        //aic_dbg("%s, tx_pktcnt: %d", __func__, sdio_dev.tx_pktcnt);
        rtos_mutex_unlock(sdio_dev.tx_sema);
        #endif  /* CONFIG_SDIO_BUS_PWRCTRL */

        FHOST_TX_AC_LOCK();
        tx_ac_count = fhost_tx_env.tx_ac_count;
        FHOST_TX_AC_UNLOCK();
        // Push the packet to the WiFi Core
        #ifdef CONFIG_SDIO_SUPPORT
        ret = aicwf_sdio_send(tx_buf, tx_len, (0 == tx_ac_count));
        #endif
        if(ret) {
            aic_dbg("txdata unexpected no buf\r\n");
        }

        if (txdesc->host.hostid & TXU_CNTRL_NEED_CFM) {
            fhost_tx_enqueue_cfm_list(desc);
        } else {
            fhost_tx_desc_netbuf_free(desc);
        }

        //only process 1 packet for now
        break;
    }
}
#endif
static void fhost_tx_ac_flush(void)
{
    struct fhost_tx_desc_tag *desc;

    aic_dbg("%s ac_cnt %d\r\n",__func__, fhost_tx_env.tx_ac_count);
    FHOST_TX_AC_LOCK();
    for (int ac = AC_MAX - 1; ac >= AC_BK; ac--) {
        while (!co_list_is_empty(&fhost_tx_env.tx_ac[ac])) {
            desc = (struct fhost_tx_desc_tag *)co_list_pop_front(&fhost_tx_env.tx_ac[ac]);
            if (desc) {
                fhost_tx_env.tx_ac_count--;
                fhost_tx_desc_netbuf_free(desc);
            } else {
                break;
            }
        }
    }
    FHOST_TX_AC_UNLOCK();
}

void fhost_tx_ac_flush_sta(struct mac_addr *sta_addr)
{
    struct fhost_tx_desc_tag *desc, *next_desc;
    struct txdesc *txdesc;

    FHOST_TX_AC_LOCK();
    aic_dbg("AC flush sta_addr: %x %x %x[%d]\r\n", sta_addr->array[0], sta_addr->array[1],
                                                sta_addr->array[2], fhost_tx_env.tx_ac_count);
    for (int ac = AC_MAX - 1; ac >= AC_BK; ac--) {
        desc = (struct fhost_tx_desc_tag *)co_list_pick(&fhost_tx_env.tx_ac[ac]);
        while (desc) {
            txdesc = &desc->txdesc;
            next_desc = (struct fhost_tx_desc_tag *)co_list_next(&desc->hdr);
            //aic_dbg("desc %x, next_desc %x %d\r\n", desc, next_desc, co_list_cnt(&fhost_tx_env.tx_ac[ac]));
            //aic_dbg("DA: %x %x %x[%d]\r\n", txdesc->host.eth_dest_addr.array[0], txdesc->host.eth_dest_addr.array[1],
            //                                txdesc->host.eth_dest_addr.array[2], (MAC_ADDR_CMP(&txdesc->host.eth_dest_addr, sta_addr)));
            if (MAC_ADDR_CMP(&txdesc->host.eth_dest_addr, sta_addr)) {
                co_list_extract(&fhost_tx_env.tx_ac[ac], desc);
                fhost_tx_env.tx_ac_count--;
                fhost_tx_desc_netbuf_free(desc);
            }
            desc = next_desc;
            //aic_dbg("next desc %x ac_cnt %d\r\n", desc, fhost_tx_env.tx_ac_count);
        }
    }
    aic_dbg("AC flush end[%d]\r\n", fhost_tx_env.tx_ac_count);
    FHOST_TX_AC_UNLOCK();
}
#endif

#if NX_MFP
bool mfp_is_robust_frame(uint16_t frame_cntl, uint8_t action)
{
    if ((frame_cntl & MAC_FCTRL_TYPE_MASK) != MAC_FCTRL_MGT_T)
        return false;

    switch (frame_cntl & MAC_FCTRL_SUBT_MASK) {
        case MAC_FCTRL_DEAUTHENT_ST:
        case MAC_FCTRL_DISASSOC_ST:
            return true;
        case MAC_FCTRL_ACTION_ST:
            switch (action) {
                case MAC_PUBLIC_ACTION_CATEGORY:
                case MAC_HT_ACTION_CATEGORY:
                case MAC_UNPROT_WNM_ACTION_CATEGORY:
                case MAC_SELF_PROT_ACTION_CATEGORY:
                case MAC_VENDOR_ACTION_CATEGORY:
                    return false;
                default:
                    return true;
            }
        default :
            return false;
    }
}
#endif

/**
 ****************************************************************************************
 * @brief Release a buffer pushed by upper layer
 *
 * This is called whether the buffer has been transmitted or discarded.
 *
 * @param[in] net_buf     Net Buffer to release
 * @param[in] tx_status   Buffer TX status as reported by umac (0 if discarded)
 * @param[in] cfm_cb      Confirmation callback for the buffer
 * @param[in] cfm_cb_arg  Confirmation callback private argument
 ****************************************************************************************
 */
void fhost_tx_release_buf(net_buf_tx_t *net_buf, uint32_t tx_status,
                                 cb_fhost_tx cfm_cb, void *cfm_cb_arg)
{
    if (cfm_cb)
        cfm_cb((uint32_t)net_buf, tx_status & TX_STATUS_ACKNOWLEDGED, cfm_cb_arg);

    net_buf_tx_free(net_buf);
}

#if NX_BEACONING
static uint16_t ap_mgmt_get_seq_ctrl(void)
{
    // Increment the sequence number
    ap_mgmt_seqnbr++;

    // Build the sequence control
    return (ap_mgmt_seqnbr << MAC_SEQCTRL_NUM_OFT);
}
#endif /* NX_BEACONING */

/**
 ****************************************************************************************
 * @brief Push a network buffer to a TX queue.
 *
 * This function is directly call from the network stack thread.
 * It is called with the tx mutex hold
 *
 * @param[in] net_if      Pointer to the net interface for which the packet is pushed
 * @param[in] net_buf     Pointer to the net buffer to transmit.
 * @param[in] type        Type of buffer
 * @param[in] cfm_cb      Confirmation callback for the buffer (Only if type == IEEE802_11)
 * @param[in] cfm_cb_arg  Private argument for confirmation callback
 ****************************************************************************************
 */

#ifdef CONFIG_TX_NOCOPY
static void fhost_tx_req(net_if_t *net_if, net_buf_tx_t *net_buf, enum fhost_tx_buf_type type,
                         cb_fhost_tx cfm_cb, void *cfm_cb_arg)
{
    #ifdef CONFIG_TX_TRIGGER_TS
    uint32_t start_time = 0;
    uint32_t end_time = 0;
    static volatile uint8_t txq_count = 0;
    #endif
    struct fhost_tx_desc_tag *desc;
    struct txdesc *txdesc;
    struct hostdesc *host;
    struct fhost_tx_queue_tag *txq;
    struct fhost_vif_tag *vif;

    uint8_t seg_cnt = 0;
    uint16_t buf_len_adjust = 0;

    // Get the information about the buffer
    desc = (struct fhost_tx_desc_tag *)fhost_tx_dequeue_free_list();
    if (desc == NULL)
    {
        aic_dbg("TX desc free list NULL\n");
        // Free the net buffer
        fhost_tx_release_buf(net_buf, 0, cfm_cb, cfm_cb_arg);
        return;
    }

    // Get the private information about the VIF
    vif = net_if_vif_info(net_if);

    txdesc = &desc->txdesc;
    host = &txdesc->host;
    memset(host, 0 , sizeof(*host));
    host->flags = 0;
    host->ethertype = 0;
    host->hostid = 0;
    host->vif_idx = vif->mac_vif->index;
    host->cfm_cb = (void *)cfm_cb;
    host->cfm_cb_arg = cfm_cb_arg;
    //desc->pbd_cnt    = seg_cnt;

    if (type == IEEE802_3)
    {
        struct pbuf *temp_pbuf = (struct pbuf *)net_buf->data_ptr;
        struct mac_eth_hdr *eth_hdr = (struct mac_eth_hdr *)temp_pbuf->payload;

        // Parse Ethernet header
        host->eth_dest_addr = eth_hdr->da;
        host->eth_src_addr = eth_hdr->sa;
        host->ethertype = eth_hdr->type;

        if (co_ntohs(host->ethertype) == LLC_ETHERTYPE_IP)
        {
            // Read DSCP field in the IP header to determine TID (2nd byte after the header)
            uint8_t dscp = *((uint8_t *)(eth_hdr + 1) + 1) & 0xFC;
            host->tid = dscp >> 5;
        }
        else if (co_ntohs(host->ethertype) == LLC_ETHERTYPE_EAP_T)
        {
            host->tid = TID_6;
        }
        else
        {
            host->tid = 0;
        }
        // and skip it once info have been extracted
        //seg_addr[0] += sizeof(*eth_hdr);
        //seg_len[0] -= sizeof(*eth_hdr);
        buf_len_adjust = sizeof(*eth_hdr);
        // net_buf->data_ptr += buf_len_adjust;
        if (pbuf_header(temp_pbuf, -buf_len_adjust))
        {
            // Sanity check - we shall have enough space in the buffer
            ASSERT_ERR(0);
        }
// aic_dbg("%s %x p_buf->tot_len = %d\n", __func__, temp_pbuf, temp_pbuf->tot_len);
// print_hex_dump_bytes(temp_pbuf->payload, temp_pbuf->tot_len);
    }
    else if (type == IEEE802_11)
    {
        struct mac_hdr *mac_hdr = (struct mac_hdr *)net_buf->data_ptr;
        host->eth_dest_addr = mac_hdr->addr1;
        host->tid = TID_MGT;
        host->flags |= TXU_CNTRL_MGMT;
    #if NX_MFP
        if (mfp_is_robust_frame(mac_hdr->fctl, *((uint8_t *)(mac_hdr + 1))))
            host->flags |= TXU_CNTRL_MGMT_ROBUST;
    #endif
        if((MAC_FCTRL_ASSOCRSP_ST == (mac_hdr->fctl & MAC_FCTRL_SUBT_MASK)) || \
            (MAC_FCTRL_ACTION_ST == (mac_hdr->fctl & MAC_FCTRL_SUBT_MASK))  || \
            (MAC_FCTRL_REASSOCRSP_ST == (mac_hdr->fctl & MAC_FCTRL_SUBT_MASK))  || \
            (MAC_FCTRL_AUTHENT_ST == (mac_hdr->fctl & MAC_FCTRL_SUBT_MASK))  || \
            (MAC_FCTRL_NULL_FUNCTION == (mac_hdr->fctl & 0xFF))) {
            host->hostid |= TXU_CNTRL_NEED_CFM;
        }
        if ((mac_hdr->fctl & MAC_FCTRL_SUBT_MASK) != MAC_FCTRL_PROBERSP_ST)
        aic_dbg("T mgmt %x %d %d\r\n",  (mac_hdr->fctl & MAC_FCTRL_SUBT_MASK), net_buf->data_len,rtos_now(0));
    #if NX_BEACONING
        if(VIF_AP == vif->mac_vif->type) {
            mac_hdr->seq = ap_mgmt_get_seq_ctrl();
        }
    #endif
    }
    txdesc->host.packet_addr = (uint32_t)net_buf;
    txdesc->host.packet_len  = net_buf->data_len - buf_len_adjust;

    // Get destination STA id
    host->staid = fhost_tx_get_staid(vif, &host->eth_dest_addr);

    if (vif->acm)
    {
        // Get TID from descriptor and update it if necessary
        host->tid = fhost_tx_downgrade_ac(vif, host->tid);
    }

    #if NX_BEACONING
    if((VIF_AP == vif->mac_vif->type) && (0xFF == host->staid) && (0 != host->ethertype)) {
        //aic_dbg("Peer STA not found\r\n");
        // net_buf->data_ptr -= (buf_len_adjust);
        fhost_tx_desc_netbuf_free(desc);
        return;
    }
    #endif

    FHOST_TXQ_LOCK();
    // Check if the queue is active
    txq = fhost_tx_get_txq(vif->mac_vif->index, host->staid, host->tid);
    if (!txq || !(txq->status & TXQ_ENABLED))
    {
        AIC_LOG_PRINTF(" Fail :Queue is not active %d %d %d, %x status %x type %x\n", vif->mac_vif->index,  host->staid, host->tid, txq,
        txq->status, type);
        // net_buf->data_ptr -= (buf_len_adjust);
        fhost_tx_desc_netbuf_free(desc);
        FHOST_TXQ_UNLOCK();
        return;
    }

    // Queue descriptor
    fhost_tx_queue(txq, desc);
#ifdef CONFIG_SDIO_SUPPORT
    //sdiodev->tx_pktcnt++;
    //aic_dbg("#*di:%d\n", sdiodev->tx_pktcnt);
#endif
    //AIC_LOG_PRINTF("Txq cnt %d, st %x\r\n",  co_list_cnt(&txq->ready), txq->status);
    //AIC_LOG_PRINTF("!!Txq %x, ac %d %x\r\n", txq, mac_tid2ac[host->tid], host->staid);

    #if defined(CONFIG_FHOST_TX_SCHEDULE_SEPERATE) && defined(CONFIG_FHOST_TX_AC_SCHEDULE)
    // Check if we can add the TX queue to the schedule list
    if (!(txq->status & TXQ_STOP))
    {
        int ac = mac_tid2ac[host->tid];
        fhost_tx_add_txq_to_sched(txq, &fhost_tx_env.sched[ac]);
        fhost_tx_schedule(ac);
    }
    FHOST_TXQ_UNLOCK();
    #endif
}
#else
static void fhost_tx_req(net_if_t *net_if, net_buf_tx_t *net_buf, enum fhost_tx_buf_type type,
                         cb_fhost_tx cfm_cb, void *cfm_cb_arg)
{
    #ifdef CONFIG_TX_TRIGGER_TS
    uint32_t start_time = 0;
    uint32_t end_time = 0;
    static volatile uint8_t txq_count = 0;
    #endif
    struct fhost_tx_desc_tag *desc;
    struct txdesc *txdesc;
    struct hostdesc *host;
    struct fhost_tx_queue_tag *txq;
    struct fhost_vif_tag *vif;

    uint8_t seg_cnt = 0;
    uint16_t buf_len_adjust = 0;

    // Get the information about the buffer
    desc = (struct fhost_tx_desc_tag *)fhost_tx_dequeue_free_list();
    if (desc == NULL)
    {
        aic_dbg("TX desc free list NULL\n");
        // Free the net buffer
        fhost_tx_release_buf(net_buf, 0, cfm_cb, cfm_cb_arg);
        return;
    }

    #if 0
    net_buf_tx_info(net_buf, &buf_len, &seg_cnt);
    if (!seg_cnt)
    {
        aic_dbg("net buf err: %d\n", seg_cnt);
        fhost_tx_release_buf(net_buf, 0, cfm_cb, cfm_cb_arg);
        co_list_push_back(&tx_desc_free_list, (struct co_list_hdr *)(desc));
        return;
    }
    #endif

	#ifdef CONFIG_SDIO_BUS_PWRCTRL
    rtos_mutex_lock(sdio_dev.tx_sema, -1);
    sdio_dev.tx_pktcnt++;
    //aic_dbg("%s, tx_pktcnt 1: %d, type: %d", __func__, sdio_dev.tx_pktcnt, type);
    rtos_mutex_unlock(sdio_dev.tx_sema);
    #endif  /* CONFIG_SDIO_BUS_PWRCTRL */


    // Get the private information about the VIF
    vif = net_if_vif_info(net_if);

    txdesc = &desc->txdesc;
    host = &txdesc->host;
    memset(host, 0 , sizeof(*host));
    host->flags = 0;
    host->ethertype = 0;
    host->hostid = 0;
    host->vif_idx = vif->mac_vif->index;
    host->cfm_cb = (void *)cfm_cb;
    host->cfm_cb_arg = cfm_cb_arg;
    //desc->pbd_cnt    = seg_cnt;

    if (type == IEEE802_3)
    {
        struct mac_eth_hdr *eth_hdr = (struct mac_eth_hdr *)net_buf->data_ptr;

        // Parse Ethernet header
        host->eth_dest_addr = eth_hdr->da;
        host->eth_src_addr = eth_hdr->sa;
        host->ethertype = eth_hdr->type;

        if (co_ntohs(host->ethertype) == LLC_ETHERTYPE_IP)
        {
            // Read DSCP field in the IP header to determine TID (2nd byte after the header)
            uint8_t dscp = *((uint8_t *)(eth_hdr + 1) + 1) & 0xFC;
            host->tid = dscp >> 5;
        }
        else if (co_ntohs(host->ethertype) == LLC_ETHERTYPE_EAP_T)
        {
            host->tid = TID_6;
        }
        else
        {
            host->tid = 0;
        }

        if (co_ntohs(host->ethertype) == LLC_ETHERTYPE_EAP_T) {
            aic_dbg("Tx eapol, len %d(%d)\r\n",  net_buf->data_len, rtos_now(0));
            host->hostid |= TXU_CNTRL_NEED_CFM;
        }

        // and skip it once info have been extracted
        //seg_addr[0] += sizeof(*eth_hdr);
        //seg_len[0] -= sizeof(*eth_hdr);
        buf_len_adjust = sizeof(*eth_hdr);
        net_buf->data_ptr += buf_len_adjust;
        #ifdef CONFIG_PING_DUMP
        uint8_t *ethdata = (uint8_t *)eth_hdr + sizeof(struct mac_eth_hdr);
        if (co_ntohs(eth_hdr->type) == LLC_ETHERTYPE_IP) {
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
                    AIC_LOG_PRINTF("ICMP tx1 %s sn:%d, %s\n", echo_str, sn, ipaddr_str);
                } else {
                    AIC_LOG_PRINTF("ICMP tx1 type:%d, code:%d, %s\n", type, code, ipaddr_str);
                }
            }
        } else if (co_ntohs(eth_hdr->type) == LLC_ETHERTYPE_IPV6) {
            //todo
        }
        #endif
    }
    else if (type == IEEE802_11)
    {
        struct mac_hdr *mac_hdr = (struct mac_hdr *)net_buf->data_ptr;
        host->eth_dest_addr = mac_hdr->addr1;
        host->tid = TID_MGT;
        host->flags |= TXU_CNTRL_MGMT;
    #if NX_MFP
        if (mfp_is_robust_frame(mac_hdr->fctl, *((uint8_t *)(mac_hdr + 1))))
            host->flags |= TXU_CNTRL_MGMT_ROBUST;
    #endif
        if((MAC_FCTRL_ASSOCRSP_ST == (mac_hdr->fctl & MAC_FCTRL_SUBT_MASK)) || \
            (MAC_FCTRL_ACTION_ST == (mac_hdr->fctl & MAC_FCTRL_SUBT_MASK))  || \
            (MAC_FCTRL_REASSOCRSP_ST == (mac_hdr->fctl & MAC_FCTRL_SUBT_MASK))  || \
            (MAC_FCTRL_AUTHENT_ST == (mac_hdr->fctl & MAC_FCTRL_SUBT_MASK))  || \
            (MAC_FCTRL_NULL_FUNCTION == (mac_hdr->fctl & 0xFF))) {
            host->hostid |= TXU_CNTRL_NEED_CFM;
        }
        if ((mac_hdr->fctl & MAC_FCTRL_SUBT_MASK) != MAC_FCTRL_PROBERSP_ST)
        aic_dbg("T mgmt %x %d %d\r\n",  (mac_hdr->fctl & MAC_FCTRL_SUBT_MASK), net_buf->data_len,rtos_now(0));
    #if NX_BEACONING
        if(VIF_AP == vif->mac_vif->type) {
            mac_hdr->seq = ap_mgmt_get_seq_ctrl();
        }
    #endif
    }
    txdesc->host.packet_addr = (uint32_t)net_buf;
    txdesc->host.packet_len  = net_buf->data_len - buf_len_adjust;

    // Get destination STA id
    host->staid = fhost_tx_get_staid(vif, &host->eth_dest_addr);

    if (vif->acm)
    {
        // Get TID from descriptor and update it if necessary
        host->tid = fhost_tx_downgrade_ac(vif, host->tid);
    }

    #if NX_BEACONING
    if((VIF_AP == vif->mac_vif->type) && (0xFF == host->staid) && (0 != host->ethertype)) {
        //aic_dbg("Peer STA not found\r\n");
        #ifdef CONFIG_SDIO_BUS_PWRCTRL
        rtos_mutex_lock(sdio_dev.tx_sema, -1);
        sdio_dev.tx_pktcnt--;
        //aic_dbg("tx_pktcnt_m sta: %d", sdio_dev.tx_pktcnt);
        rtos_mutex_unlock(sdio_dev.tx_sema);
        #endif  /* CONFIG_SDIO_BUS_PWRCTRL */

        net_buf->data_ptr -= (buf_len_adjust);
        fhost_tx_desc_netbuf_free(desc);
        return;
    }
    #endif

    FHOST_TXQ_LOCK();
    // Check if the queue is active
    txq = fhost_tx_get_txq(vif->mac_vif->index, host->staid, host->tid);
    if (!txq || !(txq->status & TXQ_ENABLED))
    {
		#ifdef CONFIG_SDIO_BUS_PWRCTRL
        rtos_mutex_lock(sdio_dev.tx_sema, -1);
        sdio_dev.tx_pktcnt--;
        //aic_dbg("%s, tx_pktcnt: %d", __func__, sdio_dev.tx_pktcnt);
        rtos_mutex_unlock(sdio_dev.tx_sema);
        #endif  /* CONFIG_SDIO_BUS_PWRCTRL */

        AIC_LOG_PRINTF(" Fail :Queue is not active %d %d %d, %x status %x type %x\n", vif->mac_vif->index,  host->staid, host->tid, txq,
        txq->status, type);
        net_buf->data_ptr -= (buf_len_adjust);
        fhost_tx_desc_netbuf_free(desc);
        FHOST_TXQ_UNLOCK();
        return;
    }

    // Queue descriptor
    fhost_tx_queue(txq, desc);
#ifdef CONFIG_SDIO_SUPPORT
    //sdiodev->tx_pktcnt++;
    //aic_dbg("#*di:%d\n", sdiodev->tx_pktcnt);
#endif
    //AIC_LOG_PRINTF("Txq cnt %d, st %x\r\n",  co_list_cnt(&txq->ready), txq->status);
    //AIC_LOG_PRINTF("!!Txq %x, ac %d %x\r\n", txq, mac_tid2ac[host->tid], host->staid);

    #if defined(CONFIG_FHOST_TX_SCHEDULE_SEPERATE) && defined(CONFIG_FHOST_TX_AC_SCHEDULE)
    // Check if we can add the TX queue to the schedule list
    if (!(txq->status & TXQ_STOP))
    {
        int ac = mac_tid2ac[host->tid];
        fhost_tx_add_txq_to_sched(txq, &fhost_tx_env.sched[ac]);
        fhost_tx_schedule(ac);
    } else {
        aic_dbg("ERR: drop");
    }
    FHOST_TXQ_UNLOCK();
    #else
    uint32_t txq_ready_cnt = co_list_cnt(&txq->ready);
    FHOST_TXQ_UNLOCK();
#ifdef CONFIG_SDIO_SUPPORT
    if ((txq_ready_cnt == 1) || (true == rtos_queue_is_empty(fhost_tx_task_queue)))
#else
    if (co_list_cnt(&txq->ready) < 2)
#endif
    {
        uint32_t msg;
        msg = host->tid | (host->vif_idx << 8) | (host->staid << 16);
        #if FHOST_TX_MSG_USE_QUEUE
        int ret = rtos_queue_write(fhost_tx_task_queue, (void *)&msg, 0, false);
        #else
        int ret = rtos_signal_send(fhost_tx_task, (void *)&msg, 4, false);
        #endif
        if (ret)
        {
            aic_dbg("TX task Q write err\r\n");
            // retry write queue
            #if FHOST_TX_MSG_USE_QUEUE
            rtos_queue_write(fhost_tx_task_queue, (void *)&msg, 0, false);
            #else
            rtos_signal_send(fhost_tx_task, (void *)&msg, 4, false);
            #endif
        }
        #ifdef CONFIG_TX_TRIGGER_TS
        end_time = rtos_now(false);
        aic_dbg("txqe:%u/%u\n", end_time, txq_count);
        #endif
    }

    #endif
}
#endif

static void fhost_tx_cfm(uint32_t statinfo)
{
    struct fhost_tx_queue_tag *txq;
    int ac;
    struct txdesc *txdesc;

    if (statinfo & TX_STATUS_RETRY_REQUIRED) {
        // handled by fw
        return;
    }
    if ((statinfo == 0) && !fhost_tx_cfm_list_cnt(1)) {
        fhost_tx_env.to_times++;
        aic_dbg("to %d\r\n", fhost_tx_env.to_times);
        return;
    }
    if ((statinfo == 0) && fhost_tx_cfm_list_cnt(1))  { // To fix fw
        statinfo |= TX_STATUS_ACKNOWLEDGED;
        statinfo |= TX_STATUS_DONE;
    }

    //net_buf_tx_t *net_buf;
    struct fhost_tx_desc_tag *desc = (struct fhost_tx_desc_tag *)fhost_tx_dequeue_cfm_list();
    if (!desc) {
        aic_dbg("tx cfm list null\r\n");
        return;
    }
    txdesc = &desc->txdesc;
    //net_buf = (net_buf_tx_t *)txdesc->host.packet_addr;
    if (txdesc->host.tid == 0xFF)
        txdesc->host.tid = TID_MGT;
    txdesc->host.cfm.status = statinfo;
    //AIC_LOG_PRINTF("cfm list pop, statinfo:%x\r\n",statinfo);

    ac = mac_tid2ac[txdesc->host.tid];

    txq = fhost_tx_get_txq(txdesc->host.vif_idx, txdesc->host.staid, txdesc->host.tid);
    ASSERT_ERR(txq);

    #if NX_BEACONING
    if (txdesc->host.cfm.status & TX_STATUS_ACKNOWLEDGED) {
        if(txdesc->host.staid < NX_REMOTE_STA_MAX) {
            struct sta_info_tag *sta = vif_mgmt_get_sta_by_staid(txdesc->host.staid);
            if (sta)
                sta->last_active_time_us = rtos_now(0);
        }
    }
    #endif /* NX_BEACONING */

    // Check if the queue is active
    if (!(txq->status & TXQ_ENABLED))
    {
        fhost_tx_desc_netbuf_free(desc);
    }
    // Check the status of the packet
    else if (txdesc->host.cfm.status & TX_STATUS_SW_RETRY_REQUIRED)
    {
        AIC_LOG_PRINTF("###TX_STATUS_RETRY_REQUIRED %x\r\n", txdesc->host.hostid);
        // For the time being, retries are pushed immediately
        txdesc->host.flags = TXU_CNTRL_RETRY;

        // Push back the packet to the txq
        fhost_tx_queue(txq, desc);
    }
    else if (txdesc->host.cfm.status & TX_STATUS_DONE)
    {
        // Update the number of credits of the queue
        //txq->credits += txdesc->host.cfm.credits;
        //if (txq->credits > 0)
        //{
        //    txq->status &= ~TXQ_STOP_FULL;
        //}
        fhost_tx_desc_netbuf_free(desc);
    } else {
        aic_dbg("###txqs %x %x\r\n", txq->status, txdesc->host.cfm.status);
        fhost_tx_desc_netbuf_free(desc);
    }

    // Check if we can add the TX queue to the schedule list
    if (!(txq->status & TXQ_STOP) && !co_list_is_empty(&txq->ready))
    {
        FHOST_TXQ_LOCK();
        fhost_tx_add_txq_to_sched(txq, &fhost_tx_env.sched[ac]);
        fhost_tx_schedule(ac);
        FHOST_TXQ_UNLOCK();
    }
}

uint8_t fhost_tx_get_to_times(void)
{
    return fhost_tx_env.to_times;
}

void fhost_tx_enqueue_cfm_list(struct fhost_tx_desc_tag *desc)
{
    rtos_mutex_lock(tx_desc_cfm_mutex, -1);
    co_list_push_back(&tx_desc_cfm_list, (struct co_list_hdr *)(desc));
    tx_desc_cfm_cnt++;
    rtos_mutex_unlock(tx_desc_cfm_mutex);
}

struct fhost_tx_desc_tag *fhost_tx_dequeue_cfm_list(void)
{
    struct fhost_tx_desc_tag *desc;

    rtos_mutex_lock(tx_desc_cfm_mutex, -1);
    desc = (struct fhost_tx_desc_tag *)co_list_pop_front(&tx_desc_cfm_list);
    if (desc) {
        tx_desc_cfm_cnt--;
    }
    rtos_mutex_unlock(tx_desc_cfm_mutex);

    return desc;
}

uint32_t fhost_tx_cfm_list_cnt(bool lock)
{
    uint32_t cnt;

    if (lock) {
        rtos_mutex_lock(tx_desc_cfm_mutex, -1);
    }
    cnt = tx_desc_cfm_cnt;
    if (lock) {
        rtos_mutex_unlock(tx_desc_cfm_mutex);
    }

    return cnt;
}

void fhost_tx_enqueue_free_list(struct fhost_tx_desc_tag *desc)
{
    rtos_mutex_lock(tx_desc_free_mutex, -1);
    co_list_push_back(&tx_desc_free_list, (struct co_list_hdr *)(desc));
    tx_desc_free_cnt++;
    rtos_mutex_unlock(tx_desc_free_mutex);
}

struct fhost_tx_desc_tag *fhost_tx_dequeue_free_list(void)
{
    struct fhost_tx_desc_tag *desc;

    rtos_mutex_lock(tx_desc_free_mutex, -1);
    desc = (struct fhost_tx_desc_tag *)co_list_pop_front(&tx_desc_free_list);
    if (desc) {
        tx_desc_free_cnt--;
    }
    rtos_mutex_unlock(tx_desc_free_mutex);

    return desc;
}

uint32_t fhost_tx_free_list_cnt(bool lock)
{
    uint32_t cnt;

    if (lock) {
        rtos_mutex_lock(tx_desc_free_mutex, -1);
    }
    cnt = tx_desc_free_cnt;
    if (lock) {
        rtos_mutex_unlock(tx_desc_free_mutex);
    }

    return cnt;
}

void fhost_tx_desc_netbuf_free(struct fhost_tx_desc_tag *desc)
{
    struct hostdesc *p_hd = (struct hostdesc *)(&(desc->txdesc.host));
    net_buf_tx_t *net_buf = (net_buf_tx_t *)p_hd->packet_addr;

    if (!net_buf)
        return;

    fhost_tx_release_buf(net_buf, p_hd->cfm.status,
                         (cb_fhost_tx)p_hd->cfm_cb, p_hd->cfm_cb_arg);
    fhost_tx_enqueue_free_list(desc);
}

/**
 ****************************************************************************************
 * @brief Enable the TX queues for the given STA.
 * This function is called in the FHOST TX thread after getting the @ref FHOST_TX_STA_ADD
 * message that was sent from the control thread.
 *
 * @param[in] sta_id Station index to add
 ****************************************************************************************
 */
void fhost_tx_do_sta_add(uint8_t sta_id)
{
    int tid;
    //AIC_LOG_PRINTF("%s sta_id %d\r\n", __func__, sta_id);
    if (sta_id >= STA_MAX)
        return ;
    for (tid = 0; tid < TID_MAX; tid++)
    {
        struct fhost_tx_queue_tag *txq = &fhost_tx_env.tx_q[sta_id * TID_MAX + tid];

        // TX queue will now be enabled
        txq->status = TXQ_ENABLED;
        txq->credits = NX_DEFAULT_TX_CREDIT_CNT;
        //AIC_LOG_PRINTF("%s %d %d, txq %x %x\r\n", __func__, sta_id, tid, txq, txq->status);
    }
#if NX_BEACONING
    fhost_tx_env.traffic[sta_id].ps_ready[PS_LEGACY] = -1;
    fhost_tx_env.traffic[sta_id].ps_ready[PS_UAPSD] = -1;
    fhost_tx_env.traffic[sta_id].sp_cnt[PS_LEGACY] = 0;
    fhost_tx_env.traffic[sta_id].sp_cnt[PS_UAPSD] = 0;
#endif
}

/**
 ****************************************************************************************
 * @brief Disable the TX queues for the given STA.
 * This function is called in the FHOST TX thread after getting the @ref FHOST_TX_STA_DEL
 * message that was sent from the control thread. It goes through all the TX queues
 * attached to this STA and free the packets pending in those queues.
 *
 * @param[in] sta_id Station index to delete
 ****************************************************************************************
 */
void fhost_tx_do_sta_del(uint8_t sta_id)
{
    struct fhost_tx_desc_tag *desc = NULL;
    int tid;
    for (tid = 0; tid < TID_MAX; tid++)
    {
        struct fhost_tx_queue_tag *txq = fhost_tx_get_txq(INVALID_VIF_IDX, sta_id, tid);

        // Go through the TX queue and free all the packets
        desc = fhost_tx_dequeue(txq);
        while (desc)
        {
            fhost_tx_desc_netbuf_free(desc);
            desc = fhost_tx_dequeue(txq);
        }
        int ac = mac_tid2ac[tid];
        if(txq->status & TXQ_IN_HWQ_LIST)
            fhost_tx_del_txq_from_sched(txq, &fhost_tx_env.sched[ac]);

        // TX queue is now disabled
        txq->status = 0;
    }
    desc = fhost_tx_dequeue_cfm_list();
    while (desc)
    {
        fhost_tx_desc_netbuf_free(desc);
        desc = fhost_tx_dequeue_cfm_list();
    }
}

void fhost_tx_flush_txq(uint8_t sta_id)
{
    struct fhost_tx_desc_tag *desc = NULL;
    int tid;
    for (tid = 0; tid < TID_MAX; tid++) {

        struct fhost_tx_queue_tag *txq = fhost_tx_get_txq(INVALID_VIF_IDX, sta_id, tid);

        while (desc = fhost_tx_dequeue(txq)) {
            fhost_tx_desc_netbuf_free(desc);
        }
    }
    while (desc = (struct fhost_tx_queue_tag *)fhost_tx_dequeue_cfm_list()) {
        fhost_tx_desc_netbuf_free(desc);
    }
}

/**
 ****************************************************************************************
 * @brief Enable the TX queues for the given VIF.
 *
 * This function is called in the FHOST TX thread after receiving the
 * @ref FHOST_TX_VIF_ENABLE message sent from the control thread.
 *
 * @param[in] vif_idx  MAC VIF index
 ****************************************************************************************
 */
static void fhost_tx_do_vif_enable(uint8_t vif_idx)
{
    struct fhost_tx_queue_tag *txq = fhost_tx_get_txq(vif_idx, INVALID_STA_IDX, 0);
    //AIC_LOG_PRINTF("fhost_tx_do_vif_enable %x\n", txq);
    if(txq) {
        txq->status = TXQ_ENABLED;
        txq->credits = NX_DEFAULT_TX_CREDIT_CNT;
        #if NX_BEACONING
        txq->nb_ready = 0;
        #endif
    }

}

/**
 ****************************************************************************************
 * @brief Disable the TX queues for the given VIF.
 *
 * This function is called in the FHOST TX thread after receiving the
 * @ref FHOST_TX_VIF_DISABLE message sent from the control thread.
 *
 * @param[in] vif_idx  MAC VIF index
 ****************************************************************************************
 */
void fhost_tx_do_vif_disable(uint8_t vif_idx)
{
    struct fhost_tx_queue_tag *txq = fhost_tx_get_txq(vif_idx, INVALID_STA_IDX, 0);
    struct fhost_tx_desc_tag *desc;

    // Go through the TX queue and free all the packets
    desc = fhost_tx_dequeue(txq);
    while (desc)
    {
        fhost_tx_release_buf((net_buf_tx_t *)desc->txdesc.host.packet_addr, 0, desc->txdesc.host.cfm_cb,
                             desc->txdesc.host.cfm_cb_arg);
        desc = fhost_tx_dequeue(txq);
    }

    txq->status = 0;
}

void fhost_txq_vif_stop(uint8_t sta_id, uint16_t reason)
{
    struct fhost_tx_queue_tag *txq = fhost_tx_get_txq(INVALID_VIF_IDX, sta_id, 0);
    int tid, ac;
    aic_dbg("%s, %x\r\n", __func__, reason);

    FHOST_TXQ_LOCK();
    for (tid = 0; tid < TID_MAX; tid++, txq++)
    {
        ac = mac_tid2ac[tid];
        txq->status |= reason;
        if (txq->status & TXQ_IN_HWQ_LIST)
            fhost_tx_del_txq_from_sched(txq, &fhost_tx_env.sched[ac]);
    }
    FHOST_TXQ_UNLOCK();
    //AIC_LOG_PRINTF("%s, txq %x %x\r\n", __func__, txq, txq->status);
}

void fhost_txq_vif_start(uint8_t sta_id, uint16_t reason)
{
    struct fhost_tx_queue_tag *txq = fhost_tx_get_txq(INVALID_VIF_IDX, sta_id, 0);
    int tid, ac, ac_to_sched = 0;

    aic_dbg("%s, %x\r\n", __func__, reason);
    FHOST_TXQ_LOCK();
    for (tid = 0; tid < TID_MAX; tid++, txq++)
    {
        ac = mac_tid2ac[tid];
        txq->status &= ~reason;

        if (!(txq->status & TXQ_STOP))
        {
            fhost_tx_add_txq_to_sched(txq, &fhost_tx_env.sched[ac]);
            ac_to_sched |= CO_BIT(ac);
        }
    }
    //aic_dbg("%s, ac_to_sched %x\r\n", __func__, ac_to_sched);

    for (ac = AC_VO; ac >= AC_BK; ac--)
    {
        if (ac_to_sched & CO_BIT(ac)) {
            fhost_tx_schedule(ac);
        }
    }
    FHOST_TXQ_UNLOCK();
}

/**
 ****************************************************************************************
 * @brief Stop TX queues for a STA that enabled PS mode.
 *
 * @param[in] sta_id  Index of the station
 ****************************************************************************************
 */
static void fhost_tx_do_sta_enter_ps(uint8_t sta_id)
{
    #if NX_BEACONING
    struct fhost_tx_queue_tag *txq = fhost_tx_get_txq(INVALID_VIF_IDX, sta_id, 0);
    //aic_dbg("Ps %d, %d\r\n", sta_id, rtos_now(false));
    struct fhost_tx_sta_traffic *tfc = &fhost_tx_env.traffic[sta_id];
    int tid, ac, ps;

    for (ps = PS_LEGACY; ps < PS_TYPE_CNT; ps++)
    {
        tfc->ps_ready[ps] = 0;
        tfc->sp_cnt[ps] = 0;
    }

    FHOST_TXQ_LOCK();
    for (tid = 0; tid < TID_MAX; tid++, txq++)
    {
        ac = mac_tid2ac[tid];
        txq->status |= TXQ_STOP_STA_PS;
        //GLOBAL_INT_DISABLE();
        if (txq->status & TXQ_IN_HWQ_LIST)
            fhost_tx_del_txq_from_sched(txq, &fhost_tx_env.sched[ac]);
        //GLOBAL_INT_RESTORE();
        tfc->ps_ready[txq->ps_type] += txq->nb_ready;
    }
    FHOST_TXQ_UNLOCK();

    for (ps = PS_LEGACY; ps < PS_TYPE_CNT; ps++)
    {
        if (tfc->ps_ready[ps])
            fhost_tx_ps_traffic_ind(sta_id, true, ps);
    }
    #endif
}


/**
 ****************************************************************************************
 * @brief Restart TX queues for a STA that disabled PS mode.
 *
 * @param[in] sta_id  Index of the station
 ****************************************************************************************
 */
static void fhost_tx_do_sta_exit_ps(uint8_t sta_id)
{
    #if NX_BEACONING
    struct fhost_tx_queue_tag *txq = fhost_tx_get_txq(INVALID_VIF_IDX, sta_id, 0);
    //aic_dbg("Wk %d, %d\r\n", sta_id, rtos_now(false));
    struct fhost_tx_sta_traffic *tfc = &fhost_tx_env.traffic[sta_id];
    int tid, ac, ps, ac_to_sched = 0;

    for (ps = PS_LEGACY; ps < PS_TYPE_CNT; ps++)
    {
        if (tfc->ps_ready[ps])
            fhost_tx_ps_traffic_ind(sta_id, false, ps);
        tfc->ps_ready[ps] = -1;
        tfc->sp_cnt[ps] = 0;
    }
    FHOST_TXQ_LOCK();
    for (tid = 0; tid < TID_MAX; tid++, txq++)
    {
        ac = mac_tid2ac[tid];
        txq->status &= ~TXQ_STOP_STA_PS;
        txq->limit = 0;

        if (!(txq->status & TXQ_STOP) && txq->nb_ready)
        {
            fhost_tx_add_txq_to_sched(txq, &fhost_tx_env.sched[ac]);
            ac_to_sched |= CO_BIT(ac);
        }
    }

    for (ac = AC_VO; ac >= AC_BK; ac--)
    {
        if (ac_to_sched & CO_BIT(ac)) {
            fhost_tx_schedule(ac);
        }
    }
    FHOST_TXQ_UNLOCK();
    #endif
}
int fhost_tx_sta_ps_enable(uint8_t sta_id, bool enable)
{
    struct fhost_tx_queue_tag *txq;

    if (sta_id >= STA_MAX)
        return -1;

    //aic_dbg("Ps, %d, %d\r\n", sta_id, enable);

    if(sta_id < NX_REMOTE_STA_MAX) {
        //aic_dbg("P %d, %d\r\n", enable, sys_now());
        struct sta_info_tag *sta = vif_mgmt_get_sta_by_staid(sta_id);
        if (sta)
            sta->last_active_time_us = rtos_now(0);
    }

    txq = fhost_tx_get_txq(INVALID_VIF_IDX, sta_id, 0);
    if (!txq || !(txq->status & TXQ_ENABLED))
        return -1;

    if (enable) {
        fhost_tx_do_sta_enter_ps(sta_id);
    } else {
        fhost_tx_do_sta_exit_ps(sta_id);
    }

    return 0;
}

/**
 ****************************************************************************************
 * @brief Restart TX queues for a STA that disabled PS mode.
 *
 * @param[in] sta_id   Index of the station
 * @param[in] req_cnt  Number of packet requested (0 means all)
 * @param[in] ps_type  Type of PS traffic requested
 ****************************************************************************************
 */
void fhost_tx_do_ps_traffic_req(uint8_t sta_id, int req_cnt,
                                       enum fhost_tx_ps_type ps_type)
{
    #if NX_BEACONING
    struct fhost_tx_queue_tag *txq = fhost_tx_get_txq(INVALID_VIF_IDX, sta_id, TID_MGT);
    struct fhost_tx_sta_traffic *tfc = &fhost_tx_env.traffic[sta_id];
    int tid, sp_size = 0, ac, ac_to_sched = 0;

    if (tfc->ps_ready[ps_type] == -1)
    {
        // ignore traffic request if STA is not in PS mode
        //ASSERT_WARN(0);
        return;
    }
    ASSERT_ERR(tfc->sp_cnt[ps_type] == 0);

    FHOST_TXQ_LOCK();
    for (tid = TID_MGT; tid >= 0; tid--, txq--)
    {
        if (!txq->nb_ready || (ps_type != txq->ps_type))
            continue;

        if (!req_cnt || (txq->nb_ready < req_cnt))
            txq->limit = txq->nb_ready;
        else
            txq->limit = req_cnt;

        if (sta_id < NX_REMOTE_STA_MAX)
            ac = mac_tid2ac[tid];
        else
            ac = AC_VO;//AC_MAX; // use BCN queue for broadcast/multicast sta

        if ((txq->status & TXQ_STOP) == TXQ_STOP_STA_PS)
        {
            fhost_tx_add_txq_to_sched(txq, &fhost_tx_env.sched[ac]);
            ac_to_sched |= CO_BIT(ac);
        }

        sp_size += txq->limit;
        if (req_cnt)
        {
            req_cnt -= txq->limit;
            if (!req_cnt)
                break;
        }
    }

    tfc->sp_cnt[ps_type] = sp_size;
    tfc->ps_ready[ps_type] -= sp_size;

    for (ac = AC_MAX; ac >= AC_BK; ac--)
    {
        if (ac_to_sched & CO_BIT(ac))
        {
            fhost_tx_schedule(ac);
        }
    }
    FHOST_TXQ_UNLOCK();
    #endif
}

int fhost_tx_init(void)
{
    RWNX_DBG(RWNX_FN_ENTRY_STR);
    uint16_t i = 0;

    // Reset the environment
    memset(&fhost_tx_env, 0, sizeof(fhost_tx_env));

    // Initial free tx desc
    if (rtos_mutex_create(&tx_desc_free_mutex, "tx_desc_free_mutex")) {
        return 1;
    }
    co_list_init(&tx_desc_free_list);
    tx_desc_free_cnt = 0;
    for(i = 0; i < TXBUF_CNT; i++)
    {
        co_list_push_back(&tx_desc_free_list, (struct co_list_hdr *)(&tx_desc_array[i]));
        tx_desc_free_cnt++;
    }

    if (rtos_mutex_create(&tx_desc_cfm_mutex, "tx_desc_cfm_mutex")) {
        return 2;
    }
    co_list_init(&tx_desc_cfm_list);
    tx_desc_cfm_cnt = 0;

    // Create the TX mutex
    if (rtos_mutex_create(&fhost_tx_env.tx_lock, "fhost_tx_env.tx_lock"))
    {
        return 3;
    }
    #if 1
    // Create the TXQ mutex
    if (rtos_mutex_create(&fhost_tx_env.txq_lock, "fhost_tx_env.txq_lock"))
    {
        return 4;
    }
    #endif

    #ifdef CONFIG_FHOST_TX_SCHEDULE_SEPERATE
    #ifdef CONFIG_FHOST_TX_AC_SCHEDULE
    // Create the TX AC mutex
    if (rtos_mutex_create(&fhost_tx_env.tx_ac_lock, "fhost_tx_env.tx_ac_lock"))
    {
        return 5;
    }

    for (int ac = AC_BK; ac < AC_MAX; ac++) {
        co_list_init(&fhost_tx_env.tx_ac[ac]);
    }
    #else
    co_list_init(&fhost_tx_list);
    #endif
    #endif

    return 0;
}

void fhost_tx_deinit(void)
{
    int i;
    struct fhost_tx_desc_tag *desc;
    for (i = 0; i < TXBUF_CNT; i++) {
        desc = fhost_tx_dequeue_free_list();
        if (!desc)
            desc = NULL;
    }
    if (tx_desc_free_mutex) {
        rtos_mutex_delete(tx_desc_free_mutex);
        tx_desc_free_mutex = NULL;
    }
    if (tx_desc_cfm_mutex) {
        rtos_mutex_delete(tx_desc_cfm_mutex);
        tx_desc_cfm_mutex = NULL;
    }
    if (fhost_tx_env.tx_lock)
        rtos_mutex_delete(fhost_tx_env.tx_lock);
    if (fhost_tx_env.txq_lock)
        rtos_mutex_delete(fhost_tx_env.txq_lock);
}

int fhost_tx_start(net_if_t *net_if, net_buf_tx_t *net_buf,
                   cb_fhost_tx cfm_cb, void *cfm_cb_arg)
{
    FHOST_TX_LOCK();
    fhost_tx_req(net_if, net_buf, IEEE802_3, cfm_cb, cfm_cb_arg);
    FHOST_TX_UNLOCK();

    return 0;
}

static void fhost_tx_task_func(void *param)
{
    struct fhost_vif_tag *fhost_vif;
    net_buf_tx_t *net_buf;
    uint32_t msg;
    uint8_t sta_id, tid, vif_index;
    int ret = 0;
    #ifdef CONFIG_FHOST_TX_TASK_TS
    uint32_t start_time = 0;
    uint32_t end_time = 0;
    static volatile uint8_t tx_count = 0;
    #endif

    fhost_vif = &fhost_env.vif[0];

    for (;;) {
#if defined(CONFIG_FHOST_TX_SCHEDULE_SEPERATE) && defined(CONFIG_FHOST_TX_AC_SCHEDULE)
        ret = rtos_semaphore_wait(fhost_tx_task_sema, -1);
        #ifdef CONFIG_FHOST_TX_TASK_TS
        tx_count++;
        start_time = (uint32_t)rtos_now(false);
        aic_dbg("txin:%u/%u\n", start_time, tx_count);
        #endif

        if (0 == ret) {
            fhost_tx_ac_schedule();
        }
#else
        #if FHOST_TX_MSG_USE_QUEUE
        ret = rtos_queue_read(fhost_tx_task_queue, &msg, -1, false);
        #else
        ret = rtos_signal_recv(fhost_tx_task, &msg, 4);
        #endif

        #ifdef CONFIG_FHOST_TX_TASK_TS
        tx_count++;
        start_time = (uint32_t)rtos_now(false);
        aic_dbg("txin:%u/%u\n", start_time, tx_count);
        #endif
        sta_id    = (uint8_t)(msg >> 16);
        vif_index = (uint8_t)(msg >> 8);
        tid       = (uint8_t)(msg);
        //AIC_LOG_PRINTF("##txQ %x sta_id %x %d %d\r\n", msg, sta_id, vif_index, tid);
        #if 1
        struct fhost_tx_queue_tag *txq = fhost_tx_get_txq(vif_index, sta_id, tid);
        int ac, ac_to_sched = 0;

        //AIC_LOG_PRINTF("##txQ cnt %d\r\n",  co_list_cnt(&txq->ready));

        FHOST_TXQ_LOCK();
        //for (tid = 0; tid < TID_MAX; tid++, txq++)
        {
            ac = mac_tid2ac[tid];

            if (!(txq->status & TXQ_STOP))
            {
                fhost_tx_add_txq_to_sched(txq, &fhost_tx_env.sched[ac]);
                ac_to_sched |= CO_BIT(ac);
            }
        }
        //aic_dbg("txq ac_to_sched %x txq %x\r\n", ac_to_sched, txq);

        //for (ac = AC_VO; ac >= AC_BK; ac--)
        {
            if (ac_to_sched & CO_BIT(ac)) {
                fhost_tx_schedule(ac);
            }
        }
        FHOST_TXQ_UNLOCK();

        #ifdef CONFIG_FHOST_TX_SCHEDULE_SEPERATE
        fhost_tx_schedule2();
        #endif
        #endif
        #if 0
        net_buf = (net_buf_tx_t *)msg;
        if (fhost_vif->mac_vif) {
            fhost_tx_start(&fhost_vif->net_if, net_buf, NULL, NULL);
        }
        #endif
#endif

        #ifdef CONFIG_FHOST_TX_TASK_TS
        end_time = (uint32_t)rtos_now(false);
        aic_dbg("txout:%u/%u\n", end_time, tx_count);
        #endif

    }
exit:
    aic_dbg("Exit hostif_tx_task\r\n");
    if (fhost_tx_task) {
        rtos_task_delete(fhost_tx_task);
        fhost_tx_task = NULL;
    }
}

#ifdef CONFIG_OFFCHANNEL
struct fhost_tx_queue_tag* fhost_txq_offchan_init(void)
{
    printk("%s in\n", __func__);
    struct fhost_tx_queue_tag *txq = &fhost_tx_env.tx_q[FHOST_OFF_CHAN_TXQ_IDX];

    //tx queue will now be enabled
    txq->status = TXQ_ENABLED;
    txq->credits = NX_DEFAULT_TX_CREDIT_CNT;

    return txq;
}

void fhost_txq_offchan_deinit(struct fhost_tx_queue_tag* txq)
{
    printk("%s in\n", __func__);
    int off_chan_txq_idx = FHOST_OFF_CHAN_TXQ_IDX;
    struct fhost_tx_desc_tag *desc = NULL;

    desc = fhost_tx_dequeue(txq);
    while (desc)
    {
        fhost_tx_desc_netbuf_free(desc);
        desc = fhost_tx_dequeue(txq);
    }
    if (txq->status & TXQ_IN_HWQ_LIST)
        fhost_tx_del_txq_from_sched(txq, &fhost_tx_env.sched[TID_MAX - 1]);

    //tx queue is now disabled
    txq->status = 0;

    #if 1//doubt
    desc = fhost_tx_dequeue_cfm_list();
    while (desc)
    {
        fhost_tx_desc_netbuf_free(desc);
        desc = fhost_tx_dequeue_cfm_list();
    }
    #endif
    return;
}
#endif

int fhost_tx_task_init(void)
{
    RWNX_DBG(RWNX_FN_ENTRY_STR);

    if (fhost_tx_task_queue) {
        aic_dbg("Error: %s\r\n", __func__);
        return -1;
    }
    #if 1
    #if FHOST_TX_MSG_USE_QUEUE
    if (rtos_queue_create(sizeof(uint32_t), 60, &fhost_tx_task_queue, "fhost_tx_task_queue")) {
        aic_dbg("fhost_tx_task_queue fail\r\n");
        return 1;
    }
    #endif

    #if defined(CONFIG_FHOST_TX_SCHEDULE_SEPERATE) && defined(CONFIG_FHOST_TX_AC_SCHEDULE)
    if (rtos_semaphore_create(&fhost_tx_task_sema, "fhost_tx_task_sema", 0x7FFFFFFF, 0)) {
        aic_dbg("fhost_tx_task_sema fail\r\n");
        return 3;
    }
    #endif

    if (rtos_task_create(fhost_tx_task_func, "Fhost TX task", FHOST_TX_TASK,
                         fhost_tx_stack_size, NULL, fhost_tx_priority, &fhost_tx_task)) {
        aic_dbg("Fhost TX task fail\r\n");
        return 2;
    }
    #endif
    return 0;
}

void fhost_tx_task_deinit(void)
{
    uint32_t msg;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    if (fhost_tx_task) {
        rtos_task_delete(fhost_tx_task);
        fhost_tx_task = NULL;
    }

    if (fhost_tx_task_queue) {
        // flush tx queue
        if (rtos_queue_cnt(fhost_tx_task_queue) > 0) {
            AIC_LOG_PRINTF("fhost_tx_task_queue cnt:%d\n", rtos_queue_cnt(fhost_tx_task_queue));
        }
        while (!rtos_queue_is_empty(fhost_tx_task_queue)) {
            rtos_queue_read(fhost_tx_task_queue, &msg, 30, false);
            AIC_LOG_PRINTF("fhost_tx_task_queue msg:%X\n", msg);
        }
        rtos_queue_delete(fhost_tx_task_queue);
        fhost_tx_task_queue = NULL;
    }

    #ifdef CONFIG_FHOST_TX_SCHEDULE_SEPERATE
    #ifdef CONFIG_FHOST_TX_AC_SCHEDULE
    fhost_tx_ac_flush();

    if (fhost_tx_task_sema != NULL) {
        rtos_semaphore_delete(fhost_tx_task_sema);
        fhost_tx_task_sema = NULL;
    #else
    fhost_tx_list_flush();
    #endif
    #endif
    }

}

void fhost_tx_vif_txq_enable(struct fhost_vif_tag *fhost_vif)
{
    //struct fhost_tx_msg_tag msg;

    if (!fhost_vif || ! fhost_vif->mac_vif)
        return;

    // vif TXQ is used to send data to 'unknown' STA
    // AP: For all frames generated by wpa_supplicant (probe resp, auth ...)
    // STA: Needed for external authentication only
    // MONITOR: Needed by some external OS framework
    if ((fhost_vif->mac_vif->type != VIF_MONITOR) &&
        (fhost_vif->mac_vif->type != VIF_AP) &&
        (fhost_vif->mac_vif->type != VIF_STA))
        return;

    fhost_tx_do_vif_enable(fhost_vif->mac_vif->index);
}

void fhost_tx_vif_txq_disable(struct fhost_vif_tag *fhost_vif)
{
    //struct fhost_tx_msg_tag msg;

    if (!fhost_vif || ! fhost_vif->mac_vif)
        return;

    if ((fhost_vif->mac_vif->type != VIF_MONITOR) &&
        (fhost_vif->mac_vif->type != VIF_AP) &&
        (fhost_vif->mac_vif->type != VIF_STA))
        return;

    //msg.msg_id = FHOST_TX_VIF_DISABLE;
    //msg.u.vif.vif_idx = fhost_vif->mac_vif->index;

    // Push the message to the queue
    //rtos_queue_write(fhost_tx_env.queue_msg, &msg, -1, false);
}

uint32_t fhost_send_80211_frame(int fvif_idx, const uint8_t *frame, uint32_t length,
                                cb_fhost_tx cfm_cb, void *cfm_cb_arg)
{
    struct fhost_vif_tag *fhost_vif;
    net_buf_tx_t *net_buf;

    if (fvif_idx >= NX_VIRT_DEV_MAX)
        return 0;

    fhost_vif = &fhost_env.vif[fvif_idx];
    if (!fhost_vif->mac_vif)
        return 0;

    net_buf = net_buf_tx_alloc(frame, length);
    if (net_buf == NULL)
        return 0;

    FHOST_TX_LOCK();
    fhost_tx_req(&fhost_vif->net_if, net_buf, IEEE802_11, cfm_cb, cfm_cb_arg);
    FHOST_TX_UNLOCK();

    return (uint32_t)net_buf;
}

void fhost_tx_cfm_push(uint32_t *data)
{
    uint32_t statinfo = (uint8_t)data[0];
    FHOST_TX_LOCK();
    fhost_tx_cfm(statinfo);
    FHOST_TX_UNLOCK();
}
/// @}
