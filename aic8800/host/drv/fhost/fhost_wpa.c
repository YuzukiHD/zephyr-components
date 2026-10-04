/**
 ****************************************************************************************
 *
 * @file fhost_wpa.c
 *
 * @brief Helper functions for wpa_supplicant task management
 *
 * Copyright (C) RivieraWaves 2017-2019
 *
 ****************************************************************************************
 */

#include "mac_frame.h"
#include "fhost.h"
#include "fhost_wpa.h"
#include "fhost_wpa_config.h"
#include "fhost_cntrl.h"
#include "fhost_rx.h"
#include "net_al.h"
#include "aic_log.h"
#include "lwip/sockets.h"
#include "porting.h"
#include "ap.h"

#if defined(PLATFORM_ALLWIN_RT_THREAD) || defined(PLATFORM_ALLWIN_FREERTOS)
#include "wifi_driver_event.h"
#include "wifi.h"
#ifdef PLATFORM_ALLWIN_RT_THREAD
#include <pthread.h>
#endif
#endif

/**
 ****************************************************************************************
 * @addtogroup FHOST_WPA
 * @{
 ****************************************************************************************
 */
/// wpa_supplicant callback definition
struct fhost_wpa_event_cb
{
    /// The callback function
    fhost_wpa_cb_t func;
    /// The callback private parameter
    void *arg;
    /// The events bitfield for this callback
    uint32_t events;
};

/// Private structure for @ref fhost_wpa_wait_event
struct fhost_wpa_target_event
{
    /// Event expected
    enum fhost_wpa_event event;
    /// Task to notify when event is received
    rtos_semaphore sem;
};

/// Maximum number of callback allowed
#define FHOST_WPA_EVENT_CB_CNT 2

/// wpa_supplicant parameters for one FHOST interface
struct fhost_wpa_tag
{
    /// handle of WPA task
    rtos_task_handle task;
    /// state
    enum fhost_wpa_state state;
    /// Config of WPA task
    struct fhost_wpa_config config;
    /// Socket for WPA control interface
    int ctrl_sock;
    /// Cookie for WPA control interface
    char cookie[24];
    /// Cookie length for WPA control interface
    int cookie_len;
    /// Table of callback for WPA event
    struct fhost_wpa_event_cb cb[FHOST_WPA_EVENT_CB_CNT];
    /// Network ID WPA is connected to
    int network_id;
    /// MGMT RX filter
    uint32_t rx_filter;
};

extern aic_wifi_event_cb g_aic_wifi_event_cb;
/// wpa_supplicant configuration for all FHOST interfaces
struct fhost_wpa_tag fhost_wpa[NX_VIRT_DEV_MAX];
rtos_semaphore fhost_wpa_end_sema = NULL, fhost_wpa_exit_sema = NULL;
static rtos_task_handle fhost_wpa_task = NULL;

/// main function of wpa task
//void *wpa_supplicant_main(uint32_t arc, void *env);
static void fhost_wpa_event_process(enum fhost_wpa_event event, void *param,
                                    int param_len, int fhost_vif_idx);
int wpa_rwnx_driver_send(void *msg);


/**
 ****************************************************************************************
 * @brief Get wpa configuration structure for an interface
 *
 * @param[in] fhost_vif_idx  Index of the FHOST interface
 * @return pointer to configuration structure for the interface or NULL for invalid index
 ****************************************************************************************
 */
__INLINE struct fhost_wpa_tag *fhost_wpa_get_conf(int fhost_vif_idx)
{
    if ((fhost_vif_idx < 0) || (fhost_vif_idx >= NX_VIRT_DEV_MAX))
        return NULL;
    return &fhost_wpa[fhost_vif_idx];
}

/**
 ****************************************************************************************
 * @brief Reset interface parameters to its default values.
 *
 * @param[in] wpa_conf  wpa structure for the interface
 ****************************************************************************************
 */
static void fhost_wpa_reset(struct fhost_wpa_tag *wpa_conf)
{
    wpa_conf->state = FHOST_WPA_STATE_STOPPED;
    wpa_conf->ctrl_sock = -1;
    wpa_conf->config.fhost_vif_idx = -1;
    wpa_conf->task = RTOS_TASK_NULL;
    wpa_conf->config.iface_name[0] = '\0';
    wpa_conf->network_id = -1;
    wpa_conf->cookie_len = 0;
    memset(wpa_conf->cb, 0, sizeof(wpa_conf->cb));
}

/**
 ****************************************************************************************
 * @brief Process function for @ref FHOST_WPA_STARTED event
 *
 * If ctrl port is not null the function opens a connection and retrieves cookie.
 *
 * @param[in] param      Event parameter (contains ctrl port id when cast to int)
 * @param[in] wpa_conf   wpa structure for the interface
 * @return 0 In case of success and !=0 if the control interface cannot be opened.
 * In this case the wpa_supplicant is killed.
 ****************************************************************************************
 */
static int fhost_wpa_started(void *param, struct fhost_wpa_tag *wpa_conf)
{
    int port = (int)param;

    if (!wpa_conf->config.ctrl_itf)
        return 0;


    wpa_conf->ctrl_sock = fhost_open_loopback_udp_sock(port);

    if (wpa_conf->ctrl_sock == -1)
    {
        AIC_LOG_PRINTF("Failed to connect to WPA ctrl interface (sock=%d)\n",
                    wpa_conf->ctrl_sock);

        // Delete task (this will leak memory)
        rtos_task_delete(wpa_conf->task);

        // And process a fake EXIT event to clean everything
        fhost_wpa_event_process(FHOST_WPA_EXIT, (void *)-2, 0,
                                wpa_conf->config.fhost_vif_idx);
        return -1;
    }

    return 0;
}

/**
 ****************************************************************************************
 * @brief Process function for @ref FHOST_WPA_EXIT event
 *
 * @param[in] wpa_conf   wpa structure for the interface
 ****************************************************************************************
 */
static void fhost_wpa_exit(struct fhost_wpa_tag *wpa_conf)
{
    if (wpa_conf->ctrl_sock >= 0)
        close(wpa_conf->ctrl_sock);

    fhost_rx_set_mgmt_cb(NULL, NULL);
    fhost_wpa_reset(wpa_conf);
}

/**
 ******************************************************************************
 * @brief Generic wpa event callback to notify a waiting task when expected
 * event is received
 *
 * Besides the target event, the waiting task is also always notified when
 * the @ref FHOST_WPA_EXIT event is received (as no other event will be
 * received after this one).
 * Registered by @ref fhost_wpa_wait_event_register and unregistered by @ref
 * fhost_wpa_wait_event_unregister. The function unregister itself when the
 * waiting task is notified.
 *
 * @param[in] fhost_vif_idx  Index of the FHOST interface
 * @param[in] event          Event generated by the WPA task
 * @param[in] event_param    Event parameter (not used)
 * @param[in] arg            Registered private parameter, in this case info on
 *                           the expected event and the waiting task
 ******************************************************************************
 */
