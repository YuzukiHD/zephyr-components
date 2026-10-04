/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
 *
 * All Rights Reserved
 */

#include "wifi.h"
#include "porting.h"
#include "aic_log.h"
#include "rwnx_defs.h"
#include "rtos_al.h"
#include "rwnx_main.h"
#include "fhost_config.h"
#include "fhost_api.h"
#include "fhost_wpa.h"
#include "sdio_port.h"
#include "fhost.h"
#include "cli_cmd.h"
#include "lmac_mac.h"
#include "wlan_if.h"
#include "sunxi_hal_efuse.h"
#include "lwip/dhcp.h"

#define CONFIG_TEST_MAIN_EN 0
#define CONFIG_RTOS_AL_TEST_EN    0

#define AIC_WIFI_EVENT_ENABLE


extern const char *aic_version;
extern const char *aic_date;
extern const char *rlsversion;
extern const char aic_wifi_version[];


aic_wifi_event_cb g_aic_wifi_event_cb = NULL;
wifi_drv_event_cbk aw_aic_wifi_event_cb = NULL;
int dev_mode = WIFI_MODE_UNKNOWN;
int g_wifi_init = 0;

#if (CONFIG_RTOS_AL_TEST_EN)
rtos_semaphore sema = NULL;

rtos_task_handle test_task_handle = NULL;
#define TEST_TASK_ID 10
#define TEST_TASK_STACK_DEPTH 512
#define TEST_TASK_PRIO 1
rtos_semaphore task_sema = NULL;

#define TEST_QUEUE_ELT_CNT 5
struct test_queue_msg {
    uint32_t id;
    uint32_t param;
};
rtos_queue test_queue = NULL;

void my_timer_func(void *param)
{
    AIC_LOG_PRINTF("my_timer_func, now: %d, param: %x\n", rtos_now(false), (uint32_t)param);
    rtos_semaphore_signal(sema, 0);
}

void my_task_func(void *param)
{
    int ret = 0;
    AIC_LOG_PRINTF("%s enter, param: %x\n", __func__, (uint32_t)param);

    while (1) {
        ret = rtos_semaphore_wait(task_sema, 3000);
        if ((ret == 0))
            AIC_LOG_PRINTF("semaphore success\n");
        if ((ret == 1))
            AIC_LOG_PRINTF("semaphore timeout\n");
        else
            AIC_LOG_PRINTF("semaphore error\n");

        break;
    }
}

