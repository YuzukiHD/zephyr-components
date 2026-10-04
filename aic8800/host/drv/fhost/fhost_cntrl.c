 /**
 ****************************************************************************************
 *
 * @file fhost_cntrl.c
 *
 * @brief Definition of control task for Fully Hosted firmware.
 *
 * Copyright (C) RivieraWaves 2017-2019
 *
 ****************************************************************************************
 */

#include <string.h>
#include "lwip/sockets.h"
#include "fhost.h"
#include "fhost_cntrl.h"
#include "fhost_rx.h"
#include "fhost_tx.h"
#include "fhost_config.h"
#include "lmac_msg.h"
#include "co_utils.h"
#include "mac_frame.h"
#include "rwnx_msg_tx.h"
#include "rwnx_utils.h"
#include "wifi.h"
#include "rwnx_main.h"

/**
 ****************************************************************************************
 * @addtogroup FHOST_CNTRL
 * @{
 ****************************************************************************************
 */

#define FHOST_CNTRL_MSG_USE_QUEUE 1

/// Control task handle
static rtos_task_handle wifi_cntrl_task = NULL;
/// Master message queue
static rtos_queue fhost_cntrl_queue = NULL;

/// KE message queue size
#define FHOST_CNTRL_QUEUE_KE_MSG_SIZE 5

/// Master message queue size
#define FHOST_CNTRL_QUEUE_SIZE (FHOST_CNTRL_QUEUE_KE_MSG_SIZE + 55)

/// CFGRWNX response queue size
#define FHOST_CNTRL_QUEUE_CFGRWNX_RESP_SIZE 5

/// Number of UDP sockets for CFGRWNX connections
#define FHOST_CNTRL_MAX_LINK 3
typedef uint16_t ke_task_id_t;
typedef uint16_t ke_msg_id_t;

static rtos_semaphore fhost_cntrl_task_exit_sem = NULL;
static bool fhost_cntrl_task_exit_flag = false;

struct rwnx_hw *cntrl_rwnx_hw = NULL;

rtos_mutex cfgrwnx_link_queue_lock = NULL;

int fhost_cntrl_scan_cancel_req(void);

static inline u8_l get_chan_flags(uint32_t flags)
{
	u8_l chan_flags = 0;
#ifdef CONFIG_RADAR_OR_IR_DETECT
	if (flags & CHAN_NO_IR)
		chan_flags |= CHAN_NO_IR;
	if (flags & CHAN_RADAR)
		chan_flags |= CHAN_RADAR;
#endif
	return chan_flags;
}

/**
 ****************************************************************************************
 * @brief Process SCANU_RESULT_IND message
 *
 * Sent by firmware when a new bssid is found.
 *
 * @param[in] msgid   KE Message ID
 * @param[in] param   KE Message content
 * @param[in] dest_id KE task id that received this message
 * @param[in] src_id  KE task id that sent this message
 *
 * @return 0 on success and !=0 if error occurred
 ****************************************************************************************
 */
 #if 0
static int fhost_cntrl_ke_msg_scan_result_ind(ke_msg_id_t const msgid,
                                              struct rxu_mgt_ind const *param,
                                              ke_task_id_t const dest_id,
                                              ke_task_id_t const src_id)
{
    struct cfgrwnx_scan_result result;
    struct fhost_vif_tag *fhost_vif = fhost_from_mac_vif(param->inst_nbr);

    result.hdr.id = CFGRWNX_SCAN_RESULT_EVENT;
    result.hdr.len = sizeof(struct cfgrwnx_scan_result) +
        CO_ALIGN4_HI(param->length);
    result.hdr.split = 1;

    result.fhost_vif_idx = CO_GET_INDEX(fhost_vif, fhost_env.vif);
    result.freq = param->center_freq;
    result.rssi = param->rssi;
    result.length = param->length;

    fhost_cntrl_cfgrwnx_event_send(&result, sizeof(struct cfgrwnx_scan_result), false, fhost_vif->scan_sock);
    fhost_cntrl_cfgrwnx_event_send(param->payload, CO_ALIGN4_HI(param->length), true, fhost_vif->scan_sock);

    return 0;
}

/**
 ****************************************************************************************
 * @brief Process SCANU_START_CFM message
 *
 * Sent by firmware when the scan is done.
 *
 * @param[in] msgid   KE Message ID
 * @param[in] param   KE Message content
 * @param[in] dest_id KE task id that received this message
 * @param[in] src_id  KE task id that sent this message
 *
 * @return 0 on success and !=0 if error occurred
 ****************************************************************************************
 */
static int fhost_cntrl_ke_msg_scan_cfm(ke_msg_id_t const msgid,
                                       struct scanu_start_cfm const *param,
                                       ke_task_id_t const dest_id,
                                       ke_task_id_t const src_id)
{
    struct cfgrwnx_resp resp;
    struct fhost_vif_tag *fhost_vif = fhost_from_mac_vif(param->vif_idx);

    resp.hdr.id = CFGRWNX_SCAN_DONE_EVENT;
    resp.hdr.len = sizeof(struct cfgrwnx_resp);
    resp.hdr.split = 0;

    if (param->status == CO_OK)
        resp.status = CFGRWNX_SUCCESS;
    else
        resp.status = CFGRWNX_ERROR;

    fhost_cntrl_cfgrwnx_event_send(&resp, resp.hdr.len, false, fhost_vif->scan_sock);

    return 0;
}
/**
 ****************************************************************************************
 * @brief Process SM_CONNECT_IND message
 *
 * Sent by firmware when the connection is done
 *
 * @param[in] msgid   KE Message ID
 * @param[in] param   KE Message content
 * @param[in] dest_id KE task id that received this message
 * @param[in] src_id  KE task id that sent this message
 *
 * @return 0 on success and !=0 if error occurred
 ****************************************************************************************
 */
static int fhost_cntrl_ke_msg_connect_ind(ke_msg_id_t const msgid,
                                          struct sm_connect_ind const *param,
                                          ke_task_id_t const dest_id,
                                          ke_task_id_t const src_id)
{
    struct cfgrwnx_connect_event event;
    struct fhost_vif_tag *fhost_vif = fhost_from_mac_vif(param->vif_idx);

    event.hdr.id = CFGRWNX_CONNECT_EVENT;
    event.hdr.len = sizeof(event) + param->assoc_req_ie_len + param->assoc_rsp_ie_len;

    MAC_ADDR_CPY(&event.bssid, &param->bssid);
    event.status_code = param->status_code;
    event.freq = param->center_freq;
    event.assoc_req_ie_len = param->assoc_req_ie_len;
    event.assoc_resp_ie_len = param->assoc_rsp_ie_len;
    event.sta_idx = param->ap_idx;

    if (param->assoc_req_ie_len + param->assoc_rsp_ie_len)
    {
        event.hdr.split = 1;
        fhost_cntrl_cfgrwnx_event_send(&event, sizeof(event), false, fhost_vif->conn_sock);
        fhost_cntrl_cfgrwnx_event_send(param->assoc_ie_buf,
                                       param->assoc_req_ie_len + param->assoc_rsp_ie_len,
                                       true, fhost_vif->conn_sock);
    }
    else
    {
        event.hdr.split = 0;
        fhost_cntrl_cfgrwnx_event_send(&event, sizeof(event), false, fhost_vif->conn_sock);
    }

    if (param->status_code == MAC_ST_SUCCESSFUL)
    {
        fhost_vif->ap_id = param->ap_idx;
        fhost_vif->acm = param->acm;

        fhost_tx_do_sta_add(fhost_vif->ap_id);

        net_if_up(&fhost_vif->net_if);
    }
    return 0;
}

/**
 ****************************************************************************************
 * @brief Process SM_DISCONNECT_IND message
 *
 * Sent by firmware when the connection is lost. (After a disconnection request, a
 * disconnection from the AP or a lost link)
 *
 * @param[in] msgid   KE Message ID
 * @param[in] param   KE Message content
 * @param[in] dest_id KE task id that received this message
 * @param[in] src_id  KE task id that sent this message
 *
 * @return 0 on success and !=0 if error occurred
 ****************************************************************************************
 */
static int fhost_cntrl_ke_msg_disconnection_ind(ke_msg_id_t const msgid,
                                                struct sm_disconnect_ind const *param,
                                                ke_task_id_t const dest_id,
                                                ke_task_id_t const src_id)
{
    struct cfgrwnx_disconnect_event event;
    struct fhost_vif_tag *fhost_vif = fhost_from_mac_vif(param->vif_idx);

    event.hdr.id = CFGRWNX_DISCONNECT_EVENT;
    event.hdr.len = sizeof(event);
    event.hdr.split = 0;

    event.fhost_vif_idx = CO_GET_INDEX(fhost_vif, fhost_env.vif);
    event.reason_code = param->reason_code;

    net_if_down(&fhost_vif->net_if);

    fhost_cntrl_cfgrwnx_event_send(&event, event.hdr.len, false, fhost_vif->conn_sock);
    fhost_vif->conn_sock = -1;

    fhost_vif = &fhost_env.vif[param->vif_idx];
    //fhost_tx_sta_del(fhost_vif->ap_id);
    fhost_tx_do_sta_add(fhost_vif->ap_id);

    return 0;
}

/**
 ****************************************************************************************
 * @brief Process ME_TKIP_MIC_FAILURE_IND message
 *
 * Sent by firmware when a TKIP MIC failure is detected on a received packet.
 *
 * @param[in] msgid   KE Message ID
 * @param[in] param   KE Message content
 * @param[in] dest_id KE task id that received this message
 * @param[in] src_id  KE task id that sent this message
 *
 * @return 0 on success and !=0 if error occurred
 ****************************************************************************************
 */
static int fhost_cntrl_ke_msg_mic_failure_ind(ke_msg_id_t const msgid,
                                              struct me_tkip_mic_failure_ind const *param,
                                              ke_task_id_t const dest_id,
                                              ke_task_id_t const src_id)
{
    #if 0
    struct cfgrwnx_mic_failure_event event;
    struct fhost_vif_tag *fhost_vif = fhost_from_mac_vif(param->vif_idx);

    event.hdr.id = CFGRWNX_MIC_FAILURE_EVENT;
    event.hdr.len = sizeof(event);
    event.hdr.split = 0;

    MAC_ADDR_CPY(&event.addr, &param->addr);
    event.ga = param->ga;
    event.fhost_vif_idx = CO_GET_INDEX(fhost_vif, fhost_env.vif);

    fhost_cntrl_cfgrwnx_event_send(&event, event.hdr.len, false, fhost_vif->conn_sock);
    #endif
    return 0;
}

/**
 ****************************************************************************************
 * @brief Process ME_TX_CREDITS_UPDATE_IND message
 *
 * Sent by firmware when the a BlockAck agreement is established/deleted
 *
 * @param[in] msgid   KE Message ID
 * @param[in] param   KE Message content
 * @param[in] dest_id KE task id that received this message
 * @param[in] src_id  KE task id that sent this message
 *
 * @return 0 on success and !=0 if error occurred
 ****************************************************************************************
 */
//static int fhost_cntrl_ke_msg_credits_update_ind(ke_msg_id_t const msgid,
//                                                 struct me_tx_credits_update_ind const *param,
//                                                 ke_task_id_t const dest_id,
//                                                 ke_task_id_t const src_id)
///{
    //fhost_tx_credits_update(param->sta_idx, param->tid, param->credits);