static void fhost_wpa_wait_event(int fhost_vif_idx, enum fhost_wpa_event event,
                                 void *event_param, void *arg)
{
    struct fhost_wpa_target_event *target = arg;

    if ((event == target->event) || (event == FHOST_WPA_EXIT))
    {
        fhost_wpa_cb_unregister(fhost_vif_idx, fhost_wpa_wait_event);
        //rtos_task_notify(target->task, 0, false);
        if (target->sem) {
            AIC_LOG_PRINTF("target->sem %x\r\n", target->sem);
            rtos_semaphore_signal(target->sem, 0);
        }
        if (event == FHOST_WPA_EXIT) {
            AIC_LOG_PRINTF("fhost_wpa_exit_sema %x\r\n", fhost_wpa_exit_sema);
            rtos_semaphore_signal(fhost_wpa_exit_sema, 0);
        }
    }
}

/**
 ******************************************************************************
 * @brief Generic function to register WPA event callback that will notify
 * the calling task when a specific event occurs.
 *
 * Register @ref fhost_wpa_wait_event as callback. After calling this function
 * the user should call @ref rtos_task_wait_notification to wait for the
 * selected event. To avoid deadlock, the event @ref FHOST_WPA_EXIT is always
 * included in the event set.
 * The caller must take care of race condition between wpa events and callback
 * registration.
 *
 * @param[in] fhost_vif_idx  Index of the FHOST interface.
 * @param[in] event          Expected Event.
 * @param[in] target         Registered as callback private parameter. No need
 *                           to initialize it but the pointer MUST be valid
 *                           until rtos_task_wait_notification is called.
 * @return 0 on success and != 0 otherwise
 ******************************************************************************
 */
static int fhost_wpa_wait_event_register(int fhost_vif_idx, enum fhost_wpa_event event,
                                         struct fhost_wpa_target_event *target)
{
    target->event = event;
//    target->task = rtos_get_task_handle();

    //if (rtos_task_init_notification(target->task))
    //   return -1;
    if (rtos_semaphore_create(&target->sem, "target->sem", 1, 0))
        return -1;

    int ret = fhost_wpa_cb_register(fhost_vif_idx, CO_BIT(event) | CO_BIT(FHOST_WPA_EXIT),
                                 fhost_wpa_wait_event, target);
    if (ret) {
        rtos_semaphore_delete(target->sem);
        target->sem = NULL;
    }
    return ret;
}

/**
 ******************************************************************************
 * @brief Unregister WPA event callback
 *
 * Unregister the function registered by @ref fhost_wpa_wait_event_register.
 * There no need to call this function when a notification has been received.
 *
 * @param[in] fhost_vif_idx  Index of the FHOST interface
 * @return 0 on success and != 0 otherwise
 ******************************************************************************
 */
static int fhost_wpa_wait_event_unregister(int fhost_vif_idx)
{
    return fhost_wpa_cb_unregister(fhost_vif_idx, fhost_wpa_wait_event);
}

/**
 ****************************************************************************************
 * @brief Call registered callback when event is received from WPA task
 *
 * Loop over all registered callbacks and call them if associated to this event.
 *
 * @param[in] cb             Table of callback registered for the interface
 * @param[in] fhost_vif_idx  Index of the FHOST interface
 * @param[in] event          Event from WPA task
 * @param[in] param          Event parameter
 ****************************************************************************************
 */
static void fhost_wpa_call_event_cb(struct fhost_wpa_event_cb *cb, int fhost_vif_idx,
                                    enum fhost_wpa_event event, void *param)
{
    int i;

    for (i = 0 ; i < FHOST_WPA_EVENT_CB_CNT; i++, cb++)
    {
        if (cb->events & CO_BIT(event))
            cb->func(fhost_vif_idx, event, param, cb->arg);
    }
}

/**
 ****************************************************************************************
 * @brief Process events form WPA task
 *
 * @note This function is called in the context of the WPA task, and as such it cannot
 * block upon WPA task (e.g. it cannot send WPA command).
 *
 * @param[in] event          Event from WPA task
 * @param[in] param          Event parameter
 * @param[in] param_len      Length, in bytes, of the param buffer
 * @param[in] fhost_vif_idx  Index of the FHOST interface
 ****************************************************************************************
 */
static void fhost_wpa_event_process(enum fhost_wpa_event event, void *param,
                                    int param_len, int fhost_vif_idx)
{
    struct fhost_wpa_tag *wpa_conf = fhost_wpa_get_conf(fhost_vif_idx);
    struct fhost_wpa_event_cb cb_cpy[FHOST_WPA_EVENT_CB_CNT];
    struct fhost_wpa_event_cb *cb;

    cb = wpa_conf->cb;
    switch (event)
    {
        case FHOST_WPA_EXIT:
            AIC_LOG_PRINTF("WPA enter FHOST_WPA_STATE_STOPPED\n");
            wpa_conf->state = FHOST_WPA_STATE_STOPPED;
            // copy callbacks as fhost_wpa_exit will reset them
            memcpy(cb_cpy, wpa_conf->cb, sizeof(cb_cpy));
            cb = cb_cpy;

            if (fhost_wpa_end_sema) {
                rtos_semaphore_wait(fhost_wpa_end_sema, -1);
                rtos_semaphore_delete(fhost_wpa_end_sema);
                fhost_wpa_end_sema = NULL;
            }

            fhost_wpa_exit(wpa_conf);
            break;
        case FHOST_WPA_STARTED:
            wpa_conf->state = FHOST_WPA_STATE_NOT_CONNECTED;
            if (fhost_wpa_started(param, wpa_conf))
                return;
            break;
        case FHOST_WPA_CONNECTED:
            if(FHOST_WPA_STATE_CONNECTED == wpa_conf->state)
                return;
            AIC_LOG_PRINTF("WPA enter FHOST_WPA_STATE_CONNECTED\n");
            wpa_conf->state = FHOST_WPA_STATE_CONNECTED;
            extern wifi_drv_event_cbk aw_aic_wifi_event_cb;
            AIC_WIFI_MODE mode = aic_wifi_get_mode();
            if (aw_aic_wifi_event_cb &&  mode == WIFI_MODE_STA) {
                wifi_drv_event drv_event;
                struct wifi_sta_event dev_event;
                dev_event.event_type = WIFI_STA_EVENT_ON_ASSOC;
                drv_event.type = WIFI_DRV_EVENT_STA;
                drv_event.node.sta_event = dev_event;
                aw_aic_wifi_event_cb(&drv_event);
            }
            if (g_aic_wifi_event_cb &&  (mode == WIFI_MODE_STA)) {
                aic_wifi_event_data enData = {0};
                enData.data.join_data.reserved[0] = 0;
                g_aic_wifi_event_cb(JOIN_SUCCESS_EVENT, &enData);
            }
            break;
        case FHOST_WPA_DISCONNECTED:
            wpa_conf->state = FHOST_WPA_STATE_NOT_CONNECTED;
            break;
        default:
            return;
    }

    fhost_wpa_call_event_cb(cb, fhost_vif_idx, event, param);
}

