#if 0
#include "ipc_host.h"
#include "rtos_def.h"
#include "rtos_al.h"
//#include "core_cm4.h"
#include "rwnx_defs.h"
#include "msg.h"
#include "mac.h"
#include "net_al.h"
#include "fhost.h"
#include "ipc_shared.h"
//#include "ll.h"
#include "rwnx_msg_tx.h"
#include "fhost_config.h"
#include "fhost_cntrl.h"
#include "rwnx_utils.h"
#include "rwnx_msg_tx.h"
//#include "reg_riu.h"
//#include "reg_wlan_rf.h"
//#include "rf_config.h"

/// GLOBAL
struct ipc_host_env_tag ipc_env;
static rtos_task_handle ipc_cntrl_task_handle;

/// Master message queue
rtos_queue ipc_queue;

#define FHOST_IPC_CNTRL_QUEUE_SIZE  40
extern rtos_semaphore ipc_emb_ready_sema;

/**
 ****************************************************************************************
 * @brief Start WiFi stack.
 *
 * Send messages to configure and start the WiFi STack and then add a first STA interface.
 * Configuration is read form IPC buffer so that it can be modified via driver for test
 * purpose.
 *
 * @return 0 on success and != 0 otherwise
 ****************************************************************************************
 */
extern struct me_chan_config_req fhost_chan;
static uint8_t ipc_host_cntrl_started = 0;
int ipc_host_cntrl_start(void)
{
    struct me_config_req me_config;
    struct mm_start_req start;
    struct fhost_vif_tag *fhost_vif;
    struct mac_addr base_mac_addr;

    if (!ipc_host_cntrl_started) {
        fhost_config_prepare(&me_config, &start, &base_mac_addr, false);
        // Register net interfaces
        for (int i = 0; i < NX_VIRT_DEV_MAX; i++) {
            fhost_vif = &fhost_env.vif[i];
            fhost_vif->mac_addr = base_mac_addr;
            fhost_vif->mac_addr.array[2] ^= (i << 8);

            net_if_add(&fhost_vif->net_if, NULL, NULL, NULL, fhost_vif);
        }
        ipc_host_cntrl_started = 1;
    }

    return 0;
}

static uint8_t ipc_host_fw_initial = 0;
int ipc_host_fw_init(void)
{
    struct mm_version_cfm version;
    struct me_config_req me_config;
    struct mm_start_req start;
    struct mac_addr base_mac_addr;
    RWNX_DBG(RWNX_FN_ENTRY_STR);

    if (!ipc_host_fw_initial) {
        fhost_config_prepare(&me_config, &start, &base_mac_addr, true);

        rtos_semaphore_wait(ipc_emb_ready_sema, -1);
        rtos_semaphore_delete(ipc_emb_ready_sema);

        rwnx_ipc_rxbuf_init(&hw_env);
        ipc_host_fw_initial = 1;
    }

    /* Update tx/rx gain table before calib */
    if (rwnx_set_rf_config_req()) {
        return -2;
    }

    struct mm_set_rf_calib_cfm cfm;
    if (rwnx_set_rf_calib_req(&cfm)) {
        return -1;
    }
    update_rxgain_table(cfm.rxgain_24g_addr, cfm.rxgain_5g_addr);

    /* Set lp_level */
    fhost_cntrl_me_set_lp_level(wifi_sleep_level_get());

    /* Reset FW */
    if (rwnx_send_reset())
        return 1;
    if (rwnx_send_version_req(&version))
        return 2;

    if (rwnx_send_me_config_req(&me_config))
        return 3;
    if (rwnx_send_me_chan_config_req(&fhost_chan))
        return 4;
    if (rwnx_send_start(&start))
        return 5;

    //PHY register set
    REG_PL_WR(0x40340014, (REG_PL_RD(0x40340014)|(1<<3))); //crm_mdmbrxclkforce_setf(1);
    REG_PL_WR(0x40330300, 0xD41E0A19);                      //mdm_febcntl_set(0xD41E0A19);

    return 0;
}
static uint8_t ipc_rx_desc_already = 0;
static int fhost_ipc_cntrl_write_msg(uint32_t status)
{
    struct ipc_irq_elem elem = {0};

    elem.status = status;

    if (IPC_IRQ_E2A_RXDESC == status) {
        if(ipc_rx_desc_already) {
            return 0;
        } else {
            ipc_rx_desc_already = 1;
        }
    }
    if (IPC_IRQ_E2A_MGMT_CFM == status) {
        elem.param = ipc_env.shared->statinfo;
    }
    return rtos_queue_write(ipc_queue, &elem, 0, true);
}