//    return 0;
//}

/// Handlers function for KE indication message
//static const struct ke_msg_handler fhost_cntrl_ke_msg_handlers[] =
//{
   // {SCANU_RESULT_IND, (ke_msg_func_t)fhost_cntrl_ke_msg_scan_result_ind},
   // {SCANU_START_CFM, (ke_msg_func_t)fhost_cntrl_ke_msg_scan_cfm},
   // {SM_CONNECT_IND, (ke_msg_func_t)fhost_cntrl_ke_msg_connect_ind},
   // {SM_DISCONNECT_IND, (ke_msg_func_t)fhost_cntrl_ke_msg_disconnection_ind},
    //{ME_TX_CREDITS_UPDATE_IND, (ke_msg_func_t)fhost_cntrl_ke_msg_credits_update_ind},
    //{ME_TKIP_MIC_FAILURE_IND, (ke_msg_func_t)fhost_cntrl_ke_msg_mic_failure_ind},
//    {0,NULL}
//};


/**
 ****************************************************************************************
 * @brief Read message for KE message queue and process it
 *
 * All pending message in the KE message queue and read and processed.
 * The message is freed once processed.
 * Read is done without blocking, so that function returned after processing the last
 * message.
 *
 ****************************************************************************************
 */
static void fhost_cntrl_ke_msg_read_n_process(void)
{
    struct fhost_msg msg;
    struct ke_msg *ke_msg;

    #if 0
    while (!rtos_queue_read(queue_ke_msg, &msg, 0, false))
    {
        ke_msg = msg.data;
        fhost_cntrl_ke_msg_process(ke_msg);
        ke_msg_free(ke_msg);
    }
    #endif
}

/**
 ****************************************************************************************
 * @brief Wait for specific response from wifi task
 *
 * Calling this function will block the control task until it receives the expected msg.
 * The task waits on th KE message queue and then ignore messages received on the master
 * queue. Other KE messages, if any, are processed.
 * Once the message is received its parameters are copied into @p resp buffer and the
 * message is freed.
 *
 * @param[in]  msg_id Id of the expected message
 * @param[out] resp   Buffer to copy message parameters into (may be NULL)
 * @param[in]  len    Size of the buffer pointed by @p resp
 ****************************************************************************************
 */
static void fhost_cntrl_ke_msg_wait(int msg_id, void *resp, int len)
{
    struct fhost_msg msg;
    struct ke_msg *ke_msg;
    bool found = false;
    #if 0
    while (!found)
    {
        rtos_queue_read(queue_ke_msg, &msg, -1, false);
        ke_msg = msg.data;

        if (ke_msg->id == msg_id)
        {
            found = true;
            if (resp && len)
            {
                int copy = ke_msg->param_len;
                if (copy > len)
                {
                    /* must not happen */
                    ASSERT_WARN(0);
                    copy = len;
                }
                memcpy(resp, ke_msg->param, copy);
            }
        }
        else
        {
            fhost_cntrl_ke_msg_process(ke_msg);
        }
        ke_msg_free(ke_msg);
    }
    #endif
}
#endif

/****************************************************************************************
 * CFGRWNX MSG process
 ***************************************************************************************/
/// CFGRWNX handler description
struct fhost_cntrl_cfgrwnx_handler {
    /// message index
    int index;
    /// handler function
    void (*func) (void *msg);
};

/// CFGRWNX link parameters
struct fhost_cntrl_link cfgrwnx_link[FHOST_CNTRL_MAX_LINK] = {{NULL,},};

/**
 ****************************************************************************************
 * @brief Send a cfgrwnx response message to task that sent a command
 *
 * @param[in] msg     Pointer on buffer to send
 * @param[in] msg_len Size, in bytes, on the buffer
 * @param[in] resp_queue RTOS queue on which the response to the command shall be written
 ****************************************************************************************
 */
void fhost_cntrl_cfgrwnx_resp_send(void const *msg, int msg_len,
                                          rtos_queue resp_queue)
{
    void *msg_buf;

    // Allocate memory to copy the data, because the message memory used by the caller
    // is supposed to be re-usable immediately after the function returns
    msg_buf = rtos_malloc(msg_len);
    if (msg_buf == NULL)
    {
        aic_dbg("[CFGRWNX] Failed to send response message of length %d\n", msg_len);
        return;
    }

    memcpy(msg_buf, msg, msg_len);
    rtos_queue_write(resp_queue, &msg_buf, -1, false);
}

uint8_t mac_vif_index = 0;
/**
 ****************************************************************************************
 * @brief Add a new virtual interface to the MAC
 * This interface will be attached to the FHOST VIF element passed as parameter.
 *
 * @param[in,out] fhost_vif Pointer to the FHOST VIF element
 * @param[in]     type      Type of the new interface (@ref mac_vif_type)
 * @param[in]     p2p       Flag indicating whether the new interface is a P2P one
 *
 * @return 0 if successful, !=0 otherwise
 ****************************************************************************************
 */
static int fhost_cntrl_mac_vif_add(struct fhost_vif_tag *fhost_vif, uint8_t type, bool p2p)
{
    struct mm_add_if_req add_if_req;
    struct mm_add_if_cfm add_if_cfm;

    // Sanity check - We should not add a MAC VIF to a FHOST VIF that already has one
    ASSERT_ERR(fhost_vif->mac_vif == NULL);

    // Set message parameters
    add_if_req.type = type;
    add_if_req.p2p = p2p;
    add_if_req.addr = fhost_vif->mac_addr;

    // Send message

    if (rwnx_send_add_if(cntrl_rwnx_hw, &add_if_req, &add_if_cfm)) {
        return -1;
    }

    // Check confirm
    if (add_if_cfm.status) {
        return -1;
    }

    fhost_vif->mac_vif = &vif_info_tab[add_if_cfm.inst_nbr];
    fhost_vif->mac_vif->index  = add_if_cfm.inst_nbr;
    fhost_vif->mac_vif->type = type;
    fhost_env.mac2fhost_vif[add_if_cfm.inst_nbr] = fhost_vif;
    mac_vif_index = add_if_cfm.inst_nbr;

    if(VIF_STA == type) {
        memset(fhost_vif->mac_vif->u.sta.vif_name, 0, 32);
        snprintf((char *)fhost_vif->mac_vif->u.sta.vif_name, 32, "AIC_%04X", fhost_vif->mac_addr.array[2]&0xFFFF);
        aic_dbg("vif_name %s \r\n", fhost_vif->mac_vif->u.sta.vif_name);
    }


    fhost_tx_vif_txq_enable(fhost_vif);

    return 0;
}
/**
 ****************************************************************************************
 * @brief Delete a virtual interface of the MAC
 * This interface will be unlinked from the FHOST VIF element passed as parameter.
 *
 * @param[in] fhost_vif Pointer to the FHOST VIF element
 *
 * @return 0 if successful, !=0 otherwise
 ****************************************************************************************
 */
static int fhost_cntrl_mac_vif_del(struct fhost_vif_tag *fhost_vif)
{
    struct mm_remove_if_req rem_if_req;

    // Sanity check - We should not delete a MAC VIF for a FHOST VIF that doesn't have one
    ASSERT_ERR(fhost_vif->mac_vif != NULL);

    if (fhost_vif->mac_vif->type == VIF_STA) {
        fhost_cntrl_scan_cancel_req();
    }

    // Set parameters for the MM_REMOVE_IF_REQ message
    rem_if_req.inst_nbr = fhost_vif->mac_vif->index;

    fhost_tx_do_vif_disable(fhost_vif->mac_vif->index);

    // Send message
    //if (rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_MM, MM_REMOVE_IF_REQ, sizeof(rem_if_req), &rem_if_req, 1, MM_REMOVE_IF_CFM, NULL))
    if (rwnx_send_remove_if(cntrl_rwnx_hw, &rem_if_req))
        return -1;

    fhost_vif->mac_vif->active = false;
    fhost_vif->mac_vif->type   = VIF_UNKNOWN;
    fhost_vif->mac_vif = NULL;
    fhost_env.mac2fhost_vif[rem_if_req.inst_nbr] = NULL;

    return 0;
}

/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_HW_FEATURE_CMD message
 *
 * @param[in] msg Command header
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_hw_feature(void *msg)
{
    struct cfgrwnx_msg *cmd = msg;
    struct cfgrwnx_hw_feature feat;
    struct mm_start_req start;
    struct mac_addr base_mac_addr;

    feat.hdr.id = CFGRWNX_HW_FEATURE_RESP;
    feat.hdr.len = sizeof(struct cfgrwnx_hw_feature);
    fhost_config_prepare(&feat.me_config, &start, &base_mac_addr, false);
    feat.chan = &fhost_chan;
    fhost_cntrl_cfgrwnx_resp_send(&feat, feat.hdr.len, cmd->hdr.resp_queue);
}

/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_GET_CAPA_CMD message
 *
 * @param[in] msg Command header
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_get_capa(void *msg)
{
    struct cfgrwnx_msg *cmd = msg;
    struct cfgrwnx_msg resp;

    resp.hdr.id = CFGRWNX_GET_CAPA_RESP;
    resp.hdr.len = sizeof(resp);

    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}

/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_SET_KEY_CMD message
 *
 * TODO: update for MESH
 *
 * @param[in] msg Key parameters (@ref cfgrwnx_set_key)
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_set_key(void *msg)
{
    int ret = 0;
    struct cfgrwnx_set_key *cmd = msg;
    struct cfgrwnx_resp resp;
    struct sta_info_tag *sta;
    struct vif_info_tag *mac_vif = fhost_to_mac_vif(cmd->fhost_vif_idx);
    bool pairwise = (cmd->addr && !MAC_ADDR_GROUP(cmd->addr));
    int sta_idx = INVALID_STA_IDX;

    resp.hdr.id = CFGRWNX_SET_KEY_RESP;
    resp.hdr.len = sizeof(resp);
    resp.status = CFGRWNX_SUCCESS;

    if (!mac_vif || (mac_vif->type == VIF_UNKNOWN) || (mac_vif->index > NX_VIRT_DEV_MAX))
    {
        resp.status = CFGRWNX_INVALID_VIF;
        goto send_resp;
    }

    if (pairwise)
    {
        sta = vif_mgmt_get_sta_by_addr(cmd->addr);
        if (sta)
            sta_idx = sta->staid;

        if (sta_idx == INVALID_STA_IDX)
        {
            resp.status = CFGRWNX_INVALID_STA;
            goto send_resp;
        }
    }


    if (cmd->cipher_suite != MAC_CIPHER_INVALID)
    {
        struct mm_key_add_req req;
        struct mm_key_add_cfm cfm;
        req.key_idx = cmd->key_idx;
        req.sta_idx = sta_idx;
        req.key.length = cmd->key_len;
        ASSERT_ERR(cmd->key_len <= MAC_SEC_KEY_LEN);
        memcpy(req.key.array, cmd->key, cmd->key_len);
        req.cipher_suite = cmd->cipher_suite;
        req.inst_nbr = mac_vif->index;
        req.spp = 0;
        req.pairwise = pairwise;

        /* Build the MM_KEY_ADD_REQ message */
        ret = rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_MM, MM_KEY_ADD_REQ, sizeof_b(struct mm_key_add_req), &req, 1, MM_KEY_ADD_CFM, &cfm);
        if (ret < 0) {
            aic_dbg("ERR: <%s>, %d\n", __func__, ret);
        }
        if (cfm.status)
            resp.status = CFGRWNX_ERROR;
    }
    else
    {
        struct mm_key_del_req req;
        if (pairwise)
        {
            req.hw_key_idx = MM_STA_TO_KEY(sta_idx);
        }
        else
        {
            #if NX_MFP
            if (cmd->key_idx > 3)
                req.hw_key_idx = MM_VIF_TO_MFP_KEY(cmd->key_idx, mac_vif->index);
            else
            #endif
                req.hw_key_idx = MM_VIF_TO_KEY(cmd->key_idx, mac_vif->index);
        }

        /* Build the MM_KEY_ADD_REQ message */
        ret = rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_MM, MM_KEY_DEL_REQ, sizeof_b(struct mm_key_del_req), &req, 1, MM_KEY_DEL_CFM, NULL);
        if (ret < 0) {
            aic_dbg("ERR: <%s>, %d\n", __func__, ret);
        }
    }

  send_resp:
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}