/**
 ****************************************************************************************
 * @brief Callback function for non processed MGMT frames
 *
 * Management frames not processed by the wifi task are forwarded to wpa_supplicant
 *
 * @param[in] info  Frame information.
 * @param[in] arg   Pointer to struct fhost_wpa_tag.
 ****************************************************************************************
 */
static void fhost_wpa_rx_cb(struct fhost_frame_info *info, void *arg)
{
    struct fhost_wpa_tag *wpa_conf = arg;
    struct mac_hdr *hdr = (struct mac_hdr *)info->payload;
    struct fhost_vif_tag *fhost_vif;
    struct wpa_rwnx_driver_msg *event;

    // aic_dbg("%s mgmt %x %d\r\n", __func__, (hdr->fctl & MAC_FCTRL_SUBT_MASK), rtos_now(0));

    if ((info->payload == NULL) ||
        (wpa_conf->config.fhost_vif_idx >= NX_VIRT_DEV_MAX) ||
        (wpa_conf->config.fhost_vif_idx < 0) ||
        ((hdr->fctl & MAC_FCTRL_TYPE_MASK) != MAC_FCTRL_MGT_T) ||
        (wpa_conf->rx_filter & CO_BIT(MAC_FCTRL_SUBTYPE(hdr->fctl))))
        return;

    event = rtos_malloc(sizeof(struct wpa_rwnx_driver_msg));

    fhost_vif = &fhost_env.vif[wpa_conf->config.fhost_vif_idx];

    event->id = CFGRWNX_RX_MGMT_EVENT;
    //event.hdr.len = sizeof(event) + sizeof(struct cfgrwnx_msg_hdr);
    event->msg.rx_mgmt_event.fhost_vif_idx = wpa_conf->config.fhost_vif_idx;
    event->msg.rx_mgmt_event.freq = info->freq;
    event->msg.rx_mgmt_event.rssi = info->rssi;
    event->msg.rx_mgmt_event.length = info->length;
    event->msg.rx_mgmt_event.payload = rtos_malloc(event->msg.rx_mgmt_event.length);
    if (event->msg.rx_mgmt_event.payload == NULL)
        return;

    memcpy(event->msg.rx_mgmt_event.payload, info->payload, event->msg.rx_mgmt_event.length);

    if (wpa_rwnx_driver_send(&event)) {
        aic_dbg("%s fail\r\n", __func__);
        rtos_free(event->msg.rx_mgmt_event.payload);
        event->msg.rx_mgmt_event.payload = NULL;

        rtos_free(event);
        event = NULL;
    }
}

/**
 ****************************************************************************************
 * @brief Send command to wpa_supplicant task
 *
 * @param[in] wpa_conf  wpa structure for the interface
 * @param[in] cmd       Command string (must be NULL terminated)
 * @return 0 if command has been successfully sent to WPA task and != 0 otherwise
 ****************************************************************************************
 */
static int fhost_wpa_send_cmd(struct fhost_wpa_tag *wpa_conf, char *cmd)
{
    struct iovec iovec[2];
    struct msghdr msghdr;
    int res;

    // Retrieve control interface cookie on first command
    if (wpa_conf->cookie_len <= 0)
    {
        strcpy(wpa_conf->cookie, "GET_COOKIE");
        res = send(wpa_conf->ctrl_sock, wpa_conf->cookie,
                 strlen(wpa_conf->cookie), 0);
        if (res < 0) {
            AIC_LOG_PRINTF("Err %s, send res:%d\n", __func__, res);
            return -1;
        }

        res = recv(wpa_conf->ctrl_sock, wpa_conf->cookie,
                   sizeof(wpa_conf->cookie), 0);
        if (res < 0) {
            AIC_LOG_PRINTF("Err %s, recv res:%d\n", __func__, res);
            return -2;
        } else if (res == 0) {
            AIC_LOG_PRINTF("Warn %s, recv res:%d\n", __func__, res);
        }

        wpa_conf->cookie_len = res;
    }
    //AIC_LOG_PRINTF("wpa_conf->cookie %s\r\n", wpa_conf->cookie);

    iovec[0].iov_base = wpa_conf->cookie;
    iovec[0].iov_len = wpa_conf->cookie_len;
    iovec[1].iov_base = cmd;
    iovec[1].iov_len = strlen(cmd);

    if (iovec[1].iov_len == 0) {
        AIC_LOG_PRINTF("Err %s, iov_len=0\n", __func__);
        return -3;
    }

    memset(&msghdr, 0, sizeof(msghdr));
    msghdr.msg_iov = iovec;
    msghdr.msg_iovlen = 2;

    res = sendmsg(wpa_conf->ctrl_sock, &msghdr, 0);
    if (res < 0) {
        AIC_LOG_PRINTF("Err %s, sendmsg res:%d\n", __func__, res);
        return -4;
    } else if (res == 0) {
        AIC_LOG_PRINTF("Warn %s, sendmsg res:%d\n", __func__, res);
    }

    return 0;
}

/**
 ****************************************************************************************
 * @brief Retrieve response from wpa_supplicant task
 *
 * The function first waits up to @p timeout_ms ms for wpa_supplicant to send data on the
 * ctrl interface. An error is returned if this is not the case.
 * Then the response is read in the provided buffer. If no buffer is provided (or if the
 * buffer is too small) a temporary buffer is used to retrieve at up 4 characters.
 * The function then check if the response starts with the "FAIL" string. If so the
 * function returns 1.
 * In any cases the response is also copied in the @p resp buffer (as much as possible)
 * and the size written in updated in @p resp_len.
 *
 * @param[in]     wpa_conf    wpa structure for the interface
 * @param[in]     resp        Buffer to retrieve the response.
 * @param[in,out] resp_len    Size, in bytes, of the response buffer.
 *                            If no error is reported, it is updated with the size
 *                            actually written in the response buffer.
 * @param[in]     timeout_ms  Timeout, in ms, allowed to the wpa_supplicant task to
 *                            respond (<0 means wait forever).
 *
 * @return <0 if an error occurred, 1 if response starts with "FAIL" and 0 otherwise.
 ****************************************************************************************
 */