void rtos_al_test(void)
{
    AIC_LOG_PRINTF("rtos_al_test start\n");

    int ret = 0;
    unsigned int i = 0;

    // 1.rtos_now/rtos_msleep
    AIC_LOG_PRINTF("now: %d\n", rtos_now(false));
    rtos_msleep(20);
    AIC_LOG_PRINTF("now: %d\n", rtos_now(false));

    // 2.rtos_malloc/rtos_free/rtos_memcpy/rtos_memset
    char *ptr = NULL;
    ptr = (char *)rtos_malloc(16);
    if (ptr == NULL)
        AIC_LOG_PRINTF("rtos_malloc failed\n");
    else
        AIC_LOG_PRINTF("rtos_malloc successfully, addr: 0x%lx\n", (unsigned long)ptr);

    rtos_memset(ptr, 0, 16);
    //rwnx_data_dump("rtos_memset", ptr, 16);
    AIC_LOG_PRINTF("rtos_memset:\n");
    for (i = 0; i < 16; i++) {
        aic_dbg("%02x ", ptr[i]);
    }
    aic_dbg("\n");

    char a[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    rtos_memcpy(ptr, a, 16);
    //rwnx_data_dump("rtos_memset", ptr, 16);
    AIC_LOG_PRINTF("rtos_memcpy:\n");
    for (i = 0; i < 16; i++) {
        aic_dbg("%02x ", ptr[i]);
    }
    aic_dbg("\n");

    rtos_free(ptr);

    // 3.rtos_entercritical/rtos_exitcritical

    // 4.task
    ret = rtos_semaphore_create(&task_sema, "task_sema", 1, 0);
    if (ret != 0)
        AIC_LOG_PRINTF("rtos_semaphore_create failed\n");
    else
        AIC_LOG_PRINTF("rtos_semaphore_create successfully\n");

    ret = rtos_mutex_create(&task_mutex);
    if (ret != 0)
        AIC_LOG_PRINTF("rtos_mutex_create failed\n");
    else
        AIC_LOG_PRINTF("rtos_mutex_create successfully\n");

    AIC_LOG_PRINTF("test_task_handle ptr: %x\n", (uint32_t)&test_task_handle);
    ret = rtos_task_create(my_task_func, "Test", TEST_TASK_ID, TEST_TASK_STACK_DEPTH, &test_task_handle, TEST_TASK_PRIO, &test_task_handle);
    if (ret != 0)
        AIC_LOG_PRINTF("rtos_task_create failed\n");
    else
        AIC_LOG_PRINTF("rtos_task_create successfully\n");
    AIC_LOG_PRINTF("test_task_priority:%x\n" ,rtos_task_get_priority(test_task_handle));


    AIC_LOG_PRINTF("%s task_mutex lock start@%u\n", __func__, rtos_now(false));
    ret = rtos_mutex_lock(task_mutex, -1);
    AIC_LOG_PRINTF("%s task_mutex lock end@%u, ret=%d\n", __func__, rtos_now(false), ret);

#if 1
    AIC_LOG_PRINTF("test_task2_handle ptr: %x\n", (uint32_t)&test_task2_handle);
    ret = rtos_task_create(my_task2_func, "Test2", TEST_TASK2_ID, TEST_TASK2_STACK_DEPTH, &test_task2_handle, TEST_TASK2_PRIO, &test_task2_handle);
    if (ret != 0)
        AIC_LOG_PRINTF("rtos_task_create failed\n");
    else
        AIC_LOG_PRINTF("rtos_task_create successfully\n");
    AIC_LOG_PRINTF("test_task2_priority:%x\n" ,rtos_task_get_priority(test_task2_handle));
#endif

    rtos_msleep(10);
    rtos_semaphore_signal(task_sema, 0);
    rtos_msleep(150);

    AIC_LOG_PRINTF("%s task_mutex unlock@%u\n", __func__, rtos_now(false));
    rtos_mutex_unlock(task_mutex);

    AIC_LOG_PRINTF("test_task delete start\n");
    rtos_task_delete(test_task_handle);
    AIC_LOG_PRINTF("test_task delete end\n");
#if 1
    AIC_LOG_PRINTF("test_task2 delete start\n");
    rtos_task_delete(test_task2_handle);
    AIC_LOG_PRINTF("test_task2 delete end\n");
#endif
    rtos_semaphore_delete(task_sema);
    rtos_mutex_delete(task_mutex);

    // 5.queue
    struct test_queue_msg msg;
    ret = rtos_queue_create(sizeof(struct test_queue_msg), TEST_QUEUE_ELT_CNT, &test_queue, "test_queue");
    if (ret != 0)
        AIC_LOG_PRINTF("rtos_queue_create failed\n");
    else
        AIC_LOG_PRINTF("rtos_queue_create successfully\n");
    AIC_LOG_PRINTF("test_queue:%X\n", (uint32_t)test_queue);
    AIC_LOG_PRINTF("rtos_queue cnt:%d full:%d empty:%d\n", rtos_queue_cnt(test_queue), rtos_queue_is_full(test_queue), rtos_queue_is_empty(test_queue));
    AIC_LOG_PRINTF("rtos_queue_read empty queue start@:%d\n", rtos_now(false));
    ret = rtos_queue_read(test_queue, &msg, 10, 0);
    AIC_LOG_PRINTF("rtos_queue_read empty queue end @%d ret:%d\n", rtos_now(false), ret);

    msg.id = 1;
    msg.param = 1;
    ret = rtos_queue_write(test_queue, &msg, 0, 0);
    AIC_LOG_PRINTF("rtos_queue_write ret:%d id:%d param:%d\n", ret, msg.id, msg.param);
    AIC_LOG_PRINTF("rtos_queue cnt:%d full:%d empty:%d\n", rtos_queue_cnt(test_queue), rtos_queue_is_full(test_queue), rtos_queue_is_empty(test_queue));

    msg.id = 2;
    msg.param = 2;
    ret = rtos_queue_write(test_queue, &msg, 0, 0);
    AIC_LOG_PRINTF("rtos_queue_write ret:%d id:%d param:%d\n", ret, msg.id, msg.param);
    AIC_LOG_PRINTF("rtos_queue cnt:%d full:%d empty:%d\n", rtos_queue_cnt(test_queue), rtos_queue_is_full(test_queue), rtos_queue_is_empty(test_queue));

    memset(&msg, 0, sizeof(struct test_queue_msg));
    ret = rtos_queue_read(test_queue, &msg, 10, 0);
    AIC_LOG_PRINTF("rtos_queue_read ret:%d id:%d param:%d\n", ret, msg.id, msg.param);
    AIC_LOG_PRINTF("rtos_queue cnt:%d full:%d empty:%d\n", rtos_queue_cnt(test_queue), rtos_queue_is_full(test_queue), rtos_queue_is_empty(test_queue));

    memset(&msg, 0, sizeof(struct test_queue_msg));
    ret = rtos_queue_read(test_queue, &msg, 10, 0);
    AIC_LOG_PRINTF("rtos_queue_read ret:%d id:%d param:%d\n", ret, msg.id, msg.param);
    AIC_LOG_PRINTF("rtos_queue cnt:%d full:%d empty:%d\n", rtos_queue_cnt(test_queue), rtos_queue_is_full(test_queue), rtos_queue_is_empty(test_queue));

    rtos_queue_delete(test_queue);

    // 6.semaphore
    ret = rtos_semaphore_create(&sema, "sema", 1, 0);
    if (ret != 0)
        AIC_LOG_PRINTF("rtos_semaphore_create failed\n");
    else
        AIC_LOG_PRINTF("rtos_semaphore_create successfully\n");

    // 7.timer
    rtos_timer test_timer;
    AIC_LOG_PRINTF("test_timer ptr:%x\n", (uint32_t)&test_timer);
    ret = rtos_timer_create("test_timer", &test_timer, 10, 0, &test_timer, my_timer_func);
    if (ret != 0)
        AIC_LOG_PRINTF("rtos_timer_create failed\n");
    else
        AIC_LOG_PRINTF("rtos_timer_create successfully\n");

    AIC_LOG_PRINTF("test_timer start@%d\n", rtos_now(false));
    rtos_timer_start(test_timer, 0, 0);

    ret = rtos_semaphore_wait(sema, 30);
    if (ret == 0)
        AIC_LOG_PRINTF("get semaphore singal successfully\n");
    else if (ret == 1)
        AIC_LOG_PRINTF("get semaphore singal timeout\n");
    else
        AIC_LOG_PRINTF("get semaphore singal failed\n");

    ret = rtos_timer_delete(test_timer, 0);
    if ((ret != 0))
        AIC_LOG_PRINTF("rtos_timer_delete failed\n");
    else
        AIC_LOG_PRINTF("rtos_timer_delete successfully\n");

    ret = rtos_timer_create("test_timer", &test_timer, 10, 1, &test_timer, my_timer_func);
    if (ret != 0)
        AIC_LOG_PRINTF("rtos_timer_create failed\n");
    else
        AIC_LOG_PRINTF("rtos_timer_create successfully\n");

    AIC_LOG_PRINTF("test_timer start@%d\n", rtos_now(false));
    rtos_timer_start(test_timer, 20, 0);

    rtos_msleep(100);

    ret = rtos_timer_delete(test_timer, 0);
    if ((ret != 0))
        AIC_LOG_PRINTF("rtos_timer_delete failed\n");
    else
        AIC_LOG_PRINTF("rtos_timer_delete successfully\n");

    uint32_t idx = 0;
    for (idx = 0; idx < 20; idx++) {
        AIC_LOG_PRINTF("timer_sema count:%u\n", rtos_semaphore_get_count(timer_sema));
        ret = rtos_semaphore_wait(timer_sema, 10);
        AIC_LOG_PRINTF("timer_sema wait, idx:%d, ret:%d\n", idx, ret);
    }
    rtos_semaphore_delete(timer_sema);

    uint32_t sec = 0, usec = 0;
    aic_time_get(0, &sec, &usec);
    AIC_LOG_PRINTF("now:%u sec:%u usec:%u\n", rtos_now(false), sec, usec);

    AIC_LOG_PRINTF("sleep 10\n");
    rtos_msleep(10);

    aic_time_get(0, &sec, &usec);
    AIC_LOG_PRINTF("now:%u sec:%u usec:%u\n", rtos_now(false), sec, usec);

    AIC_LOG_PRINTF("rtos_al_test end\n");
}
#endif

void temp_isr(void)
{
    //AIC_LOG_PRINTF("temp_isr\r\n");
}


struct rwnx_hw *g_rwnx_hw = NULL;

void aicwf_get_chipid(void)
{
    struct rwnx_hw *rwnx_hw = g_rwnx_hw;
#if defined(CONFIG_AIC8801)
    rwnx_hw->chipid = PRODUCT_ID_AIC8801;
    AIC_LOG_PRINTF("aicwf chipid: USE AIC8801\r\n");
#elif defined(CONFIG_AIC8800DC)
    rwnx_hw->chipid = PRODUCT_ID_AIC8800DC;
    AIC_LOG_PRINTF("aicwf chipid: USE AIC8800DC\r\n");
#elif defined(CONFIG_AIC8800DW)
    rwnx_hw->chipid = PRODUCT_ID_AIC8800DW;
    AIC_LOG_PRINTF("aicwf chipid: USE AIC8800DW\r\n");
#elif defined(CONFIG_AIC8800D80)
    rwnx_hw->chipid = PRODUCT_ID_AIC8800D80;
    AIC_LOG_PRINTF("aicwf chipid: USE AIC8800D80\r\n");
#else
    AIC_LOG_PRINTF("aicwf chipid: no aic product\r\n");
#endif
}

unsigned int aicwf_is_5g_enable(void)
{
#ifdef USE_5G
    return 1;
#else
    return 0;
#endif
}

static void aic_gen_mac_by_chipid(unsigned char mac_addr[6],unsigned char *chipid)
{
       int i;
       //unsigned char chipid[16] = {0};
      // hal_efuse_read("chipid", chipid, 128);

       for (i = 0; i < 2; ++i) {
               mac_addr[i] = chipid[i] ^ chipid[i + 6] ^ chipid[i + 12];
       }
       for (i = 2; i < 6; ++i) {
               mac_addr[i] = chipid[i] ^ chipid[i + 6] ^ chipid[i + 10];
       }
       mac_addr[0] &= 0xFC;
}

/**
 * @brief initializing wifi
 * @author
 * @date
 * @param [in] mode  wifi mode
 * @param [in] param a pointer to ap/sta cfg
 * @return int
 * @retval   0  initializing sucessful
 * @retval  -1 initializing fail
 */
static int aic_wifi_open(int mode, void *param, u16 chip_id)
{
    struct rwnx_hw *rwnx_hw = NULL;
    static uint8_t g_wifi_opened = 0;
    int ret = 0;
    unsigned char mac_addr[6] = {0x88, 0x00, 0x33, 0x77, 0x69, 0x22};
    //AIC_LOG_PRINTF("Wifilib version:%s\r\n", aic_wifi_get_version());
    AIC_LOG_PRINTF("aic_wifi_open: %d\n", mode);

    if (g_wifi_opened == 0)
    {
        // pwrkey en
        platform_pwr_en_pin_init();
        // platform_pwr_en_pin_set(0);
        // rtos_task_suspend(10);
        // platform_pwr_en_pin_set(1);
        // alloc structs
        rwnx_hw = rtos_malloc(sizeof(struct rwnx_hw));
        if (rwnx_hw == NULL) {
            AIC_LOG_PRINTF("rwnx_hw alloc failed\r\n");
            return -1;
        }
        memset(rwnx_hw, 0, sizeof(struct rwnx_hw));
        g_rwnx_hw = rwnx_hw;
        rwnx_hw->mode = mode;

        #ifdef CONFIG_SDIO_SUPPORT
        aicwf_get_chipid();
        if (!sdio_host_init(rwnx_hw, temp_isr)) {
            AIC_LOG_PRINTF("aic_wifi_open: no SDIO card\r\n");
            rtos_free(rwnx_hw);
            g_rwnx_hw = NULL;
            return -1;
        }
        #endif
        #ifdef CONFIG_USB_SUPPORT
        rwnx_hw->chipid = chip_id;
        aic_usb_host_init(rwnx_hw);
        #endif
        fhost_init(rwnx_hw);
        rwnx_cmd_mgr_init(&rwnx_hw->cmd_mgr);
        ret = rwnx_fdrv_init(rwnx_hw);
        if(ret) return ret;
        #ifdef CONFIG_TEMP_COMP
        struct mm_set_vendor_swconfig_cfm swconfig_cfm;
        rwnx_send_set_temp_comp_req(rwnx_hw, &swconfig_cfm);
        #endif /* CONFIG_TEMP_COMP */
        aic_cli_cmd_init(rwnx_hw);

        g_wifi_opened = 1;
		sdio_dev.ap_sleep = false;
    } else {
        rwnx_hw = g_rwnx_hw;
    }
    if (sdio_dev.ap_sleep == true) {
        aicwf_sdio_bus_sleep_exit(&sdio_dev);
        sdio_dev.ap_sleep = false;
    }

    uint8_t *m_addr = get_mac_address();
    if (1 == CMP_MAC(fhost_default_mac_addr, m_addr)) {
        AIC_LOG_PRINTF("use default mac addr, read efuse\r\n");
    unsigned char buffer[32] = {0};
    unsigned char zero_chipid[16] = {0};
    hal_efuse_get_chipid(buffer);
    if (0 == memcmp(buffer, zero_chipid,16)) {
       AIC_LOG_PRINTF("no chipid efuse, use random chipid\n");
        for (uint8_t i = 0; i < 16 ; i++) {
            buffer[i] = co_rand_byte();
        }
    }
    if (buffer[10] != 0) {
        //memcpy(mac_addr, &buffer[10], 6);
        aic_gen_mac_by_chipid(mac_addr, buffer);
    }
        set_mac_address(mac_addr);
    }
    AIC_LOG_PRINTF("fhost_mac_addr %02x:%02x:%02x:%02x:%02x:%02x\r\n", m_addr[0], m_addr[1], m_addr[2], m_addr[3], m_addr[4], m_addr[5]);

    //rwnx_set_coex_config_req(g_rwnx_hw, 0, 0, 1, 0, 0x0, 0x0);
    if (mode == WIFI_MODE_AP) {
#if 0
        #define AP_SSID_STRING  "AIC-AP-F133"
        #define AP_PASS_STRING  "kkkkkkkk"
        struct aic_ap_cfg user_ap_cfg = {
            .aic_ap_ssid = {
                strlen(AP_SSID_STRING),
                AP_SSID_STRING
            },
            .aic_ap_passwd = {
                strlen(AP_PASS_STRING),
                AP_PASS_STRING
            },
            .band = 1,
            .type = PHY_CHNL_BW_20,
            .channel = 149,
            .hidden_ssid = 0,
            .max_inactivity = 60,
            .enable_he = 1,
            .enable_acs = 0,
            .bcn_interval = 100,
            .sta_num = 10,
        };
        #undef AP_SSID_STRING
        #undef AP_PASS_STRING
#endif
        rwnx_hw->net_id = wlan_start_ap((struct aic_ap_cfg *)param);
    } else if (mode == WIFI_MODE_STA) {
        rwnx_hw->net_id = wlan_start_sta((uint8_t *)"Empty_SSID", (uint8_t *)"Empty_Password", -1);
    } else if (mode == WIFI_MODE_P2P) {
        //aic_wifi_set_mode(WIFI_MODE_P2P);
    }

    g_wifi_init = 1;
    AIC_LOG_PRINTF("wifi open ok\r\n");
    return 0;
}

static int aic_wifi_close(int mode)
{
    int ret = 0;
    struct rwnx_hw *rwnx_hw = g_rwnx_hw;

    AIC_LOG_PRINTF("aic_wifi_deinit_mode: %d\n", mode);

    if (g_wifi_init == 0)
    {
        AIC_LOG_PRINTF("aic_wifi_deinit already deinit\r\n");
        return 0;
    }

    if (mode == WIFI_MODE_AP) {
        ret = wlan_stop_ap();
        if (ret) {
            AIC_LOG_PRINTF("wlan_stop_ap failed: %d\n", ret);
            return -1;
        } else {
            AIC_LOG_PRINTF("wlan_stop_ap success: %d\n", ret);
        }
        rwnx_hw->net_id = 0;
    }

    aicwf_sdio_bus_sleep_enter(&sdio_dev);
    sdio_dev.ap_sleep = true;
#if 0
    aic_cli_cmd_deinit(rwnx_hw);
    rwnx_fdrv_deinit(rwnx_hw);
    rwnx_cmd_mgr_deinit(&rwnx_hw->cmd_mgr);
    fhost_deinit(rwnx_hw);
    sdio_host_deinit();

    rtos_free(rwnx_hw);
#endif

    AIC_LOG_PRINTF("aic_wifi_close success\r\n");
    return ret;
}

AIC_WIFI_MODE aic_wifi_get_mode(void)
{
    return dev_mode;
}

void aic_wifi_set_mode(AIC_WIFI_MODE mode)
{
    dev_mode = mode;
}

void aic_wifi_event_register(aic_wifi_event_cb cb)
{
    g_aic_wifi_event_cb = (aic_wifi_event_cb)cb;
}

int wifi_drv_event_set_cbk(wifi_drv_event_cbk cbk)
{
    aw_aic_wifi_event_cb = cbk;
    AIC_LOG_PRINTF("%s is called, aw_aic_wifi_event_cb: %p\r\n", __func__, aw_aic_wifi_event_cb);

    return 1;
}

void wifi_drv_event_reset_cbk(void) {
    aw_aic_wifi_event_cb = NULL;

    aic_wifi_deinit(WIFI_MODE_AP);
}


static unsigned int aic_p2p_dev_port = 7236; //default port number
static unsigned char aic_p2p_associating = 0;

static wifi_event_handle g_wifi_event_handler[AIC_WIFI_EVENT_MAX] = {NULL};
void aic_wifi_event_handler_register(AIC_WIFI_EVENT enEvent, wifi_event_handle cb)
{
    if (enEvent < AIC_WIFI_EVENT_MAX) {
        g_wifi_event_handler[enEvent] = cb;
    }
}
void aic_wifi_event_callback(AIC_WIFI_EVENT enEvent, aic_wifi_event_data *enData)
{
    switch(enEvent)
    {
        case SCAN_RESULT_EVENT:
            {
                //MLOGE("func:%s, SCAN_RESULT_EVENT received\n",__FUNCTION__);
                if (g_wifi_event_handler[SCAN_RESULT_EVENT])
                {
                    g_wifi_event_handler[SCAN_RESULT_EVENT](enData);
                }
                break;
            }
        case SCAN_DONE_EVENT:
            {
                if (g_wifi_event_handler[SCAN_DONE_EVENT])
                {
                    g_wifi_event_handler[SCAN_DONE_EVENT](enData);
                }
                #if 0
                if (1)
                {
                    wlan_event_msg_t event;
                    //make a fake wlan event
                    event.event_type = WLAN_E_SCAN_COMPLETE;
                    WLAN_SYS_StatusCallback(&event);
                }
                #endif
                break;
            }
        case JOIN_SUCCESS_EVENT:
            {
                if (g_wifi_event_handler[JOIN_SUCCESS_EVENT])
                {
                    g_wifi_event_handler[JOIN_SUCCESS_EVENT](enData);
                }
                #if 0
                if (1)
                {
                    wlan_event_msg_t event;
                    //make a fake wlan event
                    event.event_type = WLAN_E_LINK;
                    event.flags = 1;
                    WLAN_SYS_StatusCallback(&event);
                }
                #endif
                break;
            }
        case STA_ASSOC_EVENT:
            {
                uint32_t state = enData->data.join_data.reserved[0];
                aic_dbg("STA_ASSOC_EVENT state = %d\r\n", state);
                if (g_wifi_event_handler[STA_ASSOC_EVENT])
                {
                    g_wifi_event_handler[STA_ASSOC_EVENT](enData);
                }
                break;
            }
        case JOIN_FAIL_EVENT:
            {
                if (g_wifi_event_handler[JOIN_FAIL_EVENT])
                {
                    g_wifi_event_handler[JOIN_FAIL_EVENT](enData);
                }
                #if 0
                if (1)
                {
                    struct resp_evt_result *join_res = (struct resp_evt_result *)enData;
                    wlan_event_msg_t event;
                    event.event_type = WLAN_E_LINK;
                    event.flags = 0;
                    event.reason = join_res->u.join.status_code;
                    WLAN_SYS_StatusCallback(&event);
                }
                #endif
                break;
            }
        case LEAVE_RESULT_EVENT:
            {
                if (g_wifi_event_handler[LEAVE_RESULT_EVENT])
                {
                    g_wifi_event_handler[LEAVE_RESULT_EVENT](enData);
                }
                #if 0
                if (1)
                {
                    struct resp_evt_result *leave_res = (struct resp_evt_result *)enData;
                    wlan_event_msg_t event;
                    event.event_type = WLAN_E_LINK;
                    event.flags = 0;
                    event.reason = leave_res->u.leave.reason_code;
                    WLAN_SYS_StatusCallback(&event);
                }

                #endif
                break;
            }
        case PRO_DISC_REQ_EVENT:
            {
                struct wifi_p2p_event p2p_event;
            	if (aic_p2p_associating) {
					break;
				}
				aic_p2p_associating = 1;
                uint32_t *mac_addr = enData->data.auth_deauth_data.reserved;
                aic_dbg("PRO_DISC_REQ_EVENT mac_addr = %02x:%02x:%02x:%02x:%02x:%02x [%d]\r\n"
                       , mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5], aic_p2p_associating);
                if (g_wifi_event_handler[PRO_DISC_REQ_EVENT])
                {
                    g_wifi_event_handler[PRO_DISC_REQ_EVENT](enData);
                }
                if (aw_aic_wifi_event_cb) {
                    wifi_drv_event drv_event;
                    p2p_event.event_type = WIFI_P2P_EVENT_GOT_PRO_DISC_REQ_AFTER_GONEGO_OK;
                    p2p_event.peer_dev_mac_addr[0] = (unsigned char)enData->data.auth_deauth_data.reserved[0];
                    p2p_event.peer_dev_mac_addr[1] = (unsigned char)enData->data.auth_deauth_data.reserved[1];
                    p2p_event.peer_dev_mac_addr[2] = (unsigned char)enData->data.auth_deauth_data.reserved[2];
                    p2p_event.peer_dev_mac_addr[3] = (unsigned char)enData->data.auth_deauth_data.reserved[3];
                    p2p_event.peer_dev_mac_addr[4] = (unsigned char)enData->data.auth_deauth_data.reserved[4];
                    p2p_event.peer_dev_mac_addr[5] = (unsigned char)enData->data.auth_deauth_data.reserved[5];
                    drv_event.type = WIFI_DRV_EVENT_P2P;
                    drv_event.node.p2p_event = p2p_event;

                    aic_p2p_dev_port = enData->p2p_dev_port_num;
                    aw_aic_wifi_event_cb(&drv_event);
                }
                // user_wps_button_pushed();
                extern int rwnx_p2p_disc_req(uint8_t *mac_addr);
                rwnx_p2p_disc_req(p2p_event.peer_dev_mac_addr);
                break;
            }
        case EAPOL_STA_FIN_EVENT:
            {
                uint32_t *mac_addr = enData->data.auth_deauth_data.reserved;
                aic_dbg("EAPOL_STA_FIN_EVENT mac_addr = %02x:%02x:%02x:%02x:%02x:%02x\r\n"
                       , mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
                if (g_wifi_event_handler[EAPOL_STA_FIN_EVENT])
                {
                    g_wifi_event_handler[EAPOL_STA_FIN_EVENT](enData);
                }
                if (aw_aic_wifi_event_cb) {
                    wifi_drv_event drv_event;
                    struct wifi_ap_event ap_event;
                    ap_event.event_type = WIFI_AP_EVENT_ON_ASSOC;
                    drv_event.type = WIFI_DRV_EVENT_AP;
                    ap_event.peer_dev_mac_addr[0] = (unsigned char)enData->data.auth_deauth_data.reserved[0];
                    ap_event.peer_dev_mac_addr[1] = (unsigned char)enData->data.auth_deauth_data.reserved[1];
                    ap_event.peer_dev_mac_addr[2] = (unsigned char)enData->data.auth_deauth_data.reserved[2];
                    ap_event.peer_dev_mac_addr[3] = (unsigned char)enData->data.auth_deauth_data.reserved[3];
                    ap_event.peer_dev_mac_addr[4] = (unsigned char)enData->data.auth_deauth_data.reserved[4];
                    ap_event.peer_dev_mac_addr[5] = (unsigned char)enData->data.auth_deauth_data.reserved[5];
                    drv_event.node.ap_event = ap_event;

                    //diag_dump_buf(ap_event.peer_dev_mac_addr, 6);
                    aw_aic_wifi_event_cb(&drv_event);
                }
                break;
            }
        case EAPOL_P2P_FIN_EVENT:
            {
                uint32_t *mac_addr = enData->data.auth_deauth_data.reserved;
                aic_dbg("EAPOL_P2P_FIN_EVENT mac_addr = %02x:%02x:%02x:%02x:%02x:%02x\r\n"
                       , mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
                if (g_wifi_event_handler[EAPOL_P2P_FIN_EVENT])
                {
                    g_wifi_event_handler[EAPOL_P2P_FIN_EVENT](enData);
                }
                if (aw_aic_wifi_event_cb) {
                    wifi_drv_event drv_event;
                    struct wifi_p2p_event p2p_event;
                    p2p_event.event_type = WIFI_P2P_EVENT_ON_ASSOC_REQ;
                    p2p_event.peer_dev_port = aic_p2p_dev_port;
                    p2p_event.peer_dev_mac_addr[0] = (unsigned char)enData->data.auth_deauth_data.reserved[0];
                    p2p_event.peer_dev_mac_addr[1] = (unsigned char)enData->data.auth_deauth_data.reserved[1];
                    p2p_event.peer_dev_mac_addr[2] = (unsigned char)enData->data.auth_deauth_data.reserved[2];
                    p2p_event.peer_dev_mac_addr[3] = (unsigned char)enData->data.auth_deauth_data.reserved[3];
                    p2p_event.peer_dev_mac_addr[4] = (unsigned char)enData->data.auth_deauth_data.reserved[4];
                    p2p_event.peer_dev_mac_addr[5] = (unsigned char)enData->data.auth_deauth_data.reserved[5];
                    drv_event.type = WIFI_DRV_EVENT_P2P;
                    drv_event.node.p2p_event = p2p_event;

                    aw_aic_wifi_event_cb(&drv_event);
                }
                break;
            }
        case ASSOC_IND_EVENT:
            {
                uint32_t *mac_addr = enData->data.auth_deauth_data.reserved;
                aic_dbg("ASSOC_IND_EVENT mac_addr = %02x:%02x:%02x:%02x:%02x:%02x\r\n"
                       , mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
                if (g_wifi_event_handler[ASSOC_IND_EVENT])
                {
                    g_wifi_event_handler[ASSOC_IND_EVENT](enData);
                }
                #if 0
                if (1)
                {
                    wlan_event_msg_t event;
                    //make a fake wlan event
                    event.event_type = WLAN_E_ASSOC_IND;
                    event.addr.mac[0] = enData->data.auth_deauth_data.reserved[0];
                    event.addr.mac[1] = enData->data.auth_deauth_data.reserved[1];
                    event.addr.mac[2] = enData->data.auth_deauth_data.reserved[2];
                    event.addr.mac[3] = enData->data.auth_deauth_data.reserved[3];
                    event.addr.mac[4] = enData->data.auth_deauth_data.reserved[4];
                    event.addr.mac[5] = enData->data.auth_deauth_data.reserved[5];
                    WLAN_SYS_StatusCallback(&event);
                }
                #endif
				aic_p2p_associating = 0;
                break;
            }
        case STA_DISCONNECT_EVENT:
            {
                AIC_WIFI_MODE mode = aic_wifi_get_mode();
                aic_dbg("STA_DISCONNECT_EVENT, current mode:%d\r\n", mode);
                net_if_t *net_if = NULL;
                net_if = net_if_find_from_wifi_idx(0);
                if (net_if) {
                    net_dhcp_stop(net_if);
                    
                    net_if_down(net_if);
                }
                if (g_wifi_event_handler[STA_DISCONNECT_EVENT])
                {
                    g_wifi_event_handler[STA_DISCONNECT_EVENT](enData);
                }
                if (aw_aic_wifi_event_cb &&  mode == WIFI_MODE_STA) {
                    wifi_drv_event drv_event;
                    struct wifi_sta_event dev_event;
                    dev_event.event_type = WIFI_STA_EVENT_ON_DISASSOC;
                    drv_event.type = WIFI_DRV_EVENT_STA;
                    drv_event.node.sta_event = dev_event;
                    aw_aic_wifi_event_cb(&drv_event);
                }
                break;
            }
        case DISASSOC_STA_IND_EVENT:
            {
                uint32_t *mac_addr = enData->data.auth_deauth_data.reserved;
                aic_dbg("DISASSOC_STA_IND_EVENT mac_addr = %02x:%02x:%02x:%02x:%02x:%02x\r\n"
                       , mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
                if (g_wifi_event_handler[DISASSOC_STA_IND_EVENT])
                {
                    g_wifi_event_handler[DISASSOC_STA_IND_EVENT](enData);
                }
                #if 0
                if (1)
                {
                    wlan_event_msg_t event;
                    //make a fake wlan event
                    event.event_type = WLAN_E_DISASSOC_IND;
                    event.addr.mac[0] = enData->data.auth_deauth_data.reserved[0];
                    event.addr.mac[1] = enData->data.auth_deauth_data.reserved[1];
                    event.addr.mac[2] = enData->data.auth_deauth_data.reserved[2];
                    event.addr.mac[3] = enData->data.auth_deauth_data.reserved[3];
                    event.addr.mac[4] = enData->data.auth_deauth_data.reserved[4];
                    event.addr.mac[5] = enData->data.auth_deauth_data.reserved[5];
                    WLAN_SYS_StatusCallback(&event);
                }
                #endif
                if(aw_aic_wifi_event_cb) {
                    wifi_drv_event drv_event;
                    struct wifi_ap_event ap_event;
                    ap_event.event_type = WIFI_AP_EVENT_ON_DISASSOC;
                    ap_event.peer_dev_mac_addr[0] = (unsigned char)enData->data.auth_deauth_data.reserved[0];
                    ap_event.peer_dev_mac_addr[1] = (unsigned char)enData->data.auth_deauth_data.reserved[1];
                    ap_event.peer_dev_mac_addr[2] = (unsigned char)enData->data.auth_deauth_data.reserved[2];
                    ap_event.peer_dev_mac_addr[3] = (unsigned char)enData->data.auth_deauth_data.reserved[3];
                    ap_event.peer_dev_mac_addr[4] = (unsigned char)enData->data.auth_deauth_data.reserved[4];
                    ap_event.peer_dev_mac_addr[5] = (unsigned char)enData->data.auth_deauth_data.reserved[5];
                    drv_event.type = WIFI_DRV_EVENT_AP;
                    drv_event.node.ap_event = ap_event;

                    aw_aic_wifi_event_cb(&drv_event);
                }
                break;
            }
        case DISASSOC_P2P_IND_EVENT:
            {
                uint32_t *mac_addr = enData->data.auth_deauth_data.reserved;
                aic_dbg("DISASSOC_P2P_IND_EVENT mac_addr = %02x:%02x:%02x:%02x:%02x:%02x\r\n"
                       , mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
                if (g_wifi_event_handler[DISASSOC_P2P_IND_EVENT])
                {
                    g_wifi_event_handler[DISASSOC_P2P_IND_EVENT](enData);
                }
                #if 0
                if (1)
                {
                    wlan_event_msg_t event;
                    //make a fake wlan event
                    event.event_type = WLAN_E_DISASSOC_IND;
                    event.addr.mac[0] = enData->data.auth_deauth_data.reserved[0];
                    event.addr.mac[1] = enData->data.auth_deauth_data.reserved[1];
                    event.addr.mac[2] = enData->data.auth_deauth_data.reserved[2];
                    event.addr.mac[3] = enData->data.auth_deauth_data.reserved[3];
                    event.addr.mac[4] = enData->data.auth_deauth_data.reserved[4];
                    event.addr.mac[5] = enData->data.auth_deauth_data.reserved[5];
                    WLAN_SYS_StatusCallback(&event);
                }
                #endif
                if(aw_aic_wifi_event_cb) {
                    wifi_drv_event drv_event;
                    struct wifi_p2p_event p2p_event;
                    p2p_event.event_type = WIFI_P2P_EVENT_ON_DISASSOC;
                    p2p_event.peer_dev_mac_addr[0] = (unsigned char)enData->data.auth_deauth_data.reserved[0];
                    p2p_event.peer_dev_mac_addr[1] = (unsigned char)enData->data.auth_deauth_data.reserved[1];
                    p2p_event.peer_dev_mac_addr[2] = (unsigned char)enData->data.auth_deauth_data.reserved[2];
                    p2p_event.peer_dev_mac_addr[3] = (unsigned char)enData->data.auth_deauth_data.reserved[3];
                    p2p_event.peer_dev_mac_addr[4] = (unsigned char)enData->data.auth_deauth_data.reserved[4];
                    p2p_event.peer_dev_mac_addr[5] = (unsigned char)enData->data.auth_deauth_data.reserved[5];
                    drv_event.type = WIFI_DRV_EVENT_P2P;
                    drv_event.node.p2p_event = p2p_event;

                    aw_aic_wifi_event_cb(&drv_event);
                }
                break;
            }
        case AP_STARTED_EVENT:
            {
                uint32_t state = enData->data.ap_go_status_data.reserved[0];
                if (g_wifi_event_handler[AP_STARTED_EVENT])
                {
                    g_wifi_event_handler[AP_STARTED_EVENT](enData);
                }
                aic_dbg("AP_STARTED_EVENT state = %d\r\n", state);
                break;
            }
        case AP_STOPED_EVENT:
            {
                uint32_t state = enData->data.ap_go_status_data.reserved[0];
                if (g_wifi_event_handler[AP_STOPED_EVENT])
                {
                    g_wifi_event_handler[AP_STOPED_EVENT](enData);
                }
                aic_dbg("AP_STOPED_EVENT state = %d\r\n", state);
                break;
            }
        case GO_STARTED_EVENT:
            {
                uint32_t state = enData->data.ap_go_status_data.reserved[0];
                if (g_wifi_event_handler[GO_STARTED_EVENT])
                {
                    g_wifi_event_handler[GO_STARTED_EVENT](enData);
                }
                aic_dbg("GO_STARTED_EVENT state = %d\r\n", state);
                break;
            }
        case GO_REMOVED_EVENT:
            {
                uint32_t state = enData->data.ap_go_status_data.reserved[0];
                if (g_wifi_event_handler[GO_REMOVED_EVENT])
                {
                    g_wifi_event_handler[GO_REMOVED_EVENT](enData);
                }
                aic_dbg("GO_REMOVED_EVENT state = %d\r\n", state);
                break;
            }
        default:
            {
                break;
            }
    }
}

int aic_wifi_init_mac(void)
{
    int ret = 0;

    unsigned char mac_addr[6] = {0x88, 0x00, 0x33, 0x77, 0x69, 0x22};
    unsigned int mac_local[6] = {0};

    /* use chip info as MAC in now(By Sunplus),
     * TODO: the MAC value read from efuse will be modified later(By AIC company).
     */
    extern int dovBTMAC(int* mac);
    ret = dovBTMAC((int *)mac_local);
    if(ret) {
        mac_local[0] = (mac_local[0] > 0xfd) ?
                        (mac_local[0] - 1) :
                        (mac_local[0] + 1);

        mac_addr[0] = mac_local[5] & 0xff;
        mac_addr[1] = mac_local[4] & 0xff;
        mac_addr[2] = mac_local[3] & 0xff;
        mac_addr[3] = mac_local[2] & 0xff;
        mac_addr[4] = mac_local[1] & 0xff;
        mac_addr[5] = mac_local[0] & 0xff;
    }

    set_mac_address(mac_addr);
       return 0;
}

int wifi_scan_event_handler(void *enData)
{
    printf("scan done\r\n");
    return 0;
}
int aic_wifi_init(int mode, int chip_id, void *param)
{
    int ret = 0;
    unsigned char mac_addr[6] = {0};
    //unsigned int mac_local[6] = {0};

    AIC_LOG_PRINTF("aic_wifi_init, mode=%d\r\n", mode);
    AIC_LOG_PRINTF("release version:%s\r\n", aic_wifi_version);

    #if (CONFIG_RTOS_AL_TEST_EN)
    rtos_al_test();
    #endif

    /*
    if (g_wifi_init == 1)
    {
        AIC_LOG_PRINTF("aic_wifi_init already init\r\n");
        return g_rwnx_hw->net_id;
    } */

    aic_wifi_event_register(aic_wifi_event_callback);
    ret = aic_wifi_open(mode, param, chip_id);
    if (ret) {
        AIC_LOG_PRINTF("wifi_open fail, ret=%d\n", ret);
        return -1;
    }

    aic_wifi_event_handler_register(SCAN_DONE_EVENT, wifi_scan_event_handler);
    #if (CONFIG_TEST_MAIN_EN)
    test_main_entry();
    #endif
    AIC_LOG_PRINTF("aic_wifi_init ok\r\n");
retry:
    if (aw_aic_wifi_event_cb) {
        wifi_drv_event drv_event;
        struct wifi_dev_event dev_event;
        dev_event.drv_status = WIFI_DEVICE_DRIVER_LOADED;
        memcpy(mac_addr, get_mac_address(), 6);
        dev_event.local_mac_addr[0] = mac_addr[0];
        dev_event.local_mac_addr[1] = mac_addr[1];
        dev_event.local_mac_addr[2] = mac_addr[2];
        dev_event.local_mac_addr[3] = mac_addr[3];
        dev_event.local_mac_addr[4] = mac_addr[4];
        dev_event.local_mac_addr[5] = mac_addr[5];
        drv_event.type = WIFI_DRV_EVENT_NET_DEVICE;
        drv_event.node.dev_event = dev_event;
        aw_aic_wifi_event_cb(&drv_event);
    } else {
        rtos_msleep(5);
        goto retry;
    }

    return g_rwnx_hw->net_id;
}

void aic_wifi_deinit(int mode)
{
    AIC_LOG_PRINTF("aic_wifi_deinit, mode=%d\r\n", mode);

    if (g_wifi_init == 1)
    {
       aic_wifi_close(mode);

       aic_wifi_event_register(NULL);
    }
    if(aw_aic_wifi_event_cb) {
        wifi_drv_event drv_event;
        struct wifi_dev_event dev_event;
        dev_event.drv_status = WIFI_DEVICE_DRIVER_UNLOAD;
        drv_event.type = WIFI_DRV_EVENT_NET_DEVICE;
        drv_event.node.dev_event = dev_event;
        aw_aic_wifi_event_cb(&drv_event);
    }
    g_wifi_init = 0;

    AIC_LOG_PRINTF("aic_wifi_deinit ok\r\n");
}
extern void sys_aic_reboot(struct rwnx_hw *rwnx_hw);
extern void sys_aic_wdt(struct rwnx_hw *rwnx_hw, uint8_t cmd, uint32_t seconds);
void aic_wifi_reboot(void)
{
    AIC_LOG_PRINTF("aic_wifi_reboot\r\n");
    sys_aic_reboot(g_rwnx_hw);
}

void aic_wifi_wdt(uint8_t cmd, uint32_t seconds)
{
    AIC_LOG_PRINTF("aic_wifi_wdt,  %d %d\r\n", cmd,  seconds);
    sys_aic_wdt(g_rwnx_hw, cmd,  seconds);
}

extern uint8_t p2p_started;

#ifdef CONFIG_VENDOR_IE
char* custom_vendor_ie = DEFAULT_VENDOR_IE;

void aic_add_custom_ie (char* vendor_ie) {
    custom_vendor_ie = vendor_ie;
}

void aic_update_custom_ie (char* vendor_ie) {
    custom_vendor_ie = vendor_ie;
}

void aic_del_custom_ie (void) {
    custom_vendor_ie = NULL;
}
#endif

int user_p2p_setDN(const char* device_name)
{
    int fhost_vif_idx = 0;
    char set_p2p_dev_name_cmd[64];
    char set_ap_dev_name_cmd[64];
    strcpy(set_p2p_dev_name_cmd, "SET p2p_dev_name ");
    strcpy(set_ap_dev_name_cmd, "P2P_SET ssid_postfix DIRECT-MD");
    strcat(set_p2p_dev_name_cmd, device_name);
    strcat(set_ap_dev_name_cmd, device_name);
    fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 300, set_p2p_dev_name_cmd);
    fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 300, set_ap_dev_name_cmd);
    return 0;
}

int user_set_wfd_type(int wfd_device_type)
{
    int fhost_vif_idx = 0;
    if (wfd_device_type)
        fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 300, "WFD_SUBELEM_SET 0 000600111C440032");
    else
        fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 300, "WFD_SUBELEM_SET 0 000600101C440032");
    return 0;
}

int user_set_wfd_enable(int wfd_device_enable)
{
    int fhost_vif_idx = 0;
    if (wfd_device_enable)
        fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 300, "SET wifi_display 1");
    else
        fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 300, "SET wifi_display 0");
    return 0;
}