static inline s8_l chan_to_fw_pwr(int power)
{
	return power > 127 ? 127 : (s8_l)power;
}

/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_SCAN_CMD message
 *
 * @param[in] msg Scan parameters (@ref cfgrwnx_scan)
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_scan(void *msg)
{
    struct cfgrwnx_scan *cmd = msg;
    struct cfgrwnx_resp resp;
    struct fhost_vif_tag *fhost_vif = &fhost_env.vif[cmd->fhost_vif_idx];
    struct vif_info_tag *mac_vif = fhost_to_mac_vif(cmd->fhost_vif_idx);
    struct scanu_start_req req;
    //struct scanu_start_cfm_add cfm;
    int i, nb_ssid = 0;
	uint8_t chan_flags = 0;

    resp.hdr.id = CFGRWNX_SCAN_RESP;
    resp.hdr.len = sizeof(resp);
    resp.status = CFGRWNX_SUCCESS;

    #if 0
    if (mac_vif->type != VIF_STA)
    {
        resp.status = CFGRWNX_INVALID_VIF;
        goto send_resp;
    }
    #endif
    if (cmd->freqs)
    {
        int *freq = cmd->freqs;
        struct mac_chan_def *chan;
        int nb_scan = 0;

        while (*freq) {
            chan = fhost_chan_get(*freq);

            if (chan) {
                memcpy(&req.chan[nb_scan], chan, sizeof(*chan));
                nb_scan++;
            }

            freq++;

            if (nb_scan == SCAN_CHANNEL_MAX)
                break;
        }

        if (!nb_scan)
        {
            resp.status = CFGRWNX_INVALID_PARAM;
            goto send_resp;
        }
        req.chan_cnt = nb_scan;
    }
    else
    {
        ASSERT_ERR(SCAN_CHANNEL_MAX >= (fhost_chan.chan5G_cnt + fhost_chan.chan2G4_cnt));
        if (fhost_chan.chan2G4_cnt) {
            memcpy(&req.chan[0], fhost_chan.chan2G4, fhost_chan.chan2G4_cnt * sizeof(req.chan[0]));
        }
        if (fhost_chan.chan5G_cnt) {
            memcpy(&req.chan[fhost_chan.chan2G4_cnt], fhost_chan.chan5G, fhost_chan.chan5G_cnt * sizeof(req.chan[0]));
        }
        req.chan_cnt = fhost_chan.chan2G4_cnt + fhost_chan.chan5G_cnt;
    }

    for (i = 0; i < cmd->ssid_cnt ; i++)
    {
        if (cmd->ssids[i].len < MAC_SSID_LEN)
        {
            memcpy(req.ssid[nb_ssid].array, cmd->ssids[i].ssid, cmd->ssids[i].len);
            req.ssid[nb_ssid++].length = cmd->ssids[i].len;
        }

        if (nb_ssid == SCAN_SSID_MAX)
            break;
    }
    #if 0
    if (! nb_ssid)
    {
        resp.status = CFGRWNX_INVALID_PARAM;
        goto send_resp;
    }
    #endif
    req.ssid_cnt = nb_ssid;

    if (cmd->bssid)
    {
        MAC_ADDR_CPY(&req.bssid, cmd->bssid);
    }
    else
    {
        req.bssid.array[0] = 0xffff;
        req.bssid.array[1] = 0xffff;
        req.bssid.array[2] = 0xffff;
    }
#ifdef CONFIG_RADAR_OR_IR_DETECT
    if (req.ssid_cnt == 0)
        chan_flags |= CHAN_NO_IR;
#endif

    req.add_ies = (uint32_t)cmd->extra_ies;
    req.add_ie_len = cmd->extra_ies_len;
    req.vif_idx = mac_vif->index;
    req.no_cck = cmd->no_cck;
    req.duration = 0;
    fhost_vif->scan_sock = cmd->sock;

    memset(cntrl_rwnx_hw->survey, 0, (SCAN_CHANNEL_MAX * sizeof(struct rwnx_survey_info)));
    for (i = 0; i < req.chan_cnt; i++) {
        struct mac_chan_def *chan = &req.chan[i];

        req.chan[i].flags = chan_flags | get_chan_flags(chan->flags);
        req.chan[i].tx_power = chan_to_fw_pwr(chan->tx_power);
    }
    //if (rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_SCANU, SCANU_START_REQ, sizeof(req), &req, 1, SCANU_START_CFM_ADDTIONAL, &cfm))
    if (rwnx_send_scanu_req(cntrl_rwnx_hw, &req))
        resp.status = CFGRWNX_ERROR;

  send_resp:
    AIC_LOG_PRINTF(" %s resp.status %d\n", __func__, resp.status);
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}

static unsigned int g_listen_interval = 0, g_dont_wait_bcmc = 0;
void set_deepsleep_param(unsigned int listen_interval, unsigned int dont_wait_bcmc)
{
    g_listen_interval = listen_interval;
    g_dont_wait_bcmc  = dont_wait_bcmc;
}
/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_CONNECT_CMD message
 *
 * @param[in] msg Connection parameters (@ref cfgrwnx_connect)
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_connect(void *msg)
{
    int ret;
    struct cfgrwnx_connect *cmd = msg;
    struct cfgrwnx_resp resp;
    struct fhost_vif_tag *fhost_vif = &fhost_env.vif[cmd->fhost_vif_idx];
    struct vif_info_tag *mac_vif = fhost_to_mac_vif(cmd->fhost_vif_idx);
    struct sm_connect_req req;
    struct sm_connect_cfm cfm;

    resp.hdr.id = CFGRWNX_CONNECT_RESP;
    resp.hdr.len = sizeof(resp);
    resp.status = CFGRWNX_SUCCESS;

    if (mac_vif->type != VIF_STA)
    {
        resp.status = CFGRWNX_INVALID_VIF;
        goto send_resp;
    }

    if (cmd->ssid.len > MAC_SSID_LEN)
    {
        resp.status = CFGRWNX_INVALID_PARAM;
        goto send_resp;
    }
    req.ssid.length = cmd->ssid.len;
    memcpy(req.ssid.array, cmd->ssid.ssid, cmd->ssid.len);
    MAC_ADDR_CPY(&req.bssid, cmd->bssid);
    req.chan = cmd->chan;
    req.flags = cmd->flags;
    req.ctrl_port_ethertype = cmd->ctrl_port_ethertype;
    req.ie_len = cmd->ie_len;
    req.listen_interval = g_listen_interval;
    req.dont_wait_bcmc = g_dont_wait_bcmc;
    req.auth_type = cmd->auth_alg;
    if ((int16_t)cmd->uapsd == -1)
        req.uapsd_queues = fhost_vif->uapsd_queues;
    else
        req.uapsd_queues = cmd->uapsd;
    if (mac_vif->type == VIF_UNKNOWN)
    {
        resp.status = CFGRWNX_INVALID_VIF;
        goto send_resp;
    }
    req.vif_idx = mac_vif->index;
    if (cmd->ie_len > sizeof(req.ie_buf))
    {
        resp.status = CFGRWNX_INVALID_PARAM;
        goto send_resp;
    }
    else if (req.ie_len)
    {
        memcpy(req.ie_buf, cmd->ie, cmd->ie_len);
    }

    fhost_vif->conn_sock = cmd->sock;

    //ret = rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_SM, SM_CONNECT_REQ, sizeof_b(struct sm_connect_req), &req, 1, SM_CONNECT_CFM, &cfm);
    ret = rwnx_send_sm_connect_req(cntrl_rwnx_hw, &req, &cfm);
    if (ret < 0) {
        aic_dbg("ERR: <%s>, %d\n", __func__, ret);
    }
    if (cfm.status != CO_OK) {
        resp.status = CFGRWNX_ERROR;
    } else {
        fhost_vif->conn_sock = cmd->sock;
    }

  send_resp:
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}

/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_DISCONNECT_CMD message
 *
 * @param[in] msg Disconnection parameters (@ref cfgrwnx_disconnect)
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_disconnect(void *msg)
{
    int ret;
    struct cfgrwnx_disconnect *cmd = msg;
    struct cfgrwnx_resp resp;
    struct vif_info_tag *mac_vif = fhost_to_mac_vif(cmd->fhost_vif_idx);
    struct sm_disconnect_req req;

    resp.hdr.id = CFGRWNX_DISCONNECT_RESP;
    resp.hdr.len = sizeof(resp);
    resp.status = CFGRWNX_SUCCESS;

    if (mac_vif->type != VIF_STA)
    {
        resp.status = CFGRWNX_INVALID_VIF;
        goto send_resp;
    }
    req.vif_idx = mac_vif->index;
    req.reason_code = cmd->reason_code;

    //ret = rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_SM, SM_DISCONNECT_REQ, sizeof_b(struct sm_disconnect_req), &req, 1, SM_DISCONNECT_CFM, NULL);
    ret = rwnx_send_sm_disconnect_req(cntrl_rwnx_hw, &req);
    if (ret < 0) {
      aic_dbg("ERR: <%s>, %d\n", __func__, ret);
    }

  send_resp:
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}

/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_CTRL_PORT_CMD message
 *
 * @param[in] msg Control port parameters (@ref cfgrwnx_ctrl_port)
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_ctrl_port(void *msg)
{
    int ret;
    struct cfgrwnx_ctrl_port *cmd = msg;
    struct cfgrwnx_resp resp;
    struct vif_info_tag *mac_vif = fhost_to_mac_vif(cmd->fhost_vif_idx);
    struct me_set_control_port_req req;

    resp.hdr.id = CFGRWNX_CTRL_PORT_RESP;
    resp.hdr.len = sizeof(resp);
    resp.status = CFGRWNX_ERROR;

    if (!mac_vif || ((mac_vif->type != VIF_STA) && (mac_vif->type != VIF_AP)) || (mac_vif->index > NX_VIRT_DEV_MAX))
        goto send_resp;

    if (mac_vif->type == VIF_STA)
        req.sta_idx = mac_vif->u.sta.ap_id;
    else {
        req.sta_idx = vif_mgmt_get_staid(mac_vif, &cmd->addr);
    }

    if (req.sta_idx == INVALID_STA_IDX)
        goto send_resp;

    req.control_port_open = cmd->authorized;

    ret = rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_ME, ME_SET_CONTROL_PORT_REQ, sizeof_b(struct me_set_control_port_req), &req, 1, ME_SET_CONTROL_PORT_CFM, NULL);
    if (ret < 0) {
        aic_dbg("ERR: <%s>, %d\n", __func__, ret);
        goto send_resp;
    }
    resp.status = CFGRWNX_SUCCESS;

  send_resp:
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}