static int fhost_wpa_get_resp(struct fhost_wpa_tag *wpa_conf, char *resp, int *resp_len,
                              int timeout_ms)
{
    struct timeval timeout;
    fd_set fds;
    char tmp_resp[4];
    char *buf;
    int res, buf_len;
    int recv_flags = 0;

    if (!resp || !resp_len || (*resp_len < 4))
    {
        // Use tmp_resp as dummy buffer
        buf = tmp_resp;
        buf_len = sizeof(tmp_resp);
    }
    else
    {
        buf = resp;
        buf_len = *resp_len;
    }

    if (timeout_ms >= 0)
    {
        FD_ZERO(&fds);
        FD_SET(wpa_conf->ctrl_sock, &fds);
        timeout.tv_sec = (timeout_ms / 1000);
        timeout.tv_usec = (timeout_ms - timeout.tv_sec * 1000) * 1000;

        res = select(wpa_conf->ctrl_sock + 1, &fds, NULL, NULL, &timeout);
        if (res <= 0) {
            AIC_LOG_PRINTF("Err %s, select res:%d\n", __func__, res);
            return -1;
        }

        recv_flags = MSG_DONTWAIT;
    }

    res = recv(wpa_conf->ctrl_sock, buf, buf_len, recv_flags);
    if (res < 0) {
        AIC_LOG_PRINTF("Err %s, recv res:%d\n", __func__, res);
        return -2;
    } else if (res == 0) {
        AIC_LOG_PRINTF("Warn %s, recv res:%d\n", __func__, res);
    }

    if (resp && resp_len)
    {
        if (buf == tmp_resp)
        {
            if (res < *resp_len)
                *resp_len = res;
            memcpy(resp, tmp_resp, *resp_len);
        }
        else
            *resp_len = res;
    }

    if (strncmp(buf, "FAIL", 4) == 0)
        res = 1;
    else
        res = 0;

    return res;
}

/*
 ****************************************************************************************
 * Public functions
 ****************************************************************************************
 */
void fhost_wpa_init(void)
{
    int i;

    for (i = 0; i < NX_VIRT_DEV_MAX; i++)
    {
        fhost_wpa_reset(&fhost_wpa[i]);
    }
}
#include "includes.h"

#include "common.h"
#include "wpa_supplicant_i.h"
//#include "config.h"
#include "aic8800_config.h"

#include "fhost_wpa.h"
#include "aic_log.h"

#ifndef CONFIG_NO_WPA_MSG
static void wpa_supplicant_ctrl_iface_msg_cb(void *ctx, int level,
					     enum wpa_msg_type type,
					     const char *txt, size_t len)
{
	if (level >= wpa_debug_level)
		AIC_LOG_PRINTF("[WPA] %s\n", txt);
}
#endif
struct wpa_supplicant *g_wpa_s = NULL;

void *wpa_supplicant_main(void *env)
{
	struct wpa_interface iface;
	int exitcode = 0;
	struct wpa_params params;
	struct wpa_global *global;
	struct fhost_wpa_config *config = (struct fhost_wpa_config *)env;
	struct wpa_supplicant *wpa_s = NULL;

	memset(&params, 0, sizeof(params));
	params.wpa_debug_level = fhost_wpa_debug_level;

	global = wpa_supplicant_init(&params);
	if (global == NULL) {
        aic_dbg("(global == NULL)\r\n");
		goto end;
	}

	os_memset(&iface, 0, sizeof(iface));
	iface.ctrl_interface = config->ctrl_itf;
	iface.ifname = config->iface_name;

	wpa_s = wpa_supplicant_add_iface(global, &iface, NULL);

	if (wpa_s == NULL) {
		exitcode = -1;
	} else {
		int port = 0;
		wpa_msg_register_cb(wpa_supplicant_ctrl_iface_msg_cb);
		if (!os_strncmp(wpa_s->conf->ctrl_interface,"udp:",4)) {
			port = atoi(&wpa_s->conf->ctrl_interface[4]);
		}
		if (port || !config->ctrl_itf) {
			fhost_wpa_send_event(FHOST_WPA_STARTED, (void *)port, 0, config->fhost_vif_idx);
		} else {
			exitcode = -1;
		}
        g_wpa_s = wpa_s;
	}

	if (exitcode == 0) {
		exitcode = wpa_supplicant_run(global);
	}

	wpa_supplicant_deinit(global);

end:
    AIC_LOG_PRINTF("wpa_supplicant_main end\r\n");
	fhost_wpa_send_event(FHOST_WPA_EXIT, (void *)exitcode, 0, config->fhost_vif_idx);
    fhost_wpa_kill();
}

void fhost_wpa_kill (void)
{
    printf("%s in\n", __func__);
    if (fhost_wpa_task) {
        rtos_task_delete(fhost_wpa_task);
        fhost_wpa_task = NULL;
    }
}


int fhost_wpa_start(int fhost_vif_idx, char *config)
{
    int ret = 0;
    struct fhost_wpa_target_event target;
    struct fhost_wpa_tag *wpa_conf = fhost_wpa_get_conf(fhost_vif_idx);
	AIC_LOG_PRINTF("fhost_wpa_start\n");

    if (!wpa_conf || (wpa_conf->state != FHOST_WPA_STATE_STOPPED) ||
        (fhost_vif_name(fhost_vif_idx, wpa_conf->config.iface_name,
                        sizeof(wpa_conf->config.iface_name)) < 0))
        return -1;

    wpa_conf->config.fhost_vif_idx = fhost_vif_idx;
    wpa_conf->config.iface_conf_buffer = config;
    wpa_conf->config.ctrl_itf = "UDP";

    // Register cb before starting the task to avoid race condition
    if (fhost_wpa_wait_event_register(fhost_vif_idx, FHOST_WPA_STARTED, &target))
        return -1;

    if (rtos_task_create((rtos_task_fct)wpa_supplicant_main, "fhost_wpa_task", SUPPLICANT_TASK,
                         2*fhost_wpa_stack_size, &wpa_conf->config, fhost_wpa_priority,
                         &wpa_conf->task))
    {
        rtos_semaphore_delete(target.sem);
        target.sem = NULL;
        fhost_wpa_wait_event_unregister(fhost_vif_idx);
        return -1;
    }

    fhost_wpa_task = wpa_conf->task;
    ret =  rtos_semaphore_wait(target.sem, 3000);
    rtos_semaphore_delete(target.sem);
    target.sem = NULL;
    AIC_LOG_PRINTF("target.sem %x\r\n", target.sem);
    if (ret)
    {
        AIC_LOG_PRINTF("Err: Start of WPA task timeout\n");
        fhost_wpa_wait_event_unregister(fhost_vif_idx);
        fhost_wpa_end(fhost_vif_idx);
        return -1;
    }
    else if (wpa_conf->task == RTOS_TASK_NULL)
    {
        // wpa_supplicant initialization failed
        AIC_LOG_PRINTF("Err: Start of WPA task failed\n");
        return -1;
    }

    // This need to be updated to support multiple interface
    wpa_conf->rx_filter = 0xFFFFFFF;
    fhost_rx_set_mgmt_cb(fhost_wpa_rx_cb, wpa_conf);
    AIC_LOG_PRINTF("WPA task started for interface {FVIF-%d}\n", fhost_vif_idx);
    return 0;
}