void ipc_irq_hdlr(void)
{
    RWNX_DBG(RWNX_FN_ENTRY_STR);

    uint32_t status = 0;
    int ret = 0;

    while ((status = ipc_host_get_status()))
    {
        /* All kinds of IRQs will be handled in one shot (RX, MSG, DBG, ...)
         * this will ack IPC irqs not the cfpga irqs */
        //ipc_host_irq(hw_env.ipc_env, status);
        //aic_dbg("%s,st %08x\r\n", __func__, status);
//        ipc_emb2app_ack_clear(status);

        ret = fhost_ipc_cntrl_write_msg(status);
        if (ret) {
            aic_dbg("ipc irq write msg fail\r\n");
        }
    }

    return;
}


/**
 ****************************************************************************************
 * @brief IPC task main loop
 *
 * IPC task may received command from the host via IPC, or information event from the
 * supplicant task.
 *
 ****************************************************************************************
 */
static RTOS_TASK_FCT(ipc_cntrl_task)
{
    RWNX_DBG(RWNX_FN_ENTRY_STR);

    struct ipc_irq_elem elem = {0};
    uint32_t ipc_irq_prio = (uint32_t)env;

    // set ipc handler
    NVIC_SetPriority(WCN2MCU2_IRQn, ipc_irq_prio);
    NVIC_SetVector(WCN2MCU2_IRQn, (uint32_t)ipc_irq_hdlr);
    NVIC_EnableIRQ(WCN2MCU2_IRQn);
    rwnx_ipc_start(&hw_env);

    aic_dbg("ipc host start...\n");

    for (;;) {
        rtos_queue_read(ipc_queue, &elem, -1, false);

        //while ((status = ipc_host_get_status()))
        {
            /* All kinds of IRQs will be handled in one shot (RX, MSG, DBG, ...)
             * this will ack IPC irqs not the cfpga irqs */
            ipc_host_irq(&ipc_env, &elem);

            if (elem.status & IPC_IRQ_E2A_RXDESC) {
                ipc_rx_desc_already = 0;
            }
        }
    }
}

/**
 ****************************************************************************************
 * @brief IPC task runtime initialization
 *
 * Initialization when IPC task is first ran:
 * - IPC task is created with lower priority to be sure that control task is executed
 *   first to create the network interface. Now we can move to the "real" priority
 * - Assign default IP address to all network interface
 ****************************************************************************************
 */
int fhost_ipc_cntrl_init(uint32_t ipc_irq_prio)
{
    int res;

    if (rtos_semaphore_create(&ipc_emb_ready_sema, 1, 0))
        return -1;
	#if 0

    if (rtos_queue_create(sizeof(struct ipc_irq_elem), FHOST_IPC_CNTRL_QUEUE_SIZE, &ipc_queue))
        return -2;

    rtos_task_cfg_st cfg = get_task_cfg(IPC_CNTRL_TASK);

    res = rtos_task_create(ipc_cntrl_task, "IPC cntrl task", IPC_CNTRL_TASK, cfg.stack_size, (void *)ipc_irq_prio,
                           cfg.priority, &ipc_cntrl_task_handle);
	#endif
    return res;
}
#endif