#if NX_SYS_STAT
/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_SYS_STATS_CMD message
 *
 * @param[in] msg Parameters (@ref cfgrwnx_msg)
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_sys_stats(void *msg)
{
    struct cfgrwnx_msg *cmd = msg;
    struct cfgrwnx_sys_stats_resp resp;

    resp.hdr.id = CFGRWNX_SYS_STATS_RESP;
    resp.hdr.len = sizeof(resp);

    if (macif_kmsg_push(DBG_GET_SYS_STAT_REQ, TASK_DBG, NULL, 0))
        goto send_resp;

    fhost_cntrl_ke_msg_wait(DBG_GET_SYS_STAT_CFM, &resp.stats, sizeof(resp.stats));

  send_resp:
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}
#endif //NX_SYS_STAT

/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_SCAN_RESULTS_CMD message
 *
 * @param[in] msg Parameters (@ref cfgrwnx_scan_results)
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_scan_results(void *msg)
{
    struct cfgrwnx_scan_results *cmd = msg;
    struct cfgrwnx_scan_results_resp resp;
    struct scanu_get_scan_result_req req;
    struct scanu_get_scan_result_cfm cfm;

    resp.hdr.id = CFGRWNX_SCAN_RESULTS_RESP;
    resp.hdr.len = sizeof(resp);
    resp.status = CFGRWNX_SUCCESS;

    req.idx = cmd->idx;

    if (rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_SCANU, SCANU_GET_SCAN_RESULT_REQ, sizeof(req), &req, 1, SCANU_GET_SCAN_RESULT_CFM, &cfm))
    {
        resp.status = CFGRWNX_ERROR;
        goto send_resp;
    }

    resp.scan_result = cfm.scan_result;

  send_resp:
    //aic_dbg("idx %d, %d, %d\r\n", cmd->idx, cfm.scan_result.valid_flag, resp.status);
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}

void fhost_cntrl_cfgrwnx_set_tx_power(uint8_t fvif_idx, uint8_t tx_power)
{
    int ret;
    struct mm_set_power_req req;
    struct mm_set_power_cfm cfm;

    req.inst_nbr = fvif_idx;
    req.power = tx_power;
    ret = rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_MM, MM_SET_POWER_REQ, sizeof(struct mm_set_power_req), &req, 1, MM_SET_POWER_CFM, &cfm);

    if(ret < 0){
        aic_dbg("ERR: <%s>, %d\n", __func__, ret);
    }
}

/**
 ****************************************************************************************
 * @brief Process ::ME_RC_SET_RATE_REQ message
 *
 * @param[in] msg Set Rate parameters (@ref me_rc_set_rate_req)
 ****************************************************************************************
 */
#define MCS_INDEX_TX_RCX_OFT    0
#define HE_GI_TYPE_TX_RCX_OFT   9
#define FORMAT_MOD_TX_RCX_OFT   11
#define BW_TX_RCX_OFT           7
#define DCM_MOD_OFT             14
void fhost_cntrl_cfgrwnx_set_fixed_rate(uint8_t sta_idx, uint8_t bw, uint8_t format_idx, uint16_t rate_idx, uint8_t pre_type, uint8_t dcm)
{
    int ret;
    struct me_rc_set_rate_req req;

    memset(&req, 0, sizeof(req));
    req.sta_idx = sta_idx;
    req.fixed_rate_cfg = (rate_idx << MCS_INDEX_TX_RCX_OFT)    | \
                         (format_idx << FORMAT_MOD_TX_RCX_OFT)  | \
                         (bw << BW_TX_RCX_OFT)  | \
                         (pre_type << HE_GI_TYPE_TX_RCX_OFT) | \
                         (dcm << DCM_MOD_OFT);

    AIC_LOG_PRINTF("%s [%x]\n", __func__, req.fixed_rate_cfg);
    ret = rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_ME, ME_RC_SET_RATE_REQ, sizeof_b(struct me_rc_set_rate_req), &req, 0, 0, NULL);
    if (ret < 0) {
        aic_dbg("Setrate fail\r\n");
    }
}

/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_SET_VIF_TYPE_CMD message
 *
 * Change the type of the MAC vif associated to the FHOST vif:
 * - Delete the current MAC vif (if any)
 * - Create a new MAC vif (unless requested type ifs VIF_UNKNOWN)
 *
 * @param[in] msg Parameters (@ref cfgrwnx_set_vif_type)
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_set_vif_type(void *msg)
{
    struct cfgrwnx_set_vif_type *cmd = msg;
    struct cfgrwnx_resp resp;
    struct fhost_vif_tag *fhost_vif;

    resp.hdr.id = CFGRWNX_SET_VIF_TYPE_RESP;
    resp.hdr.len = sizeof(resp);
    resp.status = CFGRWNX_ERROR;

    if (cmd->fhost_vif_idx >= NX_VIRT_DEV_MAX)
    {
        AIC_LOG_PRINTF("fhost_cntrl_cfgrwnx_set_vif_type vif_idx %d\r\n", cmd->fhost_vif_idx);
        resp.status = CFGRWNX_INVALID_VIF;
        goto send_resp;
    }
    fhost_vif = &fhost_env.vif[cmd->fhost_vif_idx];

    // Always delete even if it is already of the requested type
    if (fhost_vif->mac_vif && fhost_cntrl_mac_vif_del(fhost_vif)) {
        AIC_LOG_PRINTF("fhost_cntrl_cfgrwnx_set_vif_type delete fail\r\n");
        goto send_resp;
    }

    if ((cmd->type != VIF_UNKNOWN) &&
        fhost_cntrl_mac_vif_add(fhost_vif, cmd->type, cmd->p2p)) {
        AIC_LOG_PRINTF("fhost_cntrl_cfgrwnx_set_vif_type add fail\r\n");
        goto send_resp;
    }

    resp.status = CFGRWNX_SUCCESS;


  send_resp:
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}

/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_MONITOR_CFG_CMD message
 *
 * @param[in] msg Key parameters (@ref cfgrwnx_monitor_cfg)
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_monitor_cfg(void *msg)
{
    struct cfgrwnx_monitor_cfg *cmd = msg;
    struct cfgrwnx_resp resp;
    struct vif_info_tag *mac_vif = fhost_to_mac_vif(cmd->fhost_vif_idx);
    struct me_config_monitor_req conf_req;
    struct me_config_monitor_cfm conf_cfm;

    resp.hdr.id = CFGRWNX_MONITOR_CFG_RESP;
    resp.hdr.len = sizeof(resp);
    resp.status = CFGRWNX_ERROR;

    if (!mac_vif || (mac_vif->type != VIF_MONITOR)) {
        goto send_resp;
    }

    // Set parameters for the ME_CONFIG_MONITOR_REQ message
    conf_req.chan = cmd->cfg.chan;
    conf_req.uf = cmd->cfg.uf;
    conf_req.chan_set = true;

    if (rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_ME, ME_CONFIG_MONITOR_REQ, sizeof(conf_req), &conf_req, 1, ME_CONFIG_MONITOR_CFM, &conf_cfm)) {
        goto send_resp;
    }

    // Initialize the callback function
    fhost_rx_set_monitor_cb(cmd->cfg.cb, cmd->cfg.cb_arg);

    resp.status = CFGRWNX_SUCCESS;

  send_resp:
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}

#if NX_SYS_STAT
/**
 ****************************************************************************************
 * @brief Process :CFGRWNX_SYS_STATS_CMD message
 *
 * @param[in] msg Parameters
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_sys_stats(void *msg)
{
    #if 0
    struct cfgrwnx_msg *cmd = msg;
    struct cfgrwnx_sys_stats_resp resp;

    resp.hdr.id = CFGRWNX_SYS_STATS_RESP;
    resp.hdr.len = sizeof(struct cfgrwnx_sys_stats_resp);
    resp.hdr.split = 0;

    if (macif_kmsg_push(DBG_GET_SYS_STAT_REQ, TASK_DBG, NULL, 0))
        goto send_resp;

    fhost_cntrl_ke_msg_wait(DBG_GET_SYS_STAT_CFM, &resp.stats, sizeof(resp.stats));

  send_resp:
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
    #endif
}
#endif //NX_SYS_STAT
/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_EXTERNAL_AUTH_STATUS_RESP message
 *
 * @param[in] msg Key parameters (@ref cfgrwnx_external_auth_status)
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_external_auth_status(void *msg)
{
    struct cfgrwnx_external_auth_status *status = msg;
    struct vif_info_tag *mac_vif = fhost_to_mac_vif(status->fhost_vif_idx);
    struct sm_external_auth_required_rsp rsp;

    if (!mac_vif)
        return;

    rsp.vif_idx = mac_vif->index;
    rsp.status = status->status;

    #if defined(CONFIG_AIC8800D80)
    if (rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_SM, SM_EXTERNAL_AUTH_REQUIRED_RSP, sizeof_b(rsp),  &rsp, 1, SM_EXTERNAL_AUTH_REQUIRED_RSP_CFM, NULL)) {
        aic_dbg("ERR: <%s>\n", __func__);
    }
    #else
    if (rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_SM, SM_EXTERNAL_AUTH_REQUIRED_RSP, sizeof_b(rsp),  &rsp, 0, 0, NULL)) {
        aic_dbg("ERR: <%s>\n", __func__);
    }
    #endif
}

#if NX_BEACONING
/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_START_AP_CMD message
 *
 * @param[in] msg AP parameters (@ref cfgrwnx_start_ap)
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_start_ap(void *msg)
{
    struct cfgrwnx_start_ap *cmd = msg;
    struct cfgrwnx_resp resp;
    struct fhost_vif_tag *fhost_vif;
    struct apm_start_req req;
    struct apm_start_cfm cfm;

    resp.hdr.id = CFGRWNX_START_AP_RESP;
    resp.hdr.len = sizeof(resp);
    resp.status = CFGRWNX_ERROR;

    if (cmd->fhost_vif_idx >= NX_VIRT_DEV_MAX)
        goto send_resp;
    fhost_vif = &fhost_env.vif[cmd->fhost_vif_idx];

    if (!fhost_vif->mac_vif || (fhost_vif->mac_vif->type != VIF_AP))
        goto send_resp;

    if (cmd->bcn_len > NX_BCNFRAME_LEN)
    {
        aic_dbg("Beacon is too long (%d bytes)", cmd->bcn_len);
        goto send_resp;
    }
    {
        struct apm_set_bcn_ie_req bcn_ie_req = {0};
        struct apm_set_bcn_ie_cfm bcn_ie_cfm = {0};
        bcn_ie_req.vif_idx    = fhost_vif->mac_vif->index;
        memcpy(bcn_ie_req.bcn_ie, (void*)cmd->bcn, cmd->bcn_len);
        bcn_ie_req.bcn_ie_len = cmd->bcn_len;
        if (rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_APM, APM_SET_BEACON_IE_REQ, sizeof_b(struct apm_set_bcn_ie_req), &bcn_ie_req, 1, APM_SET_BEACON_IE_CFM, &bcn_ie_cfm)) {
            aic_dbg("ERR: <%s>\n", __func__);
        }
    }

    memset(&req, 0, sizeof(req));
    req.basic_rates = cmd->basic_rates;
    req.chan.band = cmd->chan.band;
    req.chan.flags = cmd->chan.flags;
    req.chan.freq  = cmd->chan.prim20_freq;
    req.center_freq1 = cmd->chan.center1_freq;
    req.center_freq2 = cmd->chan.center2_freq;
    req.bcn_addr = (uint32_t)cmd->bcn;
    req.bcn_len = cmd->bcn_len;
    req.bcn_int = cmd->bcn_int;
    req.tim_oft = cmd->tim_oft;
    req.tim_len = cmd->tim_len;
    req.flags = cmd->flags;
    req.ctrl_port_ethertype = cmd->ctrl_ethertype;
    req.vif_idx = fhost_vif->mac_vif->index;
    req.ch_width = cmd->chan.type;
#ifdef CONFIG_RADAR_OR_IR_DETECT
    req.chan.flags = get_chan_flags(cmd->chan.flags);
#else
    req.chan.flags = 0;
#endif

    if ((rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_APM, APM_START_REQ, sizeof(req), &req, 1, APM_START_CFM, &cfm)) || (0 != cfm.status)) {
        aic_dbg("%s, St %d\r\n", __func__, cfm.status);
        goto send_resp;
    }
    fhost_vif->conn_sock = cmd->sock;
    net_if_up(&fhost_vif->net_if);
    fhost_tx_do_sta_add(cfm.bcmc_idx);
    resp.status = CFGRWNX_SUCCESS;

    fhost_vif->mac_vif->active = true;
    fhost_vif->mac_vif->chan.band = cmd->chan.band;
    fhost_vif->mac_vif->chan.prim20_freq = cmd->chan.prim20_freq;
    fhost_vif->mac_vif->chan.center1_freq = cmd->chan.center1_freq;
    fhost_vif->mac_vif->chan.type = cmd->chan.type;
    // aic_dbg("%s, type %d\r\n", __func__, cmd->chan.type);
    #ifdef CONFIG_RWNX_RADAR
	struct fhost_vif_tag *vif = fhost_to_mac_vif(0);
    struct cfg80211_chan_def chandef;
    chandef.center_freq1 = cmd->chan.center1_freq;
    chandef.center_freq2 = cmd->chan.center2_freq;
    chandef.width = cmd->chan.type;
    chandef.chan.band = cmd->chan.band;
    chandef.chan.center_freq = cmd->chan.prim20_freq;
    //chandef.chan->flags = cmd->chan.flags;
    if ((chandef.chan.center_freq >= 5260) && (chandef.chan.center_freq <= 5720 )) {
        chandef.chan.flags = CHAN_RADAR;
    }

    rwnx_chanctx_link(vif, cfm.ch_idx, &chandef);
    rwnx_radar_detection_enable_on_cur_channel(cntrl_rwnx_hw);
    #endif/* CONFIG_RWNX_RADAR */
  send_resp:
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}
#endif