int fhost_wpa_end(int fhost_vif_idx)
{
    struct fhost_wpa_target_event target;
    struct fhost_wpa_tag *wpa_conf = fhost_wpa_get_conf(fhost_vif_idx);

    //fhost_cntrl_mm_set_filter(0X35038188); // Not rx beacons

    if (!wpa_conf)
        return -1;

    if (wpa_conf->state == FHOST_WPA_STATE_STOPPED)
        return 0;

    if (rtos_semaphore_create(&fhost_wpa_end_sema, "fhost_wpa_end_sema", 1, 0))
        return -1;

    if (rtos_semaphore_create(&fhost_wpa_exit_sema, "fhost_wpa_exit_sema", 1, 0))
        return -1;

    if (fhost_wpa_wait_event_register(fhost_vif_idx, FHOST_WPA_EXIT, &target))
        return -1;



    // If WPA task is of higher priority than the calling task, the command will be processed
    // by WPA before we get the response. It means that FHOST_WPA_EXIT event will be handled
    // which will close the ctrl socket while the response is still in the socket.
    // fhost_wpa_execute_cmd will then return an error, that's why in this case we test the state.
    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, -1, "TERMINATE") &&
        (wpa_conf->state != FHOST_WPA_STATE_STOPPED))
    {
        ASSERT_ERR(0);
    }

    rtos_semaphore_signal(fhost_wpa_end_sema, 0);
    rtos_semaphore_wait(fhost_wpa_exit_sema, -1);
    rtos_semaphore_delete(fhost_wpa_exit_sema);
    rtos_semaphore_delete(target.sem);
    target.sem = NULL;

    return 0;
}

int fhost_set_filter_ssids(int fhost_vif_idx)
{
    fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, -1, "SET filter_ssids 1");

    return 0;
}

int fhost_set_max_sta_num(int fhost_vif_idx, uint8_t sta_max)
{
    fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, -1, "SET max_num_sta %d", sta_max);

    return 0;
}

enum fhost_wpa_state fhost_wpa_get_state(int fhost_vif_idx)
{
    struct fhost_wpa_tag *wpa_conf = fhost_wpa_get_conf(fhost_vif_idx);

    if (!wpa_conf)
        return FHOST_WPA_STATE_STOPPED;

    return wpa_conf->state;
}

int fhost_wpa_cb_register(int fhost_vif_idx, int events, fhost_wpa_cb_t cb_func, void *cb_arg)
{
    struct fhost_wpa_event_cb *cb;
    int i;

    if (fhost_vif_idx >= NX_VIRT_DEV_MAX)
        return -1;

    cb = fhost_wpa[fhost_vif_idx].cb;

    for (i = 0; i < FHOST_WPA_EVENT_CB_CNT; i++, cb++)
    {
        if (cb->events == 0)
        {
            cb->events = events;
            cb->func = cb_func;
            cb->arg = cb_arg;
            return 0;
        }
    }

    return -1;
}

int fhost_wpa_cb_unregister(int fhost_vif_idx, fhost_wpa_cb_t cb_func)
{
    struct fhost_wpa_event_cb *cb;
    int i;

    if (fhost_vif_idx >= NX_VIRT_DEV_MAX)
        return -1;

    cb = fhost_wpa[fhost_vif_idx].cb;

    for (i = 0; i < FHOST_WPA_EVENT_CB_CNT; i++, cb++)
    {
        if (cb->func == cb_func)
        {
            cb->events = 0;
            cb->func = NULL;
            cb->arg = NULL;
            return 0;
        }
    }

    return -1;
}

int fhost_wpa_send_event(enum fhost_wpa_event event, void *param, int param_len, int fhost_vif_idx)
{
    if ((fhost_vif_idx >= NX_VIRT_DEV_MAX) || (event >= FHOST_WPA_LAST))
        return -1;

    fhost_wpa_event_process(event, param, param_len, fhost_vif_idx);
    return 0;
}

int fhost_wpa_execute_cmd(int fhost_vif_idx, char *resp_buf, int *resp_buf_len,
                          int timeout_ms, const char *fmt, ...)
{
    struct fhost_wpa_tag *wpa_conf = fhost_wpa_get_conf(fhost_vif_idx);
    va_list args;
    char cmd[128];
    unsigned int cmd_len = 0;
    int res = 0;
    int res2 = 0;

    if (!wpa_conf || (wpa_conf->ctrl_sock < 0)) {
        if (!wpa_conf) {
            AIC_LOG_PRINTF("Err %s, wpa_conf:0x%X\n", __func__, wpa_conf);
        } else {
            AIC_LOG_PRINTF("Err %s, wpa_conf:0x%X, ctrl_sock:%d\n", __func__, wpa_conf, wpa_conf->ctrl_sock);
        }
        res = -1;
        goto exit;
    }

    // Format command
    va_start(args, fmt);
    cmd_len = dbg_vsnprintf(cmd, sizeof(cmd), fmt, args);
    va_end(args);

    if (cmd_len == 0) {
        cmd[0] = '\0';
    }
    AIC_LOG_PRINTF("%s cmd:%s\n", __func__, cmd);

    if (cmd_len >= sizeof(cmd))
    {
        AIC_LOG_PRINTF("WPA Command truncated. need %d bytes\n", cmd_len);
        res = -2;
        goto exit;
    }

    // Send it
    res2 = fhost_wpa_send_cmd(wpa_conf, cmd);
    if (res2 < 0) {
        res = -3;
        goto exit;
    }

    // Retrieve respond
    res2 = fhost_wpa_get_resp(wpa_conf, resp_buf, resp_buf_len, timeout_ms);
    if (res2) {
        res = -4;
        goto exit;
    }

exit:
    if (res) {
        if (cmd_len == 0) {
            cmd[0] = '\0';
        }
        if (cmd_len >= sizeof(cmd)) {
            uint32_t cmd_max_len = sizeof(cmd);
            cmd[cmd_max_len - 1] = '\0';
        }
        AIC_LOG_PRINTF("Err %s, cmd:%s, res:%d, res2:%d\n", __func__, cmd, res, res2);
    }

    return res;
}

static char *_strtok_r(char *s, const char *delim, char **save_ptr) {
    char *token;
    if (s == NULL) s = *save_ptr;

    /* Scan leading delimiters.  */
    s += strspn(s, delim);
    if (*s == '\0')
        return NULL;

    /* Find the end of the token.  */
    token = s;
    s = strpbrk(token, delim);
    if (s == NULL)
        /* This token finishes the string.  */
        *save_ptr = strchr(token, '\0');
    else {
        /* Terminate the token and make *SAVE_PTR point past it.  */
        *s = '\0';
        *save_ptr = s + 1;
    }

    return token;
}

