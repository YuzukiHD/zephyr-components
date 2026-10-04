#define _PORTING_C_
#include <porting.h>
#include "plat_config.h"

#define APP_INCLUDE_WIFI_FW

/*============Task Priority===================*/
uint32_t sdio_datrx_priority = SDIO_DATRX_PRIORITY;
uint32_t fhost_cntrl_priority = FHOST_CNTRL_PRIORITY;
uint32_t fhost_wpa_priority = FHOST_WPA_PRIORITY;
uint32_t fhost_tx_priority = FHOST_TX_PRIORITY;
uint32_t fhost_rx_priority = FHOST_RX_PRIORITY;
uint32_t cli_cmd_priority = CLI_CMD_PRIORITY;
uint32_t rwnx_timer_priority = RWNX_TIMER_PRIORITY;
uint32_t rwnx_sta_mgmt_priority = RWNX_STA_MGMT_PRIORITY;
uint32_t tcpip_priority = TCPIP_PRIORITY;
uint32_t wpa_rwnx_driver_priority = FHOST_WPA_DRIVER_PRIORITY;
uint32_t task_end_prio = TASK_END_PRIO;
uint32_t aic_priority_mode = AIC_PRIORITY_MODE;

/*============Stack Size (unint: 16bytes)===================*/
uint32_t sdio_datrx_stack_size = SDIO_DATRX_STACK_SIZE;
uint32_t fhost_cntrl_stack_size = FHOST_CNTRL_STACK_SIZE;
uint32_t fhost_wpa_stack_size = FHOST_WPA_STACK_SIZE;
uint32_t fhost_tx_stack_size = FHOST_TX_STACK_SIZE;
uint32_t fhost_rx_stack_size = FHOST_RX_STACK_SIZE;
uint32_t cli_cmd_stack_size = CLI_CMD_STACK_SIZE;
uint32_t rwnx_timer_stack_size = RWNX_TIMER_STACK_SIZE;
uint32_t rwnx_sta_mgmt_stack_size = RWNX_STA_MGMT_STACK_SIZE;
uint32_t wpa_rwnx_driver_stack_size = FHOST_WPA_DRIVER_STACK_SIZE;

/*=============console==================*/
uint8_t hal_getchar(void)
{
    uint8_t data=0;
    data = getchar();
    return data;
}

#define PRINT_BUF_SIZE                  512

void platform_pwr_en_pin_init(void)
{
    platform_config_init();
}

void platform_pwr_en_pin_set(bool en)
{
    platform_set_regon_en(en);
}

/* SDIO IRQ stubs for platforms without CONFIG_SDIO_IRQ_SUPPORT */
#ifndef CONFIG_SDIO_IRQ_SUPPORT
int sdio_claim_irq(void *func, void *handler) { return 0; }
int sdio_release_irq(void *func) { return 0; }
#endif