#if NX_BEACONING
/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_STOP_AP_CMD message
 *
 * @param[in] msg AP parameters (@ref cfgrwnx_stop_ap)
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_stop_ap(void *msg)
{
    struct cfgrwnx_stop_ap *cmd = msg;
    struct cfgrwnx_resp resp;
    struct fhost_vif_tag *fhost_vif;
    struct apm_stop_req req;

    resp.hdr.id = CFGRWNX_STOP_AP_RESP;
    resp.hdr.len = sizeof(resp);
    resp.status = CFGRWNX_ERROR;

    if (cmd->fhost_vif_idx >= NX_VIRT_DEV_MAX)
        goto send_resp;
    fhost_vif = &fhost_env.vif[cmd->fhost_vif_idx];

    if (!fhost_vif->mac_vif || (fhost_vif->mac_vif->type != VIF_AP))
        goto send_resp;

    fhost_tx_do_sta_del(VIF_TO_BCMC_IDX(fhost_vif->mac_vif->index));
#if (AICWF_RX_REORDER)
    reord_deinit_sta_by_mac(NULL);
#endif

    req.vif_idx = fhost_vif->mac_vif->index;

    if ((rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_APM, APM_STOP_REQ, sizeof(req), &req, 1, APM_STOP_CFM, NULL))) {
        aic_dbg("Stop AP fail\r\n");
        goto send_resp;
    }
    fhost_vif->conn_sock = -1;
    net_if_down(&fhost_vif->net_if);
    resp.status = CFGRWNX_SUCCESS;

  send_resp:
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}
#endif

#if 0
/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_SET_EDCA_CMD message
 *
 * @param[in] msg AP parameters (@ref cfgrwnx_set_edca)
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_set_edca(void *msg)
{
    struct cfgrwnx_set_edca *cmd = msg;
    struct cfgrwnx_resp resp;
    struct vif_info_tag *mac_vif = fhost_to_mac_vif(cmd->fhost_vif_idx);
    struct mm_set_edca_req req;

    resp.hdr.id = CFGRWNX_SET_EDCA_RESP;
    resp.hdr.len = sizeof(resp);
    resp.status = CFGRWNX_ERROR;

    if (!mac_vif || (mac_vif->type != VIF_AP))
        goto send_resp;

    if (cmd->aifsn > 15 || cmd->cwmin > 0x7FFF || cmd->cwmax > 0x7FFF || cmd->aci > AC_VO)
        goto send_resp;

    req.inst_nbr = mac_vif->index;
    req.hw_queue = cmd->aci;
    req.uapsd = false;
    req.ac_param = (cmd->aifsn |
                    (32 - co_clz(cmd->cwmin)) << 4 |
                    (32 - co_clz(cmd->cwmax)) << 8 |
                    cmd->txop << 12);

    if (macif_kmsg_push(MM_SET_EDCA_REQ, TASK_MM, &req, sizeof(req)))
        goto send_resp;

    fhost_cntrl_ke_msg_wait(MM_SET_EDCA_CFM, NULL, 0);
    resp.status = CFGRWNX_SUCCESS;

  send_resp:
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}
#endif

#if NX_BEACONING
/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_BCN_UPDATE_CMD message
 *
 * @param[in] msg AP parameters (@ref cfgrwnx_bcn_update)
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_bcn_update(void *msg)
{
    struct cfgrwnx_bcn_update *cmd = msg;
    struct cfgrwnx_resp resp;
    struct vif_info_tag *mac_vif = fhost_to_mac_vif(cmd->fhost_vif_idx);
    struct mm_bcn_change_req req = {0};

    resp.hdr.id = CFGRWNX_BCN_UPDATE_RESP;
    resp.hdr.len = sizeof(resp);
    resp.status = CFGRWNX_ERROR;

    if (!mac_vif || (mac_vif->type != VIF_AP))
        goto send_resp;

    if (cmd->bcn_len > NX_BCNFRAME_LEN)
    {
        aic_dbg("Beacon update is too long (%d bytes)", cmd->bcn_len);
        goto send_resp;
    }

    req.bcn_ptr = (uint32_t)cmd->bcn;
    req.bcn_len = cmd->bcn_len;
    req.tim_oft = cmd->tim_oft;
    req.tim_len = cmd->tim_len;
    req.inst_nbr = mac_vif->index;
    memcpy(req.csa_oft, cmd->csa_oft, BCN_MAX_CSA_CPT);
    {
        struct apm_set_bcn_ie_req bcn_ie_req = {0};
        struct apm_set_bcn_ie_cfm bcn_ie_cfm = {0};
        bcn_ie_req.vif_idx    = mac_vif->index;
        memcpy(bcn_ie_req.bcn_ie, (void*)cmd->bcn, cmd->bcn_len);
        bcn_ie_req.bcn_ie_len = cmd->bcn_len;
        if (rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_APM, APM_SET_BEACON_IE_REQ, sizeof_b(struct apm_set_bcn_ie_req), &bcn_ie_req, 1, APM_SET_BEACON_IE_CFM, &bcn_ie_cfm)) {
            aic_dbg("ERR: <%s>\n", __func__);
        }
    }

    if (rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_MM, MM_BCN_CHANGE_REQ, sizeof(req), &req, 1, MM_BCN_CHANGE_CFM, NULL)) {
        goto send_resp;
    }
    resp.status = CFGRWNX_SUCCESS;

  send_resp:
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}
extern aic_wifi_event_cb g_aic_wifi_event_cb;
/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_STA_ADD_CMD message
 *
 * @param[in] msg AP parameters (@ref cfgrwnx_sta_add)
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_sta_add(void *msg)
{
    struct cfgrwnx_sta_add *cmd = msg;
    struct cfgrwnx_resp resp;
    struct vif_info_tag *mac_vif = fhost_to_mac_vif(cmd->fhost_vif_idx);
    struct me_sta_add_req req;
    struct me_sta_add_cfm cfm;

    resp.hdr.id = CFGRWNX_STA_ADD_RESP;
    resp.hdr.len = sizeof(resp);
    resp.status = CFGRWNX_ERROR;

    if (!mac_vif || (mac_vif->type != VIF_AP))
        goto send_resp;

    req.mac_addr = *cmd->addr;
    req.rate_set = cmd->rate_set;
    req.ht_cap = cmd->ht_cap;
    req.vht_cap = cmd->vht_cap;
    req.he_cap = cmd->he_cap;
    req.flags = cmd->flags;
    req.aid = cmd->aid;
    req.uapsd_queues = cmd->uapsd_queues;
    req.max_sp_len = cmd->max_sp_len;
    req.opmode = cmd->opmode;
    req.vif_idx = mac_vif->index;
    req.tdls_sta = false;
    //req.tdls_initiator = false;
    req.tdls_chsw_allowed = false;

#ifdef CONFIG_RADAR_OR_IR_DETECT
    if((g_rwnx_hw->radar.sta_num == 0) && (g_rwnx_hw->radar.status != RWNX_RADAR_INSERVICE_BUSY))
        g_rwnx_hw->radar.status = RWNX_RADAR_INSERVICE_BUSY;

    //AICWFDBG(LOGINFO, "DFS: assoc add num %d, %d\n", rwnx_hw->radar.sta_num, rwnx_hw->radar.status);
    if(g_rwnx_hw->radar.sta_num < 0x0FFF)
        g_rwnx_hw->radar.sta_num++;
#endif

    if (rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_ME, ME_STA_ADD_REQ, sizeof(req), &req, 1, ME_STA_ADD_CFM, &cfm)) {
        goto send_resp;
    }

    if(CO_OK == cfm.status) {
        fhost_tx_do_sta_add(cfm.sta_idx);
        // get an entry from the free station list
        struct sta_info_tag *sta = (struct sta_info_tag*)co_list_pop_front(&free_sta_list);
        sta->inst_nbr = cmd->fhost_vif_idx;
        sta->staid = cfm.sta_idx;
        sta->valid =  true;
        sta->mac_addr = req.mac_addr;
        co_list_push_back(&mac_vif->sta_list, &sta->list_hdr);
        resp.status = CFGRWNX_SUCCESS;
        #ifdef CONFIG_DYNAMIC_PERPWR
		sta->rssi_save = 0;
		sta->per_pwrloss = 0;
		sta->last_jiffies = rtos_now(0);
        #endif /* CONFIG_DYNAMIC_PERPWR */
        if (g_aic_wifi_event_cb) {
            aic_wifi_event_data enData = {0};
            uint8_t *mac_addr = (uint8_t *)&sta->mac_addr.array;

            enData.data.auth_deauth_data.reserved[0] = mac_addr[0];
            enData.data.auth_deauth_data.reserved[1] = mac_addr[1];
            enData.data.auth_deauth_data.reserved[2] = mac_addr[2];
            enData.data.auth_deauth_data.reserved[3] = mac_addr[3];
            enData.data.auth_deauth_data.reserved[4] = mac_addr[4];
            enData.data.auth_deauth_data.reserved[5] = mac_addr[5];

            g_aic_wifi_event_cb(ASSOC_IND_EVENT, &enData);
        }
    }

  send_resp:
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}