int fhost_wpa_create_network(int fhost_vif_idx, char *net_cfg, bool enable, int timeout_ms)
{
    struct fhost_wpa_tag *wpa_conf = fhost_wpa_get_conf(fhost_vif_idx);
    char res[5], *tok, *buf;
    int res_len;

	AIC_LOG_PRINTF("fhost_wpa_create_network\n");

    if (!net_cfg || !wpa_conf || fhost_wpa_start(fhost_vif_idx, NULL))
        return -1;

    // Create and configure network block
    res_len = sizeof(res) - 1;
    if (fhost_wpa_execute_cmd(fhost_vif_idx, res, &res_len, 500, "ADD_NETWORK")) {
        return -1;
    }
    res[res_len] = '\0';
    wpa_conf->network_id = atoi(res);


    tok = _strtok_r(net_cfg, ";", &buf);
    while (tok)
    {
            res_len = sizeof(res);
            if (fhost_wpa_execute_cmd(fhost_vif_idx, res, &res_len, 1800, "SET_NETWORK %d %s",
                                    wpa_conf->network_id, tok))
            {
                aic_dbg("SET_NETWORK (%s) failed\r\n", tok);
                fhost_wpa_end(fhost_vif_idx);
                return -1;
            }
        tok = _strtok_r(NULL, ";", &buf);
    }
    fhost_set_filter_ssids(fhost_vif_idx);

    AIC_LOG_PRINTF("WPA network %d: created and configured\n", wpa_conf->network_id);

    // Connect to AP if requested
    if (enable && fhost_wpa_enable_network(fhost_vif_idx, timeout_ms))
    {
        fhost_wpa_end(fhost_vif_idx);
        return -1;
    }

    return 0;
}
int fhost_wpa_wps(int fhost_vif_idx, bool enable, int timeout_ms)
{
    struct fhost_wpa_target_event target;
    struct fhost_wpa_tag *wpa_conf = fhost_wpa_get_conf(fhost_vif_idx);
    char res[5];
    int res_len;

    if (wpa_conf->state == FHOST_WPA_STATE_CONNECTED)
        return 0;

    if (!wpa_conf || fhost_wpa_start(fhost_vif_idx, NULL))
        return -1;

    if (timeout_ms &&
        fhost_wpa_wait_event_register(fhost_vif_idx, FHOST_WPA_CONNECTED, &target))
        return -1;

    // Create and configure network block
    res_len = sizeof(res) - 1;
    if (fhost_wpa_execute_cmd(fhost_vif_idx, res, &res_len, 800, "WPS_PBC")) {
        fhost_wpa_wait_event_unregister(fhost_vif_idx);
        return -1;
    }

    if (!timeout_ms)
        return 0;

    // Wait until connection is completed.
    if (rtos_semaphore_wait(target.sem, timeout_ms))
    {
        aic_dbg("WPA network %d: connection timeout\r\n", wpa_conf->network_id);
        fhost_wpa_wait_event_unregister(fhost_vif_idx);
        return -1;
    }
    else if (wpa_conf->state == FHOST_WPA_STATE_STOPPED)
    {
        aic_dbg("WPA Task exit during connection\n");
        return -1;
    }

    return 0;
}

extern bool time_out_flag;

int fhost_wpa_enable_network(int fhost_vif_idx, int timeout_ms)
{
    struct fhost_wpa_target_event target;
    struct fhost_wpa_tag *wpa_conf = fhost_wpa_get_conf(fhost_vif_idx);

    if (!wpa_conf || (wpa_conf->network_id < 0))
        return -1;

    if (wpa_conf->state == FHOST_WPA_STATE_CONNECTED)
        return 0;

    if (timeout_ms &&
        fhost_wpa_wait_event_register(fhost_vif_idx, FHOST_WPA_CONNECTED, &target))
        return -1;

    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 5000, "ENABLE_NETWORK %d ",
                              wpa_conf->network_id))
    {
        if (timeout_ms) {
            rtos_semaphore_delete(target.sem);
            target.sem = NULL;
        }
        fhost_wpa_wait_event_unregister(fhost_vif_idx);
        return -1;
    }

    if (!timeout_ms)
        return 0;

    // Wait until connection is completed.
    rtos_semaphore_wait(target.sem, timeout_ms);
    rtos_semaphore_delete(target.sem);
    target.sem = NULL;
    if (wpa_conf->state == FHOST_WPA_STATE_STOPPED)
    {
        aic_dbg("WPA Task exit during connection\n");
		time_out_flag = true;
        return -1;
    }
    else if (wpa_conf->state == FHOST_WPA_STATE_CONNECTED)
    {
        aic_dbg("WPA network %d: connected\n", wpa_conf->network_id);
    }
    else
    {
        aic_dbg("WPA network %d: connection timeout\r\n", wpa_conf->network_id);
        fhost_wpa_wait_event_unregister(fhost_vif_idx);
		time_out_flag = true;
        return -1;
    }

    return 0;
}

int fhost_wpa_disable_network(int fhost_vif_idx)
{
    struct fhost_wpa_target_event target;
    struct fhost_wpa_tag *wpa_conf = fhost_wpa_get_conf(fhost_vif_idx);

    if (!wpa_conf || (wpa_conf->network_id < 0))
        return -1;

    if (wpa_conf->state != FHOST_WPA_STATE_CONNECTED)
        return 0;

    if (fhost_wpa_wait_event_register(fhost_vif_idx, FHOST_WPA_DISCONNECTED, &target))
        return -1;

    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 100, "DISABLE_NETWORK %d ",
                              wpa_conf->network_id))
    {
        rtos_semaphore_delete(target.sem);
        target.sem = NULL;
        fhost_wpa_wait_event_unregister(fhost_vif_idx);
        return -1;
    }

    int ret = rtos_semaphore_wait(target.sem, 1000);
    rtos_semaphore_delete(target.sem);
    target.sem = NULL;
    if (ret)
    {
        aic_dbg("WPA network %d: disconnection timeout\n", wpa_conf->network_id);
        fhost_wpa_wait_event_unregister(fhost_vif_idx);
        return -1;
    }
    else if (wpa_conf->state == FHOST_WPA_STATE_STOPPED)
    {
        aic_dbg("WPA Task exit during disconnection\n");
        return -1;
    }

    aic_dbg("WPA network %d: disconnected\n", wpa_conf->network_id);
    return 0;
}

int fhost_wpa_set_mgmt_rx_filter(int fhost_vif_idx, uint32_t filter)
{
    struct fhost_wpa_tag *wpa_conf = fhost_wpa_get_conf(fhost_vif_idx);
    if (!wpa_conf)
        return -1;

    wpa_conf->rx_filter = filter;
    return 0;
}