int user_wps_button_pushed(void)
{
    int fhost_vif_idx = 0;
    printf("%s in\n", __func__);
    fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 800, "WPS_PBC");
    return 0;
}

int user_p2p_start(struct aic_p2p_cfg *user_p2p_cfg)
{
    printf("p2p state:%d\n", p2p_started);
    if (p2p_started) {
        wlan_stop_p2p();
        aic_p2p_associating = 0;
        aic_wifi_event_register(NULL);
        return 0;
     } else {
        aic_wifi_event_register(aic_wifi_event_callback);
        g_rwnx_hw->net_id = wlan_start_p2p(user_p2p_cfg);
        aic_wifi_set_mode(WIFI_MODE_P2P);
    }

    user_p2p_setDN((const char *)user_p2p_cfg->aic_p2p_ssid.array);

    char set_p2p_go_cmd[128];
    char freq[5];
    uint16 prim20_freq;
    int fhost_vif_idx = 0;

    memset(set_p2p_go_cmd, 0, sizeof(set_p2p_go_cmd));
    strcpy(set_p2p_go_cmd, "P2P_GROUP_ADD ");
    if (user_p2p_cfg->enable_he)
        strcat(set_p2p_go_cmd, "he ");
    if (user_p2p_cfg->type)
        strcat(set_p2p_go_cmd, "ht40 vht ");
    strcat(set_p2p_go_cmd, "pass=");
    strcat(set_p2p_go_cmd, (const char *)user_p2p_cfg->aic_ap_passwd.array);
    strcat(set_p2p_go_cmd, " freq=");

    if (user_p2p_cfg->enable_acs) {
        if (user_p2p_cfg->band == 0) {
            prim20_freq = 2;
        } else {
            prim20_freq = 5;
        }
    } else {
        if ((user_p2p_cfg->band == 0) && (user_p2p_cfg->channel == 0)) {
            prim20_freq = phy_channel_to_freq(PHY_BAND_2G4, 11);
            set_ap_channel_num(11);
        } else {
            prim20_freq = phy_channel_to_freq(user_p2p_cfg->band, user_p2p_cfg->channel);
            set_ap_channel_num(user_p2p_cfg->channel);
        }
    }

    memset(freq, 0, sizeof(freq));
    extern unsigned int g_aic_pref_chan_cnt;
    if (g_aic_pref_chan_cnt) {
        sprintf(freq, "%s", "acs");
    } else {
    sprintf(freq, "%d", prim20_freq);
    }
    strcat(set_p2p_go_cmd, freq);
    if (strlen(set_p2p_go_cmd) >= sizeof(set_p2p_go_cmd)) {
        AIC_LOG_PRINTF("Cmd truncated. need %d bytes\n", strlen(set_p2p_go_cmd));
        return -1;
    }

    user_set_wfd_type(1);
    user_set_wfd_enable(1);

    int res = (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, -1, set_p2p_go_cmd) |
                fhost_wpa_enable_network(fhost_vif_idx, 20000));

    if (g_aic_wifi_event_cb) {
        aic_wifi_event_data enData = {0};
        enData.data.ap_go_status_data.reserved[0] = res;
        g_aic_wifi_event_cb(GO_STARTED_EVENT, &enData);
    }
    return res;
}