/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_STA_REMOVE_CMD message
 *
 * @param[in] msg AP parameters (@ref cfgrwnx_sta_remove)
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_sta_remove(void *msg)
{
    struct cfgrwnx_sta_remove *cmd = msg;
    struct cfgrwnx_resp resp;
    struct vif_info_tag *mac_vif = fhost_to_mac_vif(cmd->fhost_vif_idx);
    struct me_sta_del_req req;

    resp.hdr.id = CFGRWNX_STA_REMOVE_RESP;
    resp.hdr.len = sizeof(resp);
    resp.status = CFGRWNX_ERROR;

    if (!mac_vif || (mac_vif->type != VIF_AP))
        goto send_resp;

    req.sta_idx = INVALID_STA_IDX;
    req.tdls_sta = false;
    req.sta_idx = vif_mgmt_get_staid(mac_vif, cmd->addr);
    if (req.sta_idx == INVALID_STA_IDX)
        goto send_resp;

    fhost_tx_do_sta_del(req.sta_idx);
#ifdef CONFIG_RADAR_OR_IR_DETECT
        if((g_rwnx_hw->radar.sta_num == 1) && (g_rwnx_hw->radar.status == RWNX_RADAR_INSERVICE_BUSY))
            g_rwnx_hw->radar.status = RWNX_RADAR_INSERVICE_DONE;

        //AICWFDBG(LOGINFO, "DFS: assoc del num %d, %d\n", rwnx_hw->radar.sta_num, rwnx_hw->radar.status);
        if(g_rwnx_hw->radar.sta_num > 0)
            g_rwnx_hw->radar.sta_num--;
#endif

    if (rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_ME, ME_STA_DEL_REQ, sizeof(req), &req, 1, ME_STA_DEL_CFM, NULL)) {
        goto send_resp;
    }

    // Free this sta info
    struct sta_info_tag *sta = vif_mgmt_get_sta_by_addr(cmd->addr);
    if (sta && sta->valid) {
        uint8_t *p_mac = (uint8_t *)&sta->mac_addr.array;
        aic_dbg("Remove %02X:%02X:%02X:%02X:%02X:%02X\n", p_mac[0], p_mac[1], p_mac[2], p_mac[3], p_mac[4], p_mac[5]);
        #if (AICWF_RX_REORDER)
        reord_deinit_sta_by_mac(p_mac);
        #endif

        sta->last_active_time_us = 0;
        #ifdef CONFIG_DYNAMIC_PERPWR
        sta->rssi_save = 0;
        sta->per_pwrloss = 0;
        sta->last_jiffies = 0;
        #endif /* CONFIG_DYNAMIC_PERPWR */

        co_list_extract(&mac_vif->sta_list, &sta->list_hdr);
        memset(sta, 0, sizeof *sta);
        sta->staid = 0xFF;
        // Push it back in the free list
        co_list_push_back(&free_sta_list, (struct co_list_hdr*)sta);
    }
    resp.status = CFGRWNX_SUCCESS;

  send_resp:
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}
#endif

#if 0
/**
 ****************************************************************************************
 * @brief Process @ref CFGRWNX_KEY_SEQNUM_CMD message
 *
 * @param[in] msg AP parameters (@ref cfgrwnx_key_seqnum)
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_key_seqnum(void *msg)
{
    struct cfgrwnx_key_seqnum *cmd = msg;
    struct cfgrwnx_key_seqnum_resp resp;
    struct vif_info_tag *mac_vif = fhost_to_mac_vif(cmd->fhost_vif_idx);
    struct key_info_tag *key = NULL;

    resp.hdr.id = CFGRWNX_KEY_SEQNUM_RESP;
    resp.hdr.len = sizeof(resp);
    resp.status = CFGRWNX_ERROR;

    if (!mac_vif || (mac_vif->type != VIF_AP))
        goto send_resp;

    if (cmd->addr)
    {
        struct sta_info_tag *sta;
        int sta_idx = vif_mgmt_get_staid(mac_vif, cmd->addr);
        if (sta_idx != INVALID_STA_IDX)
        {
            sta = &sta_info_tab[sta_idx];
            if (sta->sta_sec_info.key_info.key_idx == cmd->key_idx)
                key = &sta->sta_sec_info.key_info;
        }
    }
    else if (cmd->key_idx < CO_ARRAY_SIZE(mac_vif->key_info))
    {
        key = &mac_vif->key_info[cmd->key_idx];
    }

    if (key == NULL || !key->valid)
        goto send_resp;

    resp.seqnum = key->tx_pn;
    resp.status = CFGRWNX_SUCCESS;

  send_resp:
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}
#endif

#ifdef CONFIG_OFFCHANNEL
extern struct fhost_tx_queue_tag* fhost_txq_offchan_init(void);
extern void fhost_txq_offchan_deinit(struct fhost_tx_queue_tag* txq);
static void fhost_cntrl_cfgrwnx_roc(void *msg)
{
	printk("%s in\n", __func__);
	int ret;
	struct cfgrwnx_roc_cmd *cmd = msg;
	struct cfgrwnx_resp resp;
	struct fhost_vif_tag *fhost_vif = &fhost_env.vif[cmd->fhost_vif_idx];
	struct mm_remain_on_channel_cfm roc_cfm;
	struct rwnx_roc_elem *roc_elem;
	struct fhost_tx_queue_tag* txq;
	struct cfgrwnx_roc_event event;
	/* temporary channel element */
	struct ieee80211_channel *chan = rtos_malloc(sizeof(struct ieee80211_channel));
	if (!chan) {
        resp.status = CFGRWNX_ERROR;
        goto send_resp;
    }
	chan->band = (cmd->freq > 5180)? _80211_BAND_5GHZ : _80211_BAND_2GHZ;
	chan->center_freq = cmd->freq;
	chan->max_power = 20;//temp

	resp.hdr.id = CFGRWNX_REMAIN_ON_CHANNEL_RESP;
	resp.hdr.len = sizeof(resp);
	resp.status = CFGRWNX_SUCCESS;

	event.hdr.id = CFGRWNX_REMAIN_ON_CHANNEL_EVENT;
	event.hdr.len = sizeof(event);
	event.center_freq = cmd->freq;

	if (cmd->duration < 100) {
		printk("duration time change to 200ms\n");
		cmd->duration = 200;
	}

	if (cntrl_rwnx_hw->roc_elem) {
		rtos_msleep(2);
		if (cntrl_rwnx_hw->roc_elem) {
			printk("remain on channel fail\n");
            resp.status = CFGRWNX_ERROR;
    		goto send_resp;
		}
	}

	/* temporary roc element */
	roc_elem = rtos_malloc(sizeof(struct rwnx_roc_elem));
	if (!roc_elem) {
        resp.status = CFGRWNX_ERROR;
		goto send_resp;
    }
	roc_elem->chan = chan;
	roc_elem->duration = cmd->duration;
	roc_elem->mgmt_roc = false;
	roc_elem->on_chan = false;

	/* Initialize the OFFCHAN TX queue to allow off-channel transmissions */
    txq = fhost_txq_offchan_init();

	fhost_vif->p2p_sock = cmd->sock;

	cntrl_rwnx_hw->roc_elem = roc_elem;
	ret = rwnx_send_roc(cntrl_rwnx_hw, cmd->fhost_vif_idx, chan, cmd->duration, &roc_cfm);
	if (ret == 0) {
		if (roc_cfm.status) {
			cntrl_rwnx_hw->roc_elem = NULL;
			rtos_free(roc_elem);
			rtos_free(chan);
			fhost_txq_offchan_deinit(txq);

            resp.status = CFGRWNX_ERROR;
            goto send_resp;
		}
	} else {
		cntrl_rwnx_hw->roc_elem = NULL;
		rtos_free(roc_elem);
		rtos_free(chan);
		fhost_txq_offchan_deinit(txq);
	}

	ret = fhost_cntrl_cfgrwnx_event_send(&event.hdr, fhost_vif->p2p_sock);
	if (ret) {
		printk("%s event fail\n", __func__);
        resp.status = CFGRWNX_ERROR;
	}

send_resp:
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);

	return;
}

static void fhost_cntrl_cfgrwnx_cancel_roc(void *msg)
{
	printk("%s\n", __func__);
	struct cfgrwnx_cancel_roc_cmd *cmd = msg;
	struct cfgrwnx_resp resp;

    resp.hdr.id = CFGRWNX_CANCEL_REMAIN_ON_CHANNEL_RESP;
	resp.hdr.len = sizeof(resp);
	resp.status = CFGRWNX_SUCCESS;

    if (!cntrl_rwnx_hw->roc_elem)
	{
		printk("roc_elem is null\n");
        resp.status = CFGRWNX_ERROR;
		goto send_resp;
	}

	rwnx_send_cancel_roc(cntrl_rwnx_hw, cmd->fhost_vif_idx);

send_resp:
    fhost_cntrl_cfgrwnx_resp_send(&resp, resp.hdr.len, cmd->hdr.resp_queue);
}
#endif

/// Handlers function for cfgrwnx messages
static const struct fhost_cntrl_cfgrwnx_handler fhost_cntrl_cfgrwnx_msg_handlers[] =
{
    {CFGRWNX_HW_FEATURE_CMD, fhost_cntrl_cfgrwnx_hw_feature},
    {CFGRWNX_GET_CAPA_CMD, fhost_cntrl_cfgrwnx_get_capa},
    {CFGRWNX_SET_KEY_CMD, fhost_cntrl_cfgrwnx_set_key},
    {CFGRWNX_SCAN_CMD, fhost_cntrl_cfgrwnx_scan},
    {CFGRWNX_CONNECT_CMD, fhost_cntrl_cfgrwnx_connect},
    {CFGRWNX_DISCONNECT_CMD, fhost_cntrl_cfgrwnx_disconnect},
    {CFGRWNX_CTRL_PORT_CMD, fhost_cntrl_cfgrwnx_ctrl_port},
#if NX_SYS_STAT
    {CFGRWNX_SYS_STATS_CMD, fhost_cntrl_cfgrwnx_sys_stats},
#endif //NX_SYS_STAT
    {CFGRWNX_SCAN_RESULTS_CMD, fhost_cntrl_cfgrwnx_scan_results},
    {CFGRWNX_SET_VIF_TYPE_CMD, fhost_cntrl_cfgrwnx_set_vif_type},
    {CFGRWNX_MONITOR_CFG_CMD, fhost_cntrl_cfgrwnx_monitor_cfg},
    {CFGRWNX_EXTERNAL_AUTH_STATUS_RESP, fhost_cntrl_cfgrwnx_external_auth_status},
#if NX_BEACONING
    {CFGRWNX_START_AP_CMD, fhost_cntrl_cfgrwnx_start_ap},
    {CFGRWNX_STOP_AP_CMD, fhost_cntrl_cfgrwnx_stop_ap},
    //{CFGRWNX_SET_EDCA_CMD, fhost_cntrl_cfgrwnx_set_edca},
    {CFGRWNX_BCN_UPDATE_CMD, fhost_cntrl_cfgrwnx_bcn_update},
    {CFGRWNX_STA_ADD_CMD, fhost_cntrl_cfgrwnx_sta_add},
    {CFGRWNX_STA_REMOVE_CMD, fhost_cntrl_cfgrwnx_sta_remove},
    //{CFGRWNX_KEY_SEQNUM_CMD, fhost_cntrl_cfgrwnx_key_seqnum},
#endif // NX_BEACONING
#ifdef CONFIG_OFFCHANNEL
    {CFGRWNX_REMAIN_ON_CHANNEL_CMD, fhost_cntrl_cfgrwnx_roc},
    {CFGRWNX_CANCEL_REMAIN_ON_CHANNEL_CMD, fhost_cntrl_cfgrwnx_cancel_roc},
#endif
    {0,NULL}
};