int fhost_wpa_disconnect_network(int fhost_vif_idx)
{
    struct fhost_wpa_target_event target;
    struct fhost_wpa_tag *wpa_conf = fhost_wpa_get_conf(fhost_vif_idx);

    if (!wpa_conf || (wpa_conf->network_id < 0))
        return -1;

    if (wpa_conf->state != FHOST_WPA_STATE_CONNECTED)
        return 0;

    if (fhost_wpa_wait_event_register(fhost_vif_idx, FHOST_WPA_DISCONNECTED, &target))
        return -1;

    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 100, "DISCONNECT"))
    {
        rtos_semaphore_delete(target.sem);
        target.sem = NULL;
        fhost_wpa_wait_event_unregister(fhost_vif_idx);
        return -1;
    }

    int ret = rtos_semaphore_wait(target.sem, 1000);
    rtos_semaphore_delete(target.sem);
    target.sem = NULL;
    if (ret)
    {
        aic_dbg("WPA network %d: disconnection timeout\n", wpa_conf->network_id);
        fhost_wpa_wait_event_unregister(fhost_vif_idx);
        return -1;
    }
    else if (wpa_conf->state == FHOST_WPA_STATE_STOPPED)
    {
        aic_dbg("WPA Task exit during disconnection\n");
        return -1;
    }

    aic_dbg("WPA network %d: disconnected\n", wpa_conf->network_id);
    return 0;
}

int fhost_wpa_reconnect_network(int fhost_vif_idx, int timeout_ms)
{
    struct fhost_wpa_target_event target;
    struct fhost_wpa_tag *wpa_conf = fhost_wpa_get_conf(fhost_vif_idx);

    if (!wpa_conf || (wpa_conf->network_id < 0))
        return -1;

    if (wpa_conf->state == FHOST_WPA_STATE_CONNECTED)
        return 0;

    if (timeout_ms &&
        fhost_wpa_wait_event_register(fhost_vif_idx, FHOST_WPA_CONNECTED, &target))
        return -1;

    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 100, "RECONNECT"))
    {
        if (timeout_ms) {
            rtos_semaphore_delete(target.sem);
            target.sem = NULL;
        }
        fhost_wpa_wait_event_unregister(fhost_vif_idx);
        return -1;
    }

    if (!timeout_ms)
        return 0;

    // Wait until connection is completed.
    int ret = rtos_semaphore_wait(target.sem, timeout_ms);
    rtos_semaphore_delete(target.sem);
    target.sem = NULL;
    if (ret)
    {
        aic_dbg("WPA network %d: connection timeout\r\n", wpa_conf->network_id);
        fhost_wpa_wait_event_unregister(fhost_vif_idx);
        return -1;
    }
    else if (wpa_conf->state == FHOST_WPA_STATE_STOPPED)
    {
        aic_dbg("WPA Task exit during connection\n");
        return -1;
    }

    aic_dbg("WPA network %d: connected\n", wpa_conf->network_id);
    return 0;
}

int fhost_wpa_stop_ap(int fhost_vif_idx)
{
    struct fhost_wpa_target_event target;
    struct fhost_wpa_tag *wpa_conf = fhost_wpa_get_conf(fhost_vif_idx);

    if (!wpa_conf || (wpa_conf->network_id < 0))
        return -1;

    if (wpa_conf->state != FHOST_WPA_STATE_CONNECTED)
        return 0;

    if (fhost_wpa_wait_event_register(fhost_vif_idx, FHOST_WPA_DISCONNECTED, &target))
        return -1;

    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 100, "DISASSOCIATE FF:FF:FF:FF:FF:FF reason=1"))
    {
        rtos_semaphore_delete(target.sem);
        target.sem = NULL;
        fhost_wpa_wait_event_unregister(fhost_vif_idx);
        return -1;
    }
    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 100, "STOP_AP"))
    {
        rtos_semaphore_delete(target.sem);
        target.sem = NULL;
        fhost_wpa_wait_event_unregister(fhost_vif_idx);
        return -1;
    }

    int ret = rtos_semaphore_wait(target.sem, 1000);
    rtos_semaphore_delete(target.sem);
    target.sem = NULL;
    if (ret)
    {
        aic_dbg("WPA network %d: stop AP timeout\n", wpa_conf->network_id);
        fhost_wpa_wait_event_unregister(fhost_vif_idx);
        return -1;
    }
    else if (wpa_conf->state == FHOST_WPA_STATE_STOPPED)
    {
        aic_dbg("WPA Task exit during stop AP\n");
        return -1;
    }

    aic_dbg("WPA Stop AP %d\n", wpa_conf->network_id);
    return 0;
}

int fhost_wpa_stop_p2p(int fhost_vif_idx)
{
    // struct fhost_wpa_target_event target;
    struct fhost_wpa_tag *wpa_conf = fhost_wpa_get_conf(fhost_vif_idx);

    if (!wpa_conf || (wpa_conf->network_id < 0))
        return -1;

    if (wpa_conf->state != FHOST_WPA_STATE_CONNECTED)
        return 0;

    // if (fhost_wpa_wait_event_register(fhost_vif_idx, FHOST_WPA_DISCONNECTED, &target))
    //     return -1;

    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 100, "P2P_REMOVE_CLIENT"))
    {
        fhost_wpa_wait_event_unregister(fhost_vif_idx);
        return -1;
    }
    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 100, "P2P_FLUSH"))
    {
        fhost_wpa_wait_event_unregister(fhost_vif_idx);
        return -1;
    }

    // int ret = rtos_semaphore_wait(target.sem, 1000);
    // rtos_semaphore_delete(target.sem);
    // if (ret)
    // {
    //     aic_dbg("WPA network %d: stop P2P timeout\n", wpa_conf->network_id);
    //     fhost_wpa_wait_event_unregister(fhost_vif_idx);
    //     return -1;
    // }
    // else if (wpa_conf->state == FHOST_WPA_STATE_STOPPED)
    // {
    //     aic_dbg("WPA Task exit during stop P2P\n");
    //     return -1;
    // }

    aic_dbg("WPA Stop P2P %d\n", wpa_conf->network_id);
    return 0;
}

int fhost_wpa_disassociate_sta(int fhost_vif_idx, struct mac_addr *macaddr)
{
    struct wpa_rwnx_driver_msg *event;

    event = rtos_malloc(sizeof(struct wpa_rwnx_driver_msg));
    event->id = CFGRWNX_DISASSOC_EVENT;
    memcpy(event->msg.disassoc_event.addr, macaddr->array, 6);

    if (wpa_rwnx_driver_send(&event)) {
        aic_dbg("%s fail\r\n", __func__);
        rtos_free(event);
        event = NULL;
    }

    return 0;
}

int fhost_wpa_switch_channel(int fhost_vif_idx, uint32_t frequency)
{
    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 500, "CHAN_SWITCH 6 %d center_freq1=%d ht=1 vht=1", frequency, frequency))
    {
        aic_dbg("Switch channel fail\n");
        return -1;
    }
    aic_dbg("Switch to Channel(%d)\n", frequency);

    return 0;
}

