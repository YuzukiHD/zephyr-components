/**
 ****************************************************************************************
 *
 * @file rtos_main.h
 *
 * @brief Declarations related to the WiFi stack integration within an RTOS.
 *
 * Copyright (C) RivieraWaves 2017-2019
 *
 ****************************************************************************************
 */

#ifndef RTOS_MAIN_H_
#define RTOS_MAIN_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "rtos_al.h"
#include "rwnx_defs.h"

struct fhost_vif_tag;
struct cfg80211_chan_def;

#define CHIP_REV_U01        0x1
#define CHIP_REV_U02        0x3
#define CHIP_REV_U03        0x7
#define CHIP_SUB_REV_U04    0x20

extern u8 chip_id;
extern u8 chip_sub_id;
extern u8 chip_mcu_id;

#define CHIP_ID_H_MASK  0xC0
#define IS_CHIP_ID_H()  ((chip_id & CHIP_ID_H_MASK) == CHIP_ID_H_MASK)

#if 0
/*
 * DEFINITIONS
 ****************************************************************************************
 */
/// Definitions of the different FHOST task priorities
enum
{
    TASK_PRIORITY_CONSOLE              = RTOS_TASK_PRIORITY(1),
    TASK_PRIORITY_TEST                 = RTOS_TASK_PRIORITY(1),
    TASK_PRIORITY_BT_TASK              = RTOS_TASK_PRIORITY(2),
    TASK_PRIORITY_INTERSYS_TASK        = RTOS_TASK_PRIORITY(2),
    TASK_PRIORITY_ASIO                 = RTOS_TASK_PRIORITY(3),
    TASK_PRIORITY_AUDIO                = RTOS_TASK_PRIORITY(3),
    TASK_PRIORITY_DSP                  = RTOS_TASK_PRIORITY(2),
    TASK_PRIORITY_WIFI_VOICE           = RTOS_TASK_PRIORITY(1),
    TASK_PRIORITY_WIFI_TCPIP           = RTOS_TASK_PRIORITY(3),
    TASK_PRIORITY_WIFI_CNTRL           = RTOS_TASK_PRIORITY(2),
    TASK_PRIORITY_WIFI_IPC             = RTOS_TASK_PRIORITY(4),
    TASK_PRIORITY_WIFI_WPA             = RTOS_TASK_PRIORITY(1),
    TASK_PRIORITY_WIFI_TG_SEND         = RTOS_TASK_PRIORITY(2),
    TASK_PRIORITY_WIFI_PING_SEND       = RTOS_TASK_PRIORITY(1),
    TASK_PRIORITY_WIFI_IPERF           = RTOS_TASK_PRIORITY(1),
    TASK_PRIORITY_WIFI_SMARTCONF       = RTOS_TASK_PRIORITY(2),
    TASK_PRIORITY_WIFI_TX              = RTOS_TASK_PRIORITY(4),
    TASK_PRIORITY_CO_MAIN              = RTOS_TASK_PRIORITY(2),
    TASK_PRIORITY_WIFI_USER            = RTOS_TASK_PRIORITY(1),
    TASK_PRIORITY_USB_BT               = RTOS_TASK_PRIORITY(2),
    TASK_PRIORITY_MAX                  = RTOS_TASK_PRIORITY(configMAX_PRIORITIES - 1),
};

/// Definitions of the different FHOST task stack size requirements
enum
{
    TASK_STACK_SIZE_CONSOLE              = 512,
    TASK_STACK_SIZE_TEST                 = 2048,
    TASK_STACK_SIZE_BT_TASK              = 3072,
    TASK_STACK_SIZE_ASIO                 = 2048,
    TASK_STACK_SIZE_AUDIO                = 2048,
    TASK_STACK_SIZE_BLE_TASK_ONLY        = 512,
    TASK_STACK_SIZE_BT_BLE_TASK          = 4096,
    TASK_STACK_SIZE_DSP                  = 512,
    TASK_STACK_SIZE_WIFI_VOICE           = 512,
    TASK_STACK_SIZE_WIFI_TCPIP           = 1024,
    TASK_STACK_SIZE_WIFI_CNTRL           = 640,
    TASK_STACK_SIZE_WIFI_IPC             = 512,
    TASK_STACK_SIZE_WIFI_WPA             = 1024,
    TASK_STACK_SIZE_WIFI_TG_SEND         = 1024,
    TASK_STACK_SIZE_WIFI_PING_SEND       = 512,
    TASK_STACK_SIZE_WIFI_IPERF           = 1024,
    TASK_STACK_SIZE_WIFI_SMARTCONF       = 512,
    TASK_STACK_SIZE_WIFI_TX              = 512,
    TASK_STACK_SIZE_CO_MAIN              = 512,
    TASK_STACK_SIZE_WIFI_USER            = 512,
    TASK_STACK_SIZE_USB_BT               = 512,
};

typedef struct {
    int priority;
    int stack_size;
} rtos_task_cfg_st;
rtos_task_cfg_st get_task_cfg(uint8_t task_id);

/*
 * FUNCTIONS
 ****************************************************************************************
 */

/**
 * Save user data that declared with PRIVATE_HOST_*(G3USER)
 */
void user_data_save(void);

/**
 * Restore user data that declared with PRIVATE_HOST_*(G3USER)
 */
void user_data_restore(void);

/**
 ****************************************************************************************
 * @brief Main function of the RTOS
 *
 * Called after hardware initialization to create all RTOS tasks and start the scheduler.
 ****************************************************************************************
 */
void rtos_main(void);
#endif

void rwnx_frame_parser(char* tag, char* data, unsigned long len);
void rwnx_data_dump(char* tag, void* data, unsigned long len);

int rwnx_fdrv_init(struct rwnx_hw *rwnx_hw);
int rwnx_fdrv_deinit(struct rwnx_hw *rwnx_hw);
void rwnx_chanctx_link(struct fhost_vif_tag *vif, u8 ch_idx, struct cfg80211_chan_def *chandef);
void rwnx_chanctx_unlink(struct fhost_vif_tag *vif);                       
#endif // RTOS_MAIN_H_