/**
 ****************************************************************************************
 * @brief Process a CFGRWNX message
 *
 * Find and call callback associated to CFGRWNX message.
 *
 * @param[in] msg Message to process
 ****************************************************************************************
 */
static void fhost_cntrl_cfgrwnx_msg_process(struct fhost_msg *msg)
{
    const struct fhost_cntrl_cfgrwnx_handler *handler = fhost_cntrl_cfgrwnx_msg_handlers;
    int index = FHOST_MSG_INDEX(msg->id);

    while (handler->index)
    {
        if (handler->index == index)
        {
            handler->func(msg->data);
            break;
        }
        handler++;
    }
}

/**
 ****************************************************************************************
 * @brief Process a FHOST_TX message
 *
 * Call callback functions.
 *
 * @param[in] msg Message to process
 ****************************************************************************************
 */
static void fhost_cntrl_txcfm_cb_msg_process(struct fhost_msg *msg)
{
    tx_cfm_callback_t *cb = (tx_cfm_callback_t *)msg->data;
    if (cb->cb_func) {
        cb->cb_func(cb->frame_id, cb->acknowledged, cb->arg);
        rtos_free(cb);
    } else {
        aic_dbg("invalid txcfm_cb func\r\n");
    }
}

/****************************************************************************************
 * Task loop and helper
 ***************************************************************************************/


/**
 ****************************************************************************************
 * @brief Contrl task main loop
 *
 ****************************************************************************************
 */
#if 1
static void fhost_cntrl_task(void *param)
{
    struct fhost_msg msg;

    for( ;; )
    {
        #if FHOST_CNTRL_MSG_USE_QUEUE
        rtos_queue_read(fhost_cntrl_queue, &msg, -1, false);
        #else
        rtos_signal_recv(wifi_cntrl_task, &msg, sizeof(struct fhost_msg));
        #endif
#ifdef PLATFORM_SUNPLUS_ECOS
        if (fhost_cntrl_task_exit_flag) {
            break;
        }
#endif
        switch (FHOST_MSG_TYPE(msg.id))
        {
            case FHOST_MSG_CFGRWNX:
                fhost_cntrl_cfgrwnx_msg_process(&msg);
                break;
            case FHOST_MSG_TXCFM_CB:
                fhost_cntrl_txcfm_cb_msg_process(&msg);
                break;
            default:
                aic_dbg("Task CNTRL: unsupported message received (%d/%d)\n",
                      FHOST_MSG_TYPE(msg.id),
                      FHOST_MSG_INDEX(msg.id));
                break;
        }
    }
#ifdef PLATFORM_SUNPLUS_ECOS
    rtos_semaphore_signal(fhost_cntrl_task_exit_sem, false);
#endif
    aic_dbg("Exit fhost_cntrl_task\r\n");
}
#endif
/****************************************************************************************
 * Task interface
 ***************************************************************************************/
int fhost_cntrl_init(struct rwnx_hw *rwnx_hw)
{
    int i = 0;
    RWNX_DBG(RWNX_FN_ENTRY_STR);
    cntrl_rwnx_hw = rwnx_hw;
    cntrl_rwnx_hw->roc_elem = NULL;

    #if FHOST_CNTRL_MSG_USE_QUEUE
    if (rtos_queue_create(sizeof(struct fhost_msg), FHOST_CNTRL_QUEUE_SIZE, &fhost_cntrl_queue, "fhost_cntrl_queue"))
        return 3;
    #endif
    if (rtos_semaphore_create(&fhost_cntrl_task_exit_sem, "fhost_cntrl_task_exit_sem", 0x7FFFFFFF, 0)) {
        aic_dbg("fhost_cntrl_task_exit_sem create fail\n");
        return 2;
    }
    #if 1
    if (rtos_task_create(fhost_cntrl_task, "fhost_cntrl_task", CONTROL_TASK,
                         fhost_cntrl_stack_size, NULL, fhost_cntrl_priority,
                         &wifi_cntrl_task))
        return 1;
    #endif

    for (i = 0; i < FHOST_CNTRL_MAX_LINK; i++)
    {
        //cfgrwnx_link[i].queue     = NULL;
        cfgrwnx_link[i].sock_recv = -1;
        cfgrwnx_link[i].sock_send = -1;
    }

    if (rtos_mutex_create(&cfgrwnx_link_queue_lock, "cfgrwnx_link_q_lock"))
        return 1;
    
    return 0;
}

void fhost_cntrl_deinit(struct rwnx_hw *rwnx_hw)
{
    int i;
    if (wifi_cntrl_task) {
        fhost_cntrl_task_exit_flag = true;
        rtos_queue_write(fhost_cntrl_queue, "fhost_cntrl_task Exit Signal", -1, false);
        rtos_semaphore_wait(fhost_cntrl_task_exit_sem, -1);
        fhost_cntrl_task_exit_flag = false;
        rtos_task_delete(wifi_cntrl_task);
        wifi_cntrl_task = NULL;
    }

    #if FHOST_CNTRL_MSG_USE_QUEUE
    if (fhost_cntrl_queue) {
        rtos_queue_delete(fhost_cntrl_queue);
        fhost_cntrl_queue = NULL;
    }
    #endif
    if (fhost_cntrl_task_exit_sem) {
        rtos_semaphore_delete(fhost_cntrl_task_exit_sem);
    }
    for (i = 0; i < FHOST_CNTRL_MAX_LINK; i++)
    {
        fhost_cntrl_cfgrwnx_link_close(&cfgrwnx_link[i]);
    }
    
    if (cfgrwnx_link_queue_lock) {
        rtos_mutex_delete(cfgrwnx_link_queue_lock);
        cfgrwnx_link_queue_lock = NULL;
    }
    cntrl_rwnx_hw = NULL;
}

int fhost_cntrl_write_msg(int id, void *msg_data, int len, int timeout, bool isr)
{
    struct fhost_msg msg;

    msg.id         = id;
    msg.len        = len;
    msg.data       = msg_data;

    return rtos_queue_write(fhost_cntrl_queue, &msg, timeout, isr);
}

void fhost_cntrl_cfgrwnx_link_close(struct fhost_cntrl_link *close_link)
{
    if (close_link->sock_recv >= 0)
    {
        close(close_link->sock_recv);
        close_link->sock_recv = -1;
    }
    if (close_link->sock_send >= 0)
    {
        close(close_link->sock_send);
        close_link->sock_send = -1;
    }
    if (close_link->queue) {
        rtos_queue_delete(close_link->queue);
        close_link->queue = NULL;
    }
}
#if 1 //def PLATFORM_SUNPLUS_ECOS
struct fhost_cntrl_link *fhost_cntrl_cfgrwnx_link_open(void)
{
    struct sockaddr_in recv_addr;
    unsigned int i = 0;
    char queue_name[32];

    for (i = 0; i < FHOST_CNTRL_MAX_LINK; i++)
    {
        if (cfgrwnx_link[i].sock_recv == -1)
            break;
    }

    if (i == FHOST_CNTRL_MAX_LINK)
        return NULL;

    memset(queue_name, 0, sizeof(queue_name));
    sprintf(queue_name, "cfgrwnx_link[%d].queue", i);
    if (rtos_queue_create(sizeof(void *), FHOST_CNTRL_QUEUE_CFGRWNX_RESP_SIZE,
                          &cfgrwnx_link[i].queue, queue_name))
        return NULL;

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("sock_recv failed\n");
        goto err;
    }

    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = PF_INET;
    recv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    recv_addr.sin_port = htons(0);
    if (bind(sock, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0) {
        printf("sock_bind failed\n");
        goto err;
    }

    if (listen(sock, 1) < 0) {
        printf("sock_listen failed\n");
        goto err;
    }

    socklen_t len = (socklen_t)sizeof(recv_addr);
    if(getsockname(sock, (struct sockaddr *)&recv_addr, &len) < 0) {
        printf("sock_getsockname failed\n");
        goto err;
    }

    cfgrwnx_link[i].sock_send = socket(PF_INET, SOCK_STREAM, 0);
    if (cfgrwnx_link[i].sock_send < 0) {
        printf("sock_send failed\n");
        goto err;
    }

    if (connect(cfgrwnx_link[i].sock_send, (struct sockaddr *)&recv_addr, len) < 0) {
        printf("sock_connect failed\n");
        goto err;
    }

    if ((cfgrwnx_link[i].sock_recv = accept(sock, (struct sockaddr *)&recv_addr, &len)) < 0) {
        printf("sock_accept failed\n");
        goto err;
    }
    close(sock);
    return &cfgrwnx_link[i];

  err:
    close(sock);
    fhost_cntrl_cfgrwnx_link_close(&cfgrwnx_link[i]);
    return NULL;
}

#else
struct fhost_cntrl_link *fhost_cntrl_cfgrwnx_link_open(void)
{
    struct sockaddr_in recv_addr;
    struct sockaddr_in send_addr;
    unsigned int port = 0;
    unsigned int i = 0;
    char queue_name[32];

    for (i = 0; i < FHOST_CNTRL_MAX_LINK; i++)
    {
        if (cfgrwnx_link[i].sock_recv == -1)
            break;
    }

    if (i == FHOST_CNTRL_MAX_LINK)
        return NULL;

    memset(queue_name, 0, sizeof(queue_name));
    sprintf(queue_name, "cfgrwnx_link[%d].queue", i);
    if (rtos_queue_create(sizeof(void *), FHOST_CNTRL_QUEUE_CFGRWNX_RESP_SIZE,
                          &cfgrwnx_link[i].queue, queue_name))
        return NULL;