int fhost_ap_add_blacklist(int fhost_vif_idx, struct mac_addr *macaddr)
{
    uint8_t *mac_addr = (uint8_t *)&macaddr->array;
    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 500, "DENY_ACL ADD_MAC %02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0], mac_addr[1],mac_addr[2],mac_addr[3],mac_addr[4],mac_addr[5]))
    {
        aic_dbg("Add blacklist fail\n");
        return -1;
    }
    aic_dbg("Add %x:%x:%x:%x:%x:%x into blacklist Done\n", mac_addr[0], mac_addr[1],mac_addr[2],mac_addr[3],mac_addr[4],mac_addr[5]);

    return 0;
}
int fhost_ap_delete_blacklist(int fhost_vif_idx, struct mac_addr *macaddr)
{
    uint8_t *mac_addr = (uint8_t *)&macaddr->array;
    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 500, "DENY_ACL DEL_MAC %02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0], mac_addr[1],mac_addr[2],mac_addr[3],mac_addr[4],mac_addr[5]))
    {
        aic_dbg("Delete blacklist fail\n");
        return -1;
    }
    aic_dbg("Remove %x:%x:%x:%x:%x:%x from blacklist Done\n", mac_addr[0], mac_addr[1],mac_addr[2],mac_addr[3],mac_addr[4],mac_addr[5]);

    return 0;
}

int fhost_ap_clear_blacklist(int fhost_vif_idx)
{
    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 500, "DENY_ACL CLEAR"))
    {
        aic_dbg("Clear blacklist fail\n");
        return -1;
    }
    aic_dbg("Clear blacklist Done\n");

    return 0;
}

int fhost_ap_show_blacklist(int fhost_vif_idx, uint8_t *list, uint16_t *list_len)
{
    if (fhost_wpa_execute_cmd(fhost_vif_idx, list, list_len, 500, "DENY_ACL SHOW"))
    {
        aic_dbg("Show blacklist fail\n");
        return -1;
    }
    aic_dbg("Show blacklist Done\n");

    return 0;
}

int fhost_ap_add_whitelist(int fhost_vif_idx, struct mac_addr *macaddr)
{
    uint8_t *mac_addr = (uint8_t *)&macaddr->array;
    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 500, "ACCEPT_ACL ADD_MAC %02x:%02x:%02x:%02x:%02x:%02x",
mac_addr[0], mac_addr[1],mac_addr[2],mac_addr[3],mac_addr[4],mac_addr[5]))
    {
        aic_dbg("Add whitelist fail\n");
        return -1;
    }
    aic_dbg("Add %x:%x:%x:%x:%x:%x into whitelist Done\n", mac_addr[0], mac_addr[1],mac_addr[2],mac_addr[3],mac_addr[4
],mac_addr[5]);

    return 0;
}

int fhost_ap_delete_whitelist(int fhost_vif_idx, struct mac_addr *macaddr)
{
    uint8_t *mac_addr = (uint8_t *)&macaddr->array;
    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 500, "ACCEPT_ACL DEL_MAC %02x:%02x:%02x:%02x:%02x:%02x",
mac_addr[0], mac_addr[1],mac_addr[2],mac_addr[3],mac_addr[4],mac_addr[5]))
    {
        aic_dbg("Delete whitelist fail\n");
        return -1;
    }
    aic_dbg("Remove %x:%x:%x:%x:%x:%x from whitelist Done\n", mac_addr[0], mac_addr[1],mac_addr[2],mac_addr[3],
mac_addr[4],mac_addr[5]);

    return 0;
}

int fhost_ap_clear_whitelist(int fhost_vif_idx)
{
    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 500, "ACCEPT_ACL CLEAR"))
    {
        aic_dbg("Clear whitelist fail\n");
        return -1;
    }
    aic_dbg("Clear whitelist Done\n");

    return 0;
}

int fhost_ap_show_whitelist(int fhost_vif_idx, uint8_t *list, uint16_t *list_len)
{
    if (fhost_wpa_execute_cmd(fhost_vif_idx, list, list_len, 500, "ACCEPT_ACL SHOW"))
    {
        aic_dbg("Show whitelist fail\n");
        return -1;
    }
    aic_dbg("Show whitelist Done\n");

    return 0;
}

int fhost_ap_macaddr_acl(int fhost_vif_idx, uint8_t acl)
{
    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 500, "MACADDR_ACL %d", acl))
    {
        aic_dbg("Set macaddr_acl fail\n");
        return -1;
    }
    aic_dbg("Set macaddr_acl Done\n");

    return 0;
}

int fhost_wpa_sta_not_autoconnect(int fhost_vif_idx)
{
    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 500, "STA_AUTOCONNECT 0"))
    {
        return -1;
    }
    return 0;
}

int fhost_wpa_ap_hiden(int fhost_vif_idx, uint8_t hidden_ssid)
{
    char res[5];
    int res_len;

    struct fhost_wpa_tag *wpa_conf = fhost_wpa_get_conf(fhost_vif_idx);

    if (fhost_wpa_execute_cmd(fhost_vif_idx, res, &res_len, 800, "SET ignore_broadcast_ssid %d",
                                hidden_ssid))
    {
        aic_dbg("SET_NETWORK (ignore_broadcast_ssid) failed\r\n");
        return -1;
    }
    if (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 500, "AP_RELOAD"))
    {
        return -1;
    }
    return 0;
}

int fhost_wpa_p2p_set_pref_chan(uint8_t *chan_list)
{
    char *cmd_str = rtos_malloc(sizeof("SET p2p_pref_chan ") + strlen(chan_list));

    memset(cmd_str, 0, (sizeof("SET p2p_pref_chan ") + strlen(chan_list)));
    memcpy(cmd_str, "SET p2p_pref_chan ", sizeof("SET p2p_pref_chan "));
    strcat(cmd_str, chan_list);
    fhost_wpa_execute_cmd(0, NULL, NULL, 300, cmd_str);
    rtos_free(cmd_str);

    return 0;
}
void fhost_wpa_rx_eapol(uint8_t *src, uint16_t len, uint8_t *buf)
{
    struct wpa_rwnx_driver_msg *event;

    event = rtos_malloc(sizeof(struct wpa_rwnx_driver_msg));

    event->id = CFGRWNX_RX_EAPOL_EVENT;
    event->msg.rx_eapol_event.length = len;
    event->msg.rx_eapol_event.payload = rtos_malloc(len);
    if (event->msg.rx_eapol_event.payload == NULL)
        return;

    memcpy(event->msg.rx_eapol_event.payload, buf, len);
    memcpy(event->msg.rx_eapol_event.addr, src, 6);
    if (wpa_rwnx_driver_send(&event)) {
        aic_dbg("%s fail\r\n", __func__);
        rtos_free(event->msg.rx_eapol_event.payload);
        event->msg.rx_eapol_event.payload = NULL;

        rtos_free(event);
        event = NULL;
    }
}
/**
 * @}
 */