#define RWNX_VIF_TYPE(vif) fhost_vif->mac_vif->type
int get_cs_info(u8 *mac_addr, u8 *val)
{
    struct sta_info_tag *sta = NULL;
    u8 phymode = 0;
    u32 tx_phyrate = 0, rx_phyrate = 0;
    struct aicwf_cs_info cs_info;
    struct station_info sta_info;
    struct rwnx_sta_stats *stats;
    struct rx_vector_2 *rx_vect2;
    struct fhost_vif_tag *fhost_vif = &fhost_env.vif[0];
    

    printk("%s %02x:%02x:%02x:%02x:%02x:%02x\r\n", __func__, mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    if (RWNX_VIF_TYPE(fhost_vif) == VIF_MONITOR) {
        return -1;
    } else if ((RWNX_VIF_TYPE(fhost_vif) == VIF_STA)) {
        printk("%s: sta mode\n", __func__);
        if(fhost_vif->ap_id < NX_REMOTE_STA_MAX) {
            sta = vif_mgmt_get_sta_by_staid(fhost_vif->ap_id);
        }
    } else {
        printk("%s: AP mode\n", __func__);
        sta = vif_mgmt_get_sta_by_addr((const struct mac_addr *)mac_addr);
    }

    memset(&cs_info, 0, sizeof(struct aicwf_cs_info));
    memcpy(cs_info.countrycode, current_country_code, 3);

    // if((RWNX_VIF_TYPE(vif) == VIF_AP)) {
    //     sta->center_freq = ctxt->chan_def.chan->center_freq;
    //     sta->width = ctxt->chan_def.width;
    // }

    // printk("%s %x\r\n", __func__, sta);
    if (sta) {
        stats = &sta->stats;
        rx_vect2 = &stats->last_rx.rx_vect2;

        aicwf_get_station_info(sta, &sta_info, &phymode, &tx_phyrate, &rx_phyrate);

        cs_info.rssi = sta_info.signal;
        cs_info.bandwidth = fhost_vif->mac_vif->chan.type;
        cs_info.freq = fhost_vif->mac_vif->chan.center1_freq;

        cs_info.phymode = phymode; // 0:b 1:g 2:a 3:n 4:ac 5:ax
        //snr (int8_t)rx_vect2->evm1, (int8_t)rx_vect2->evm2
        cs_info.snr = (int8_t)(rx_vect2->evm1) + (int8_t)(rx_vect2->evm2) / 2;
        cs_info.noise = cs_info.rssi - cs_info.snr; //rssi - snr

        //chanutil TBD
        cs_info.chan_time_ms = stats->last_chan_time;
        cs_info.chan_time_busy_ms = stats->last_chan_busy_time;
        cs_info.tx_ack_succ_stat = stats->tx_ack_succ_stat;
        cs_info.tx_ack_fail_stat = stats->tx_ack_fail_stat;
        cs_info.chan_tx_time_busy_ms = stats->last_chan_tx_busy_time;

        extern uint32_t get_user_pwrlvl_11a_2g4(void);
        extern uint32_t get_user_pwrlvl_11a_5g(void);
        cs_info.txpwr = cs_info.freq >5000 ? get_user_pwrlvl_11a_5g() : get_user_pwrlvl_11a_2g4();
        if(sta->staid < NX_REMOTE_STA_MAX) {
            cs_info.rxnss = sta_info.rxrate.nss;
            cs_info.rxmcs = sta_info.rxrate.mcs;
            cs_info.txnss = sta_info.txrate.nss;
            cs_info.txmcs = sta_info.txrate.mcs;
        }

        cs_info.tx_phyrate = tx_phyrate;
        cs_info.rx_phyrate = rx_phyrate;

        memcpy(val, &cs_info, sizeof(struct aicwf_cs_info));

        printk("phymode=%d. bw=%d, rssi=%d, tx_phyrate=%d, rx_phyrate=%d\n", cs_info.phymode, cs_info.bandwidth, cs_info.rssi,
                                        tx_phyrate, rx_phyrate);

        return sizeof(struct aicwf_cs_info);
    }

    return 0;
}

#if 0
extern uint8_t p2p_started;
HOSTAP_HANDLE * aic_start_ap(WiFiConfig * config)
{
    if(p2p_started) {
        if(strncmp(config->ssid, "DIRECT-", 7) != 0) {
            printf("Stop p2p before enable ap\n");
            wlan_stop_p2p();
        } else {
            printf("No need to start ap when p2p is on\n");
            return 1;
        }
    }

    aic_wifi_set_mode(WIFI_MODE_AP);
    if(!config) {
        #define AP_SSID_STRING  "AIC-AP-SUNPLUS"
        #define AP_PASS_STRING  "kkkkkkkk"
        struct aic_ap_cfg user_ap_cfg = {
            .aic_ap_ssid = {
                strlen(AP_SSID_STRING),
                AP_SSID_STRING
            },
            .aic_ap_passwd = {
                strlen(AP_PASS_STRING),
                AP_PASS_STRING
            },
            .band = 1,
            .type = PHY_CHNL_BW_20,
            .channel = 149,
            .hidden_ssid = 0,
            .max_inactivity = 60,
            .enable_he = 1,
            .enable_acs = 0,
            .bcn_interval = 100,
            .sta_num = 10,
        };
        return (void*)wlan_start_ap(&user_ap_cfg);
        #undef AP_SSID_STRING
        #undef AP_PASS_STRING
    } else {
        struct aic_ap_cfg user_ap_cfg = {
            .band = 0,
            .type = PHY_CHNL_BW_20,
            .channel = config->channel,
            .hidden_ssid = 0,
            .max_inactivity = 60,
            .enable_he = 1,
            .enable_acs = 0,
            .bcn_interval = config->beacon_int,
            .sta_num = 10,
        };
        user_ap_cfg.aic_ap_ssid.length = strlen(config->ssid);
        strcpy(user_ap_cfg.aic_ap_ssid.array, config->ssid);
        user_ap_cfg.aic_ap_passwd.length = strlen(config->wpa_passphrase);
        strcpy(user_ap_cfg.aic_ap_passwd.array, config->wpa_passphrase);

        return (void*)wlan_start_ap(&user_ap_cfg);
    }
}

void aic_stop_ap(HOSTAP_HANDLE * handle)
{
    wlan_stop_ap();
}

HOSTAP_HANDLE * hostapd_enable_ap(WiFiConfig * config)
{
    char set_p2p_go_cmd[64];
    char freq[5];
    uint16_t prim20_freq;
    int fhost_vif_idx = 0;

    struct aic_p2p_cfg user_p2p_cfg = {
        .band = PHY_BAND_2G4,
        .type = PHY_CHNL_BW_20,
        .channel = config->channel,
        .enable_he = 1,
        .enable_acs = 0,
    };

    /* 2.4G: 1,2,3,4,5,6,7,8,9,10,11,12,13,14 */
    /* 5G:   7,8,9,11,12,16,34,36...*/
    if(config->channel > 14)  //AIC not support set channel 7~16 as 5G band
        user_p2p_cfg.band = PHY_BAND_5G;

    aic_wifi_set_mode(WIFI_MODE_AP);

    user_p2p_cfg.aic_p2p_ssid.length = strlen(config->ssid);
    strcpy(user_p2p_cfg.aic_p2p_ssid.array, config->ssid);
    user_p2p_cfg.aic_ap_passwd.length = strlen(config->wpa_passphrase);
    strcpy(user_p2p_cfg.aic_ap_passwd.array, config->wpa_passphrase);

    printf("p2p state:%d\n", p2p_started);
    if(p2p_started) {
        wlan_stop_p2p();
    }
    if(!p2p_started) {
        g_rwnx_hw->net_id = wlan_start_p2p(&user_p2p_cfg);
    }

    user_p2p_setDN(user_p2p_cfg.aic_p2p_ssid.array);

    strcpy(set_p2p_go_cmd, "P2P_GROUP_ADD he ");
    strcat(set_p2p_go_cmd, "pass=");
    strcat(set_p2p_go_cmd, user_p2p_cfg.aic_ap_passwd.array);
    strcat(set_p2p_go_cmd, " freq=");

    if ((user_p2p_cfg.band == 0) && (user_p2p_cfg.channel == 0)) {
        prim20_freq = phy_channel_to_freq(PHY_BAND_2G4, 11);
    } else {
        prim20_freq = phy_channel_to_freq(user_p2p_cfg.band, user_p2p_cfg.channel);
    }

    memset(freq, 0, sizeof(freq));
    sprintf(freq, "%d", prim20_freq);
    strcat(set_p2p_go_cmd, freq);
    printf("set_p2p_go_cmd as %s\n", set_p2p_go_cmd);

    fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 300, "WFD_SUBELEM_SET 0 000600111C440032");
    int res = (fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, -1, set_p2p_go_cmd) |
                fhost_wpa_enable_network(fhost_vif_idx, 20000));

    return (HOSTAP_HANDLE *)1;
}