    port = CFGRWNX_PORT + i;
    cfgrwnx_link[i].sock_recv = socket(PF_INET, SOCK_DGRAM, 0);
    if (cfgrwnx_link[i].sock_recv == -1)
        goto err;

    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = PF_INET;
    recv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    recv_addr.sin_port = htons(port);
    if (bind(cfgrwnx_link[i].sock_recv, (struct sockaddr *)if (bind(cfgrwnx_link[i].sock_recv, (struct sockaddr *)bind(cfgrwnx_link[i].sock_recv, (struct sockaddr *)(struct sockaddr *)(struct sockaddr_in *)&recv_addrrecv_addrrecv_addrrecv_addr, sizeof(recv_addr))recv_addr, sizeof(recv_addr)) < 0)
        goto err;


    cfgrwnx_link[i].sock_send = socket(PF_INET, SOCK_DGRAM, 0);
    if (cfgrwnx_link[i].sock_send == -1)
        goto err;
    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = PF_INET;
    recv_addr.sin_addr.s_addr = htonl(0);
    recv_addr.sin_port = htons(port + 100);
    if (bind(cfgrwnx_link[i].sock_send, (struct sockaddr *)if (bind(cfgrwnx_link[i].sock_send, (struct sockaddr *)(struct sockaddr *)(struct sockaddr_in *)&recv_addrrecv_addrrecv_addr, sizeof(recv_addr))recv_addr, sizeof(recv_addr)) < 0)
        goto err;
    memset(&send_addr, 0, sizeof(send_addr));
    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    send_addr.sin_port = htons(port);
    if (connect(cfgrwnx_link[i].sock_send, (struct sockaddr *)if (connect(cfgrwnx_link[i].sock_send, (struct sockaddr *)connect(cfgrwnx_link[i].sock_send, (struct sockaddr *)(struct sockaddr_in *)&send_addrsend_addrsend_addr, sizeof(send_addr))send_addr, sizeof(send_addr)) < 0)
        goto err;

    return &cfgrwnx_link[i];

  err:
    fhost_cntrl_cfgrwnx_link_close(&cfgrwnx_link[i]);
    return NULL;
}
#endif

int fhost_cntrl_set_mac_vif_type(struct fhost_cntrl_link *link, int fhost_vif_idx,
                                 enum mac_vif_type type, bool p2p)
{
    struct cfgrwnx_set_vif_type cmd;
    struct cfgrwnx_resp resp;

    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = CFGRWNX_SET_VIF_TYPE_CMD;
    cmd.hdr.resp_queue = link->queue;
    cmd.fhost_vif_idx = fhost_vif_idx;
    cmd.type = type;
    cmd.p2p = p2p;

    resp.hdr.len = sizeof(resp);
    resp.hdr.id = CFGRWNX_SET_VIF_TYPE_RESP;

    if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) ||
        (resp.status != CFGRWNX_SUCCESS))
    {
        return -1;
    }

    return 0;
}

#if NX_FHOST_MONITOR
int fhost_cntrl_monitor_cfg(struct fhost_cntrl_link *link, int fhost_vif_idx,
                            struct fhost_vif_monitor_cfg *cfg)
{
    struct cfgrwnx_monitor_cfg cmd;
    struct cfgrwnx_resp resp;
    struct fhost_vif_tag *fhost_vif = &fhost_env.vif[fhost_vif_idx];
    struct mac_chan_def *chan;

    if (!fhost_vif->mac_vif || (fhost_vif->mac_vif->type != VIF_MONITOR)) {
        return -1;
    }

    // Test channel config
    chan = fhost_chan_get(cfg->chan.prim20_freq);
    if (!chan) {
        return -1;
    }
    if (cfg->chan.tx_power > chan->tx_power)
        cfg->chan.tx_power = chan->tx_power;

    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = CFGRWNX_MONITOR_CFG_CMD;
    cmd.hdr.resp_queue = link->queue;
    cmd.fhost_vif_idx = fhost_vif_idx;
    cmd.cfg = *cfg;

    resp.hdr.len = sizeof(resp);
    resp.hdr.id = CFGRWNX_MONITOR_CFG_RESP;

    if (fhost_cntrl_cfgrwnx_cmd_send(&cmd.hdr, &resp.hdr) ||
        (resp.status != CFGRWNX_SUCCESS)) {
        return -1;
    }
    fhost_tx_task_init();
    return 0;
}
#endif // NX_FHOST_MONITOR

int fhost_cntrl_cfgrwnx_cmd_send(struct cfgrwnx_msg_hdr *cmd,
                                 struct cfgrwnx_msg_hdr *resp)
{
    struct cfgrwnx_msg_hdr *msg_hdr = NULL;
    int err;

    rtos_mutex_lock(cfgrwnx_link_queue_lock, -1);
    // Send the command to the FHOST control thread
    err = fhost_cntrl_write_msg(FHOST_MSG_ID(FHOST_MSG_CFGRWNX, cmd->id),
                                cmd, cmd->len, 10, false);
    if (err)
    {
        aic_dbg("[CFGRWNX] Command write error\n");
        goto cmd_send_end;
    }

    // return immediately if no response is expected
    if (!resp)
        goto cmd_send_end;

    // aic_dbg("%s msg_hdr->id %d, resp->id %d\n", __func__, cmd->id, resp->id);
    // Wait for the response from the FHOST control thread
    if (cmd->resp_queue != NULL)
        rtos_queue_read(cmd->resp_queue, &msg_hdr, -1, false);
    if (msg_hdr != NULL && resp != NULL) {
        if (msg_hdr->id != resp->id)
        {
            aic_dbg("[CFGRWNX] Unexpected response ID, msg_hdr->id %d, resp->id %d\n", msg_hdr->id, resp->id);
            err = -1;
            goto cmd_send_end;
        }

        if (msg_hdr->len > resp->len)
        {
            aic_dbg("[CFGRWNX] Response buffer too small for received response\n");
            err = -1;
            goto cmd_send_end;
        }
    } else
        aic_dbg("%s, msg_hdr and resp is null\n", __func__);
    memcpy(resp, msg_hdr, msg_hdr->len);

    rtos_free(msg_hdr);
cmd_send_end:
    rtos_mutex_unlock(cfgrwnx_link_queue_lock);
    return err;
}

int fhost_cntrl_txcfm_cb_msg_send(tx_cfm_callback_t *cb)
{
    int err;

    // Send the command to the FHOST control thread
    err = fhost_cntrl_write_msg(FHOST_MSG_ID(FHOST_MSG_TXCFM_CB, 0),
                                cb, sizeof(tx_cfm_callback_t), -1, false);
    if (err) {
        aic_dbg("Command write error\n");
        return err;
    }

    // return immediately
    return 0;
}

int fhost_cntrl_cfgrwnx_event_send(struct cfgrwnx_msg_hdr *msg_hdr, int sock)
{
    ASSERT_ERR(sock >= 0);

    //TRACE_FHOST("fhost_cntrl_cfgrwnx_event_send %d (%d)\r\n", msg_hdr->id, msg_hdr->len);

    if (send(sock, msg_hdr, msg_hdr->len, 0) < 0)
    {
        TRACE_FHOST("E %d (%d)\r\n",
                    msg_hdr->id, msg_hdr->len);
        return -1;
    }

    return 0;
}

int fhost_cntrl_cfgrwnx_event_peek_header(struct fhost_cntrl_link *link,
                                          struct cfgrwnx_msg_hdr *msg_hdr)
{
    int ret = recv(link->sock_recv, msg_hdr, (sizeof *msg_hdr), MSG_PEEK);
    if (ret < 0) {
        TRACE_FHOST("Peek errno %d\r\n", ret);
        return -1;
    }


    return 0;
}

int fhost_cntrl_cfgrwnx_event_get(struct fhost_cntrl_link *link, void *event, int len)
{
    int read;

    read = recv(link->sock_recv, event, len, MSG_DONTWAIT);
    if (read < 0) {
        TRACE_FHOST("Get err\r\n");
        return -1;
    }

    return read;
}

int fhost_cntrl_cfgrwnx_event_discard(struct fhost_cntrl_link *link,
                                      struct cfgrwnx_msg_hdr *msg_hdr)
{
    if (msg_hdr == NULL)
        return -1;

    if (msg_hdr->id == CFGRWNX_SCAN_RESULT_EVENT)
    {
        struct cfgrwnx_scan_result event;
        if (recv(link->sock_recv, &event, sizeof(event), MSG_DONTWAIT) < 0)
            return -1;
        if (event.payload)
            rtos_free(event.payload);
    }
    else if (msg_hdr->id == CFGRWNX_CONNECT_EVENT)
    {
        struct cfgrwnx_connect_event event;
        if (recv(link->sock_recv, &event, sizeof(event), MSG_DONTWAIT) < 0)
            return -1;
        if (event.req_resp_ies)
            rtos_free(event.req_resp_ies);
    }
    #if 0
    else if (msg_hdr->id == CFGRWNX_RX_MGMT_EVENT)
    {
        struct cfgrwnx_rx_mgmt_event event;
        if (recv(link->sock_recv, &event, sizeof(event), MSG_DONTWAIT) < 0)
            return -1;

        if (event.payload) {
            rtos_free(event.payload);
            event.payload = NULL;
        }
    }
    #endif
    else if (recv(link->sock_recv, msg_hdr, sizeof(*msg_hdr), MSG_DONTWAIT) < 0)
    {
        return -1;
    }

    return 0;
}

int fhost_cntrl_me_set_lp_level(uint8_t lp_level)
{
    int ret;
    struct me_set_lp_level_req req;

    req.lp_level = lp_level;

    ret = rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_ME, ME_SET_LP_LEVEL_REQ, sizeof(struct me_set_lp_level_req), &req, 1, ME_SET_LP_LEVEL_CFM, NULL);
    if (ret)
        return -1;

    return 0;
}

int fhost_cntrl_mm_set_filter(uint32_t value)
{
    int ret;
    struct mm_set_filter_req req;

    req.filter = value;

    ret = rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_MM, MM_SET_FILTER_REQ, sizeof(struct mm_set_filter_req), &req, 1, MM_SET_FILTER_CFM, NULL);
    if (ret)
        return -1;

    return 0;
}

int fhost_cntrl_scan_cancel_req(void)
{
    int ret = 0;
    struct scanu_scan_cancel_req req;
    
    aic_dbg("%s \r\n", __func__);
    ret = rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_SCANU, SCANU_CANCEL_REQ, sizeof(struct scanu_scan_cancel_req), &req, 1, SCANU_CANCEL_CFM, NULL);
    if (ret)
        return -1;

    return 0;
}

int fhost_cntrl_mm_set_monitor_channel(int vif_idx, int chan_num, int band_width)
{
    struct me_config_monitor_req conf_req;
    struct me_config_monitor_cfm conf_cfm;
    struct mac_chan_def *chan;
    int offset = 0;
    struct fhost_vif_tag *fhost_vif = &fhost_env.vif[vif_idx];

    if (!fhost_vif->mac_vif || (fhost_vif->mac_vif->type != VIF_MONITOR)) {
        return -1; 
    }
    conf_req.chan.prim20_freq = phy_channel_to_freq((chan_num > 14), chan_num);
    chan = fhost_chan_get(conf_req.chan.prim20_freq);
    if (!chan) {
        return -1;
    }
    // Set parameters for the ME_CONFIG_MONITOR_REQ message
    conf_req.chan.band = chan->band;
    conf_req.chan.tx_power = chan->tx_power;
    conf_req.chan.type = band_width;
    if (band_width == PHY_CHNL_BW_40){
            uint8_t k = 0, found = 0;
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
                    conf_req.chan.type = PHY_CHNL_BW_20; //20M only
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
    conf_req.chan.center1_freq = conf_req.chan.prim20_freq + offset;
    conf_req.chan.center2_freq = 0;
    conf_req.uf = false;
    conf_req.chan_set = true;

    if (rwnx_send_msg_tx(cntrl_rwnx_hw, TASK_ME, ME_CONFIG_MONITOR_REQ, sizeof(conf_req), &conf_req, 1, ME_CONFIG_MONITOR_CFM, &conf_cfm)) {
        return -1;
    }

    return 0; 
}
/**
 * @}
 */