void hostapd_disable_ap(HOSTAP_HANDLE * handle)
{
    wlan_stop_ap();
}

int wifi_drv_feature_config(wifi_drv_conf* conf)
{
    return 0;
}

int hostapd_user_wps_button_pushed(HOSTAP_HANDLE * handle,       const unsigned char *p2p_dev_addr)
{
    int fhost_vif_idx = 0;
    printf("%s in\n", __func__);
    fhost_wpa_execute_cmd(fhost_vif_idx, NULL, NULL, 300, "WPS_PBC");
    return 0;
}

int hostapd_set_hidden_ssid_mode(HOSTAP_HANDLE * handle, int is_hidden)
{
    return 0;
}

int hostapd_remove_sta(HOSTAP_HANDLE * handle, const unsigned char *sta_addr)
{
    return 0;
}

void hostapd_commmand(int argc, char ** argv) {}

int wpa_enable_sta(void)
{
    char * param[4];

    param[0] = "iwpriv";
    param[1] = "wlan0";
    param[2] = "p2p_set";
    param[3] = "sta_enable=1";
    return iw_cmd( 4, param);
}

int wpa_disable_sta(void)
{
    char * param[4];

    param[0] = "iwpriv";
    param[1] = "wlan0";
    param[2] = "p2p_set";
    param[3] = "sta_enable=0";
    return iw_cmd( 4, param);
}

int wpa_sta_disconnect(const unsigned char *mac)
{
    (void)mac;
    int ret = 0;
    char * param[4];

    ret = wlan_disconnect_sta(0);
    if(ret < 0)
        printf("disconnect sta fail\n");

    param[0] = "iwpriv";
    param[1] = "wlan0";
    param[2] = "p2p_set";
    param[3] = "sta_enable=1";
    ret = iw_cmd( 4, param);
    if(ret < 0) {
        diag_printf("wifi_sta_enable fail!\n");
    }
    return ret;
}

int wpa_sta_connect(const char *ssid, const char *passwd, int timeout_ms)
{
    return wlan_sta_connect(ssid, passwd, timeout_ms);
}

void wpa_sta_scan()
{
    wlan_if_scan();
}

void wpa_sta_getscan(hostapd_scan_list_t *result_list)
{
    wifi_ap_list_t *ap_list = rtos_malloc(sizeof(wifi_ap_list_t));
    int i = 0;
    int max_result_cnt = 0;

    wlan_if_getscan(ap_list);

    max_result_cnt = sizeof(result_list->ap_info)/sizeof(result_list->ap_info[0]);
    result_list->ap_count = ap_list->ap_count;

    for(i = 0; i < max_result_cnt && i < ap_list->ap_count; i++) {
        rtos_memcpy(result_list->ap_info[i].ssid,
                     ap_list->ap_info[i].ssid,
                      sizeof(result_list->ap_info[i].ssid));
        rtos_memcpy(result_list->ap_info[i].bssid,
                     ap_list->ap_info[i].bssid,
                     sizeof(result_list->ap_info[i].bssid));

        result_list->ap_info[i].channel = ap_list->ap_info[i].channel;
        result_list->ap_info[i].rssi = ap_list->ap_info[i].rssi;
    }

    rtos_free(ap_list);
}
#endif

