/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
 *
 * All Rights Reserved
 */

/*
 * SDIO transport of the AIC8800 on the Zephyr SD subsystem.
 *
 * The chip sits on one SD host controller (devicetree node "aicsemi,aic8800"
 * under it). The subsystem serialises single bus operations with the card
 * mutex; sequences that must not be interleaved are covered by sdio_dev.bus_txrx.
 * The card interrupt (DAT1) is a one-shot of the host controller: it is armed
 * again by the receive task once the data has been taken.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/sd/sd.h>
#include <zephyr/sd/sdio.h>

#include "porting.h"
#include "aic_log.h"

#include "sdio_def.h"
#include "sdio_port.h"
#include "plat_config.h"
#include "lmac_msg.h"
#include "rwnx_main.h"
#include "rtos_al.h"
#include "fhost_rx.h"
#include "rtos_errno.h"
#include "wifi.h"
#include "wlan_if.h"

#define SDIOM_MAX_FUNCS 3

extern bool_l func_flag_tx;
extern bool_l func_flag_rx;

extern struct rwnx_hw *g_rwnx_hw;

#define CONFIG_TXMSG_TEST_EN    0

#define FUNC_0  0
#define FUNC_1  1
#define FUNC_2  2

#define udelay(n)	rtos_udelay(n)

#define SDIO_ANY_ID (~0)

/* the controller does DMA on cache line aligned buffers only */
#define SYS_CACHE_LINE_SIZE  64U
#define WCN_CACHE_ALIGNED(addr) (((uint32_t)(addr) + (SYS_CACHE_LINE_SIZE - 1)) & (~(SYS_CACHE_LINE_SIZE - 1)))
#define IS_WCN_CACHE_ALIGNED(addr) !((uint32_t)(addr) & (SYS_CACHE_LINE_SIZE - 1))
#define WCN_CACHE_ALIGN_VARIABLE    __attribute__((aligned(SYS_CACHE_LINE_SIZE)))
WCN_CACHE_ALIGN_VARIABLE static uint32_t sdiom_cache_align_addr[SYS_CACHE_LINE_SIZE/sizeof(uint32_t)];

/* the SD card of the subsystem and its I/O functions; functions are NULL until probed */
static struct sd_card sd_card_inst;
static struct sdio_func sdio_func_inst[SDIOM_MAX_FUNCS];
static struct sdio_func *sdio_function[SDIOM_MAX_FUNCS];
static struct sd_card *sdio_card;
static const struct device *sdhc_dev;
static bool sdio_card_up;
static uint32_t sdio_block_size;
typedef void (*SDIO_ISR_FUNC)(void);

SDIO_ISR_FUNC g_sdio_isr_func = NULL;

static int msgcfm_poll_en = 1;
static rtos_semaphore sdio_rx_sema = NULL;
struct aic_sdio_dev sdio_dev = {NULL,};
static rtos_task_handle sdio_task_hdl = NULL;

#if (FHOST_RX_SW_VER == 3)
static struct sdio_buf_node_s sdio_rx_buf_node[SDIO_RX_BUF_COUNT];
WCN_CACHE_ALIGN_VARIABLE static uint8_t sdio_rx_buf_pool[SDIO_RX_BUF_COUNT][SDIO_RX_BUF_SIZE];
static struct sdio_buf_list_s sdio_rx_buf_list;
#endif

/* the subsystem takes the card mutex for each operation */
#define sdio_claim_host(card)   do { (void)(card); } while (0)
#define sdio_release_host(card) do { (void)(card); } while (0)

/*
 * Transfer to or from a FIFO (fixed address) of a function: whole blocks in
 * block mode, the rest in byte mode.
 */
static int sdio_fifo_xfer(struct sdio_func *func, bool write, uint32_t addr, uint8_t *buf,
			  uint32_t count)
{
	uint32_t bs = func->block_size;
	int ret;

	if (bs != 0U && count >= bs) {
		uint32_t blocks = count / bs;

		ret = write ? sdio_write_blocks_fifo(func, addr, buf, blocks)
			    : sdio_read_blocks_fifo(func, addr, buf, blocks);
		if (ret != 0) {
			return ret;
		}
		buf += blocks * bs;
		count -= blocks * bs;
	}
	if (count == 0U) {
		return 0;
	}

	return write ? sdio_write_fifo(func, addr, buf, count)
		     : sdio_read_fifo(func, addr, buf, count);
}

static int sdio_readsb(struct sd_card *card, uint32_t func_num, void *dst, unsigned int addr,
		       int count)
{
	if (func_num >= SDIOM_MAX_FUNCS || sdio_function[func_num] == NULL) {
		return -ENODEV;
	}

	return sdio_fifo_xfer(sdio_function[func_num], false, addr, dst, count);
}

static int sdio_writesb(struct sd_card *card, uint32_t func_num, unsigned int addr, void *src,
			int count)
{
	if (func_num >= SDIOM_MAX_FUNCS || sdio_function[func_num] == NULL) {
		return -ENODEV;
	}

	return sdio_fifo_xfer(sdio_function[func_num], true, addr, src, count);
}

/* single byte write to function 0 (the chip specific registers 0xF0..0xFF and the CCCR) */
void sdio_f0_writeb(struct sdio_func *func, unsigned char b, unsigned int addr, int *err_ret)
{
	int ret;

	if (func == NULL) {
		if (err_ret) {
			*err_ret = -EINVAL;
		}
		return;
	}
	ret = sdio_write_byte(&func->card->func0, addr, b);
	if (err_ret) {
		*err_ret = ret;
	}
}

static int aic_sdio_enable_func(struct sdio_func *func)
{
	return sdio_enable_func(func);
}

static int aic_sdio_set_func_block_size(struct sdio_func *func, unsigned int blksize)
{
	return sdio_set_block_size(func, blksize);
}

/*
 * Find an I/O function by interface class, manufacturer and card id (the
 * manufacturer and card id of the CIS tuple); SDIO_ANY_ID matches all.
 */
struct sdio_func *sdio_find_func(u8 class, u16 vendor, u16 device)
{
	for (int i = FUNC_1; i < SDIOM_MAX_FUNCS; i++) {
		struct sdio_func *func = &sdio_func_inst[i];
		uint8_t fbr = 0;

		if (func->card == NULL) {
			continue;
		}
		/* a function without a manufacturer tuple of its own has the one of the card */
		const struct sdio_cis *cis = (func->cis.manf_id == 0 && func->cis.manf_code == 0) ?
					     &func->card->func0.cis : &func->cis;

		if (class != (u8)SDIO_ANY_ID) {
			/* standard interface code of the function basic register */
			if (sdio_read_byte(&func->card->func0, SDIO_FBR_BASE(i), &fbr) != 0 ||
			    (fbr & 0x0f) != class) {
				continue;
			}
		}
		if (vendor != (u16)SDIO_ANY_ID && vendor != cis->manf_id) {
			continue;
		}
		if (device != (u16)SDIO_ANY_ID && device != cis->manf_code) {
			continue;
		}
		return func;
	}

	return NULL;
}

static int sdio_card_irq_set(int func, bool enable);
static void sdio_irq_rearm(void);

bool sdio_readb_cmd52(uint32_t addr, uint8_t *data)
{
    int err;
    uint8_t  val = 0;

    if (sdio_function[FUNC_1] == NULL) {
        return FALSE;
    }
    err = sdio_read_byte(sdio_function[FUNC_1], addr, &val);
    if(err) {
        printf("sdio_readb_cmd52 fail %d!\n", err);
        return FALSE;
    }

    *data = val;
    return TRUE;
}

bool sdio_readb_cmd52_func2(uint32_t addr, uint8_t *data)
{
    int err;
    uint8_t  val = 0;

    if (sdio_function[FUNC_2] == NULL) {
        return FALSE;
    }
    err = sdio_read_byte(sdio_function[FUNC_2], addr, &val);
    if(err) {
        printf("sdio_readb_cmd52_func2 fail %d!\n", err);
        return FALSE;
    }

    *data = val;
    return TRUE;
}

bool sdio_writeb_cmd52(uint32_t addr, uint8_t data)
{
    int err;

    if (sdio_function[FUNC_1] == NULL) {
        return FALSE;
    }
    err = sdio_write_byte(sdio_function[FUNC_1], addr, data);
    if(err) {
        AIC_LOG_PRINTF("sdio_writeb_cmd52 fail %d!\n", err);
        return FALSE;
    }

    return TRUE;
}

bool sdio_writeb_cmd52_func2(uint32_t addr, uint8_t data)
{
    int err;

    if (sdio_function[FUNC_2] == NULL) {
        return FALSE;
    }
    err = sdio_write_byte(sdio_function[FUNC_2], addr, data);
    if(err) {
        AIC_LOG_PRINTF("sdio_writeb_cmd52_func2 fail %d!\n", err);
        return FALSE;
    }

    return TRUE;
}

/* CMD53 at an incrementing address: the data port of function 1 */
bool sdio_read_cmd53(uint32_t dataPort, uint8_t *dat, size_t size)
{
    int ret;

    if (sdio_function[FUNC_1] == NULL) {
        return FALSE;
    }
    ret = sdio_read_addr(sdio_function[FUNC_1], dataPort, dat, size);
    if (ret != 0) {
        AIC_LOG_PRINTF("sdio_read_cmd53 size = %u, fail %d!\n", (unsigned int)size, ret);
        return FALSE;
    }

    return TRUE;
}

bool sdio_write_cmd53(uint32_t dataPort, uint8_t *dat, size_t size)
{
    int ret;

    if (sdio_function[FUNC_1] == NULL) {
        return FALSE;
    }
    ret = sdio_write_addr(sdio_function[FUNC_1], dataPort, dat, size);
    if (ret != 0) {
        AIC_LOG_PRINTF("sdio_write_cmd53 size = %u, fail %d!!\n", (unsigned int)size, ret);
        return FALSE;
    }

    return TRUE;
}

/*
 * Power the chip, enumerate the card on the host controller and set up the I/O
 * functions the chip has. Done once; the chip stays on the bus afterwards.
 */
int aic_sdio_card_probe(void)
{
    int ret = -ENODEV;

    if (sdio_card_up) {
        return 0;
    }
    sdhc_dev = platform_get_sdhc();
    if (sdhc_dev == NULL || !device_is_ready(sdhc_dev)) {
        AIC_LOG_PRINTF("sdio host not ready\n");
        return -ENODEV;
    }
    for (int try = 0; try < 3; try++) {
        aic_wifi_reset_power();
        ret = sd_init(sdhc_dev, &sd_card_inst);
        if (ret == 0) {
            break;
        }
        AIC_LOG_PRINTF("sd_init failed %d (try %d)\n", ret, try);
    }
    if (ret != 0) {
        return ret;
    }
    sdio_card = &sd_card_inst;
    for (int i = FUNC_1; i < SDIOM_MAX_FUNCS && i <= sd_card_inst.num_io; i++) {
        ret = sdio_init_func(&sd_card_inst, &sdio_func_inst[i], i);
        if (ret != 0) {
            AIC_LOG_PRINTF("sdio func %d init failed %d\n", i, ret);
            sdio_func_inst[i].card = NULL;
        }
    }
    sdio_card_up = true;
    AIC_LOG_PRINTF("sdio card up: %d functions\n", sd_card_inst.num_io);

    return 0;
}

#ifdef CONFIG_SDIO_BUS_PWRCTRL
int aicwf_sdio_bus_sleep_allow(struct aic_sdio_dev *sdiodev);

#if AICWF_RWNX_TIMER_EN
bool rwnx_timer_is_posted(rwnx_timer_handle timer_hdl);
int rwnx_timer_create(rwnx_timer_handle *timer_ptr, uint32_t period_ms,
                      void *arg, rwnx_timer_cb_t cb,
                      bool auto_load, bool periodic);
int rwnx_timer_stop(rwnx_timer_handle timer_hdl);
int rwnx_timer_start(rwnx_timer_handle timer_hdl, uint32_t period_ms, bool lock);
#endif
void aicwf_sdio_bus_pwrctl_timer_handler(void)
{
    //AIC_LOG_PRINTF(">>> timer_hdl");
    rtos_semaphore_signal(sdio_dev.pwrctl.pwrctl_trgg, 0);
}

void aicwf_sdio_bus_pwrctl_timer_modify(uint duration)
{
    if (sdio_dev.pwrctl.init_status == false) {
         return ;
    }

    rtos_mutex_lock(sdio_dev.pwrctl.pwrctl_lock, -1);
    #if AICWF_RWNX_TIMER_EN
    if (!duration) {
        //aic_dbg("%s %p  line=%d\n", __func__, sdio_dev.pwrctl.pwrctl_timer, __LINE__);
        //rtos_timer_get_status(sdio_dev.pwrctl.pwrctl_timer, &sdio_dev.pwrctl.timer_status);
        if (rwnx_timer_is_posted(sdio_dev.pwrctl.pwrctl_timer))
            rwnx_timer_stop(sdio_dev.pwrctl.pwrctl_timer);

    } else {
        //rtos_timer_get_status(sdio_dev.pwrctl.pwrctl_timer, &sdio_dev.pwrctl.timer_status);
        //aic_dbg("%s %p  line=%d\n", __func__, sdio_dev.pwrctl.pwrctl_timer, __LINE__);

        if (!rwnx_timer_is_posted(sdio_dev.pwrctl.pwrctl_timer))
            rwnx_timer_start(sdio_dev.pwrctl.pwrctl_timer, duration, 0);
        else {
            //aic_dbg("%s %p  line=%d\n", __func__, sdio_dev.pwrctl.pwrctl_timer, __LINE__);
            // restart tne timer.
            rwnx_timer_stop(sdio_dev.pwrctl.pwrctl_timer);
            rwnx_timer_start(sdio_dev.pwrctl.pwrctl_timer, duration, 0);
        }
    }
    #else
    if (!duration) {
        //aic_dbg("%s %p  line=%d\n", __func__, sdio_dev.pwrctl.pwrctl_timer, __LINE__);
        //rtos_timer_get_status(sdio_dev.pwrctl.pwrctl_timer, &sdio_dev.pwrctl.timer_status);
        if (rtos_timer_is_active(sdio_dev.pwrctl.pwrctl_timer))
            rtos_timer_stop(sdio_dev.pwrctl.pwrctl_timer, 0);
    } else {
        //rtos_timer_get_status(sdio_dev.pwrctl.pwrctl_timer, &sdio_dev.pwrctl.timer_status);
        //aic_dbg("%s %p  line=%d\n", __func__, sdio_dev.pwrctl.pwrctl_timer, __LINE__);

        if (0 == rtos_timer_is_active(sdio_dev.pwrctl.pwrctl_timer)) {
            rtos_timer_start(sdio_dev.pwrctl.pwrctl_timer, duration, 0);
        } else {
            rtos_timer_stop(sdio_dev.pwrctl.pwrctl_timer, 0);
            rtos_timer_start(sdio_dev.pwrctl.pwrctl_timer, duration, 0);
        }
    }
    #endif
    rtos_mutex_unlock(sdio_dev.pwrctl.pwrctl_lock);
}

void aicwf_sdio_bus_pwrctl_timer_task(void *arg)
{
    int ret = 0;
    uint32_t tx_pktcnt = 0, rx_cnt = 0;
	//AIC_LOG_PRINTF("%s enter\n", __func__);
    while(1) {
        ret = rtos_semaphore_wait(sdio_dev.pwrctl.pwrctl_trgg, -1);
        if (ret < 0) {
            AIC_LOG_PRINTF("wait pwrctrl_trgg fail: ret=%d\n", ret);
            continue;
        } else {
            rtos_mutex_lock(sdio_dev.tx_sema, -1);
            tx_pktcnt = sdio_dev.tx_pktcnt;
            rtos_mutex_unlock(sdio_dev.tx_sema);
            rtos_mutex_lock(sdio_dev.rx_sema, -1);
            rx_cnt = sdio_dev.rx_cnt;
            rtos_mutex_unlock(sdio_dev.rx_sema);
            //AIC_LOG_PRINTF("tx_pktcnt: %d, rx_cnt: %d, cmd_txstate:%d", tx_pktcnt, rx_cnt, cmd_txstate);
            // AIC_LOG_PRINTF("timer_task bus_status:%d", sdio_dev.pwrctl.bus_status);
            if (sdio_dev.pwrctl.bus_status == SDIO_BUS_ACTIVE) {
                if((tx_pktcnt == 0) && (sdio_dev.cmd_txstate == false) && (rx_cnt == 0)) {
                    // aicwf_sdio_pwr_stctl(sdiodev, SDIO_BUS_SLEEP);
                    aicwf_sdio_bus_sleep_allow(&sdio_dev); // Direct call this func, for save time.
                }
                else
                    aicwf_sdio_bus_pwrctl_timer_modify(sdio_dev.pwrctl.active_duration);
            }
        }
    }
}

void aicwf_sdio_bus_pwrctl_timer_task_init(void)
{
    int ret;
    #if AICWF_RWNX_TIMER_EN
    ret = rwnx_timer_create(&sdio_dev.pwrctl.pwrctl_timer, 30, NULL, aicwf_sdio_bus_pwrctl_timer_handler, 1, 0);
    if (ret) {
        AIC_LOG_PRINTF("sdio pwrctl timer create fail\n");
        return;
    }
    #else
    ret = rtos_timer_create("pwrctl_timer", &sdio_dev.pwrctl.pwrctl_timer, 30, 1, NULL, aicwf_sdio_bus_pwrctl_timer_handler);
    if (ret) {
        AIC_LOG_PRINTF("sdio pwrctl timer create fail\n");
        return;
    }
    #endif
    ret = rtos_task_create(aicwf_sdio_bus_pwrctl_timer_task, "aicwf_sdio_bus_pwrctl_timer_task",
                           0, 4096, NULL, 24,
                           &sdio_dev.pwrctl.pwrctl_task_hdl);
    if (ret < 0) {
        AIC_LOG_PRINTF("aicwf_sdio_bus_pwrctl_timer_task create fail\n");
        return;
    } else {
		AIC_LOG_PRINTF("aicwf_sdio_bus_pwrctl_timer_task create success\n");
	}
    sdio_dev.pwrctl.init_status = true;
    // if (sdio_writeb_cmd52(sdio_dev.sdio_reg.wakeup_reg, 4)) {
    //     aic_dbg("reg:%d write failed!\n", sdio_dev.sdio_reg.wakeup_reg);
    // }
}

int aicwf_sdio_bus_wakeup(struct aic_sdio_dev *sdiodev)
{
    int ret = 0;
    int read_retry;
    int write_retry = 1;
    int wakeup_reg_val = 0;

	//AIC_LOG_PRINTF("%s, enter\n", __func__);
    if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8801 ||
        g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DC ||
        g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DW) {
        wakeup_reg_val = 1;
    } else if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8800D80) {
        wakeup_reg_val = 0x11;
    }

    if (sdiodev->pwrctl.bus_status == SDIO_BUS_SLEEP) {
        rtos_mutex_lock(sdio_dev.pwrctl.bus_wakeup_sema, -1);
        if(sdiodev->pwrctl.bus_status == SDIO_BUS_ACTIVE) {
            rtos_mutex_unlock(sdio_dev.pwrctl.bus_wakeup_sema);
            return 0;
        }
        //AIC_LOG_PRINTF("wakeup\n");
        while(write_retry) {
            ret = sdio_writeb_cmd52(sdiodev->sdio_reg.wakeup_reg, wakeup_reg_val);
            if (ret == false) {
                AIC_LOG_PRINTF("sdio wakeup fail\n");
                ret = -1;
            } else {
                read_retry=50;
                while (read_retry) {
                    u8 val;
                    ret = sdio_readb_cmd52(sdiodev->sdio_reg.wakeup_reg, &val);
                    if (ret == false) {
                        AIC_LOG_PRINTF("sdio wakeup read fail\n");
                    } else if ((val & 0x1) == 0) {
                        //AIC_LOG_PRINTF("val: 0x%x", val);
                        break;
                    }
                    read_retry--;
                    udelay(200);
                }
                if(read_retry != 0)
                    break;
            }

            AIC_LOG_PRINTF("write retry:  %d \n",write_retry);
            write_retry--;
            udelay(100);
        }

        sdiodev->pwrctl.bus_status = SDIO_BUS_ACTIVE;
        aicwf_sdio_bus_pwrctl_timer_modify(sdio_dev.pwrctl.active_duration);
        rtos_mutex_unlock(sdio_dev.pwrctl.bus_wakeup_sema);
    }
    return ret;
}

int aicwf_sdio_bus_sleep_allow(struct aic_sdio_dev *sdiodev)
{
    int ret = 0, tx_pktcnt = 0;
	u8 read_retry;
	u8 val;

	//AIC_LOG_PRINTF("%s, enter\n", __func__);
    rtos_mutex_lock(sdio_dev.tx_sema, -1);
    tx_pktcnt = sdio_dev.tx_pktcnt;
    rtos_mutex_unlock(sdio_dev.tx_sema);
    if(tx_pktcnt > 0) {
        AIC_LOG_PRINTF("Cancel sdio sleep setting\n");
        return 0;
    }

	//AIC_LOG_PRINTF("bus_status: %d\n", sdiodev->pwrctl.bus_status);

    if ((sdiodev->pwrctl.bus_status == SDIO_BUS_ACTIVE)
#ifdef	CONFIG_SDIO_BUS_PWRCTRL_DYN
		&&(wlan_ap_get_associated_sta_cnt() == 0)
#endif /* CONFIG_SDIO_BUS_PWRCTRL_DYN */
	){
		//AIC_LOG_PRINTF("%s s\n", __func__);
        rtos_mutex_lock(sdio_dev.pwrctl.bus_wakeup_sema, -1);
		if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8800D80) {
			if (sdio_writeb_cmd52(sdiodev->sdio_reg.wakeup_reg, 0x02) < 0) {
				AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.wakeup_reg);
			}
		} else if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8801 || g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DC ||
				g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DW) {
			if (sdio_writeb_cmd52_func2(sdiodev->sdio_reg.wakeup_reg, 0x2) < 0) {
				AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.wakeup_reg);
			}
			read_retry = 100;
			while (read_retry) {
				val = 0;
				if (sdio_readb_cmd52_func2(sdiodev->sdio_reg.wakeup_reg, &val) < 0){
					AIC_LOG_PRINTF("reg %d read fail\n", sdiodev->sdio_reg.wakeup_reg);
				}else if ((val & 0x2) == 0) {
					break;
				} else {
					AIC_LOG_PRINTF("val:%d\n", val);
				}
				read_retry--;
				if(read_retry < 90)
					AIC_LOG_PRINTF("warning: read cnt %d\n", read_retry);
				udelay(500);
			}
		}

        sdiodev->pwrctl.bus_status = SDIO_BUS_SLEEP;
        aicwf_sdio_bus_pwrctl_timer_modify(0);
        rtos_mutex_unlock(sdio_dev.pwrctl.bus_wakeup_sema);
    }
    else{
        aicwf_sdio_bus_pwrctl_timer_modify(sdio_dev.pwrctl.active_duration);
    }

    return ret;
}

int aicwf_sdio_bus_pwr_stctl(struct aic_sdio_dev *sdiodev, uint target)
{
    int ret = 0;
#ifdef	CONFIG_SDIO_BUS_PWRCTRL_DYN
	if (wlan_ap_get_associated_sta_cnt()) {
        return ret;
    }
#endif /* CONFIG_SDIO_BUS_PWRCTRL_DYN */
	//AIC_LOG_PRINTF("%s, target:%d\n", __func__, target);
    rtos_mutex_lock(sdio_dev.pwrctl.bus_wakeup_sema, -1);
    if (sdiodev->pwrctl.bus_status == target) {
        if (target == SDIO_BUS_ACTIVE) {
            aicwf_sdio_bus_pwrctl_timer_modify(sdio_dev.pwrctl.active_duration);
        }
        rtos_mutex_unlock(sdio_dev.pwrctl.bus_wakeup_sema);
        return ret;
    }
    rtos_mutex_unlock(sdio_dev.pwrctl.bus_wakeup_sema);

    switch (target) {
    case SDIO_BUS_ACTIVE:
        aicwf_sdio_bus_wakeup(sdiodev);
        break;
    case SDIO_BUS_SLEEP:
        aicwf_sdio_bus_sleep_allow(sdiodev);
        break;
    }

    return ret;
}
#endif /* CONFIG_SDIO_BUS_PWRCTRL */

int aicwf_sdio_bus_sleep_enter(struct aic_sdio_dev *sdiodev)
{
    int ret = 0;
    {
        //rtos_mutex_lock(sdio_dev.pwrctl.bus_wakeup_sema, -1);
        ret = sdio_writeb_cmd52(sdiodev->sdio_reg.sleep_reg, 0x10);
        if (!ret)
            AIC_LOG_PRINTF("Write sleep fail!\n");
        //rtos_mutex_unlock(sdio_dev.pwrctl.bus_wakeup_sema);
		AIC_LOG_PRINTF("sleeen\n");
    }
    return ret;
}

int aicwf_sdio_bus_sleep_exit(struct aic_sdio_dev *sdiodev)
{
    int ret = 0;
    int read_retry;
    int write_retry = 20;
    int wakeup_reg_val = 0;


    if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8801 ||
        g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DC ||
        g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DW) {
        wakeup_reg_val = 1;
    } else if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8800D80) {
        wakeup_reg_val = 0x11;
    }
    //AIC_LOG_PRINTF("wakeup\n");
    while(write_retry) {
        ret = sdio_writeb_cmd52(sdiodev->sdio_reg.wakeup_reg, wakeup_reg_val);
        if (ret == false) {
            AIC_LOG_PRINTF("sdio wakeup fail\n");
            ret = -1;
        } else {
            read_retry=10;
            while (read_retry) {
                u8 val;
                ret = sdio_readb_cmd52(sdiodev->sdio_reg.sleep_reg, &val);
                if (ret == false) {
                    AIC_LOG_PRINTF("sdio wakeup read fail\n");
                } else if ((val&0x10) != 0) {
                    //AIC_LOG_PRINTF("val: 0x%x", val);
                    break;
                }
                read_retry--;
                udelay(200);
            }
            if(read_retry != 0)
                break;
        }

        AIC_LOG_PRINTF("write retry:  %d \n",write_retry);
        write_retry--;
        udelay(100);
    }

    return ret;
}

void sdio_release_func2(void)
{
    int ret = 0;
    struct aic_sdio_dev *sdiodev = &sdio_dev;

    AIC_LOG_PRINTF("%s\n", __func__);
    ret = sdio_writeb_cmd52_func2(sdiodev->sdio_reg.intr_config_reg, 0x0);
    if (ret < 0) {
        AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.intr_config_reg);
    }
    sdio_card_irq_set(FUNC_2, false);
}

int tx_aggr_counter = MAX_AGGR_TXPKT_CNT + DATA_FLOW_CTRL_THRESH;
int aicwf_sdio_flow_ctrl_msg(void)
{
    int ret = -1;
    u8 fc_reg = 0;
    u32 count = 0;
    struct rwnx_hw *rwnx_hw = g_rwnx_hw;

    while (true) {
        struct aic_sdio_dev *sdiodev = &sdio_dev;
        rtos_mutex_lock(sdiodev->bus_txrx, -1);
        ret = sdio_readb_cmd52(sdiodev->sdio_reg.flow_ctrl_reg, &fc_reg);
        rtos_mutex_unlock(sdiodev->bus_txrx);
        if (ret == FALSE) {
            AIC_LOG_PRINTF("%s, reg read failed\n", __func__);
            return -1;
        }
        if (rwnx_hw->chipid != PRODUCT_ID_AIC8800D80)
            fc_reg = fc_reg & SDIOWIFI_FLOWCTRL_MASK_REG;

        if (fc_reg != 0) {
            ret = fc_reg;
            if (ret > tx_aggr_counter) {
                ret = tx_aggr_counter;
            }
            return ret;
        } else {
            if (count >= FLOW_CTRL_RETRY_COUNT) {
                ret = -fc_reg;
                AIC_LOG_PRINTF("msg fc:%d\n", ret);
                break;
            }
            count++;
            if (count < 30)
                udelay(200);
            else if(count < 40)
                rtos_msleep(2);
            else
                rtos_msleep(10);
        }
    }

    return ret;
}

int aicwf_sdio_flow_ctrl(void)
{
    int ret = -1;
    u8 fc_reg = 0;
    u32 count = 0;
    struct rwnx_hw *rwnx_hw = g_rwnx_hw;

    while (true) {
        struct aic_sdio_dev *sdiodev = &sdio_dev;
        rtos_mutex_lock(sdiodev->bus_txrx, -1);
        ret = sdio_readb_cmd52(sdiodev->sdio_reg.flow_ctrl_reg, &fc_reg);
        rtos_mutex_unlock(sdiodev->bus_txrx);
        if (ret == FALSE) {
            AIC_LOG_PRINTF("%s, reg read failed\n", __func__);
            return -1;
        }
        if (rwnx_hw->chipid != PRODUCT_ID_AIC8800D80)
            fc_reg = fc_reg & SDIOWIFI_FLOWCTRL_MASK_REG;

        if (fc_reg > DATA_FLOW_CTRL_THRESH) {
            ret = fc_reg;
            if (ret > tx_aggr_counter) {
                ret = tx_aggr_counter;
            }
            return ret;
        } else {
            if (count >= FLOW_CTRL_RETRY_COUNT) {
                ret = -fc_reg;
                AIC_LOG_PRINTF("data fc:%d\n", ret);
                break;
            }
            count++;
            if (count < 5)
                udelay(200);
            else if(count < 30)
                rtos_msleep(1);
            else if(count < 40)
                rtos_msleep(2);
            else
                rtos_msleep(10);
        }
    }

    return ret;
}

int aicwf_sdio_send_msg(u8 *buf, uint count)
{
    int ret = 0;
    struct aic_sdio_dev *sdiodev = &sdio_dev;
    rtos_mutex_lock(sdiodev->bus_txrx, -1);
    if (!func_flag_tx){
        sdio_claim_host(sdio_card);
        ret = sdio_writesb(sdio_function[FUNC_1]->card, sdio_function[FUNC_1]->num, sdiodev->sdio_reg.wr_fifo_addr, buf, count);
        sdio_release_host(sdio_card);
    } else {
        sdio_claim_host(sdio_function[FUNC_2]->card);
        ret = sdio_writesb(sdio_function[FUNC_2]->card, sdio_function[FUNC_2]->num, sdiodev->sdio_reg.wr_fifo_addr, buf, count);
        sdio_release_host(sdio_function[FUNC_2]->card);
    }
    rtos_mutex_unlock(sdiodev->bus_txrx);
    return ret;
}

int aicwf_sdio_send_pkt(u8 *buf, uint count)
{
    int ret = 0;
    struct aic_sdio_dev *sdiodev = &sdio_dev;
    rtos_mutex_lock(sdiodev->bus_txrx, -1);
    sdio_claim_host(sdio_card);
    if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8800D80)
        ret = sdio_writesb(sdio_function[FUNC_1]->card, sdio_function[FUNC_1]->num, sdiodev->sdio_reg.wr_fifo_addr, buf, count);
    else
        ret = sdio_writesb(sdio_function[FUNC_1]->card, sdio_function[FUNC_1]->num, sdiodev->sdio_reg.wr_fifo_addr, buf, count);

    sdio_release_host(sdio_card);
    rtos_mutex_unlock(sdiodev->bus_txrx);
    return ret;
}

int aicwf_sdio_recv_pkt(u8 *buf, u32 size, u8 msg)
{
    int ret = -1;

    if ((!buf) || (!size)) {
        return -EINVAL;;
    }

    struct aic_sdio_dev *sdiodev = &sdio_dev;
    rtos_mutex_lock(sdiodev->bus_txrx, -1);
    if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8801) {
        sdio_claim_host(sdio_card);
        ret = sdio_readsb(sdio_function[FUNC_1]->card, sdio_function[FUNC_1]->num, buf, sdiodev->sdio_reg.rd_fifo_addr, size);
        sdio_release_host(sdio_card);
    } else if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DC || g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DW) {
        if (!func_flag_rx) {
            sdio_claim_host(sdio_card);
            ret = sdio_readsb(sdio_function[FUNC_1]->card, sdio_function[FUNC_1]->num, buf, sdiodev->sdio_reg.rd_fifo_addr,
size);
            sdio_release_host(sdio_card);
        } else {
            if(!msg) {
                sdio_claim_host(sdio_card);
                ret = sdio_readsb(sdio_function[FUNC_1]->card, sdio_function[FUNC_1]->num, buf, sdiodev->sdio_reg.rd_fifo_addr
, size);
                sdio_release_host(sdio_card);
            } else {
                sdio_claim_host(sdio_function[FUNC_2]->card);
                ret = sdio_readsb(sdio_function[FUNC_2]->card, sdio_function[FUNC_2]->num, buf, sdiodev->sdio_reg.rd_fifo_addr
, size);
                sdio_release_host(sdio_function[FUNC_2]->card);
            }
        }
    } else if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8800D80) {
        sdio_claim_host(sdio_card);
        ret = sdio_readsb(sdio_function[FUNC_1]->card, sdio_function[FUNC_1]->num, buf, sdiodev->sdio_reg.rd_fifo_addr, size
);
        sdio_release_host(sdio_card);
    }
    rtos_mutex_unlock(sdiodev->bus_txrx);

    return ret;
}

int aicwf_sdio_tx_msg(u8 *buf, uint count, u8 *out)
{
    int err = 0;
    u16 len;
    u8 *payload = buf;
    u16 payload_len = (u16)count;
    u8 adjust_str[4] = {0, 0, 0, 0};
    int adjust_len = 0;
    int buffer_cnt = 0;
    u8 retry = 0;
    struct aic_sdio_dev *sdiodev = &sdio_dev;
    len = payload_len;
    if ((len % TX_ALIGNMENT) != 0) {
        adjust_len = (len + (TX_ALIGNMENT - 1)) & ~(TX_ALIGNMENT - 1); //roundup(len, TX_ALIGNMENT);
        memcpy(payload+payload_len, adjust_str, (adjust_len - len));
        payload_len += (adjust_len - len);
    }
    len = payload_len;

    //link tail is necessary
    if (len % SDIOWIFI_FUNC_BLOCKSIZE != 0) {
        memset(payload+payload_len, 0, TAIL_LEN);
        payload_len += TAIL_LEN;
        len = (payload_len/SDIOWIFI_FUNC_BLOCKSIZE + 1) * SDIOWIFI_FUNC_BLOCKSIZE;
    } else
        len = payload_len;

    if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8801 || g_rwnx_hw->chipid == PRODUCT_ID_AIC8800D80) {
        buffer_cnt = aicwf_sdio_flow_ctrl_msg();
        while ((buffer_cnt <= 0 || (buffer_cnt > 0 && len > (buffer_cnt * BUFFER_SIZE))) && retry < 10) {
            retry++;
            buffer_cnt = aicwf_sdio_flow_ctrl_msg();
        }
    } else if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DC || g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DW) {
        if (!func_flag_tx) {
            buffer_cnt = aicwf_sdio_flow_ctrl_msg();
            while ((buffer_cnt <= 0 || (buffer_cnt > 0 && len > (buffer_cnt * BUFFER_SIZE))) && retry < 10) {
                retry++;
                buffer_cnt = aicwf_sdio_flow_ctrl_msg();
            }
        }
    }

    if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8801) {
        if (buffer_cnt > 0 && len <= (buffer_cnt * BUFFER_SIZE)) {
            //AIC_LOG_PRINTF("aicwf_sdio_send_pkt, len=%d\n", len);
            err = aicwf_sdio_send_pkt(payload, len);
            if (err) {
                AIC_LOG_PRINTF("aicwf_sdio_send_pkt fail%d\n", err);
                goto txmsg_exit;
            }
            if (msgcfm_poll_en && out) {
                u8 intstatus = 0;
                u32 data_len;
                int ret, idx;
                udelay(100);
                for (idx = 0; idx < 8; idx++) {
                    rtos_mutex_lock(sdiodev->bus_txrx, -1);
                    ret = sdio_readb_cmd52(sdiodev->sdio_reg.block_cnt_reg, &intstatus);
                    rtos_mutex_unlock(sdiodev->bus_txrx);
                    while ((ret == FALSE) || (intstatus & SDIO_OTHER_INTERRUPT)) {
                        AIC_LOG_PRINTF("ret=%d, intstatus=%x\n",ret, intstatus);
                        rtos_mutex_lock(sdiodev->bus_txrx, -1);
                        ret = sdio_readb_cmd52(sdiodev->sdio_reg.block_cnt_reg, &intstatus);
                        rtos_mutex_unlock(sdiodev->bus_txrx);
                    }
                    AIC_LOG_PRINTF("[%d] intstatus=%d\n", idx, intstatus);
                    if (intstatus > 0) {
                        if (intstatus < 64) {
                            data_len = intstatus * SDIOWIFI_FUNC_BLOCKSIZE;
                        } else {
                            u8 byte_len = 0;
                            rtos_mutex_lock(sdiodev->bus_txrx, -1);
                            ret = sdio_readb_cmd52(sdiodev->sdio_reg.bytemode_len_reg, &byte_len);
                            rtos_mutex_unlock(sdiodev->bus_txrx);
                            if (ret == FALSE) {
                                AIC_LOG_PRINTF("byte mode len read err %d\r\n", ret);
                                err = ret;
                                goto txmsg_exit;
                            }
                            AIC_LOG_PRINTF("byte mode len=%d\r\n", byte_len);
                            data_len = byte_len * 4; //byte_len must<= 128
                        }
                        if (data_len) {
                            ret = aicwf_sdio_recv_pkt(out, data_len, 0);
                            if (ret) {
                                AIC_LOG_PRINTF("recv pkt err %d\r\n", ret);
                                err = ret;
                                goto txmsg_exit;
                            }
                            AIC_LOG_PRINTF("recv pkt done len=%d\r\n", data_len);
                        }
                        break;
                    }
                }
            }
        } else {
            AIC_LOG_PRINTF("tx msg fc retry fail\n");
            //up(&sdiodev->tx_priv->cmd_txsema);
            #ifdef CONFIG_SDIO_BUS_PWRCTRL
            sdio_dev.cmd_txstate = false;
            #endif  /* CONFIG_SDIO_BUS_PWRCTRL */
            return -1;
        }
    } else if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DC || g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DW) {
        if (((!func_flag_tx) && (buffer_cnt > 0 && len <= (buffer_cnt * BUFFER_SIZE))) || func_flag_tx) {
            //AIC_LOG_PRINTF("aicwf_sdio_send_pkt, len=%d\n", len);
            err = aicwf_sdio_send_msg(payload, len);
            if (err) {
                AIC_LOG_PRINTF("aicwf_sdio_send_pkt fail%d\n", err);
                goto txmsg_exit;
            }
            if (msgcfm_poll_en && out) {
                u8 intstatus = 0;
                u32 data_len;
                int ret, idx;
                udelay(100);
                for (idx = 0; idx < 8; idx++) {
                    rtos_mutex_lock(sdiodev->bus_txrx, -1);
                    ret = sdio_readb_cmd52_func2(sdiodev->sdio_reg.block_cnt_reg, &intstatus);
                    rtos_mutex_unlock(sdiodev->bus_txrx);
                    while ((ret == FALSE) || (intstatus & SDIO_OTHER_INTERRUPT)) {
                        AIC_LOG_PRINTF("ret=%d, intstatus=%x\n",ret, intstatus);
                        rtos_mutex_lock(sdiodev->bus_txrx, -1);
                        ret = sdio_readb_cmd52_func2(sdiodev->sdio_reg.block_cnt_reg, &intstatus);
                        rtos_mutex_unlock(sdiodev->bus_txrx);
                    }
                    AIC_LOG_PRINTF("[%d] intstatus=%d\n", idx, intstatus);
                    if (intstatus > 0) {
                        if (intstatus < 64) {
                            data_len = intstatus * SDIOWIFI_FUNC_BLOCKSIZE;
                        } else {
                            u8 byte_len = 0;
                            rtos_mutex_lock(sdiodev->bus_txrx, -1);
                            ret = sdio_readb_cmd52_func2(sdiodev->sdio_reg.bytemode_len_reg, &byte_len);
                            rtos_mutex_unlock(sdiodev->bus_txrx);
                            if (ret == FALSE) {
                                AIC_LOG_PRINTF("byte mode len read err %d\r\n", ret);
                                err = ret;
                                goto txmsg_exit;
                            }
                            AIC_LOG_PRINTF("byte mode len=%d\r\n", byte_len);
                            data_len = byte_len * 4; //byte_len must<= 128
                        }
                        if (data_len) {
                            ret = aicwf_sdio_recv_pkt(out, data_len, 1);
                            if (ret) {
                                AIC_LOG_PRINTF("recv pkt err %d\r\n", ret);
                                err = ret;
                                goto txmsg_exit;
                            }
                            AIC_LOG_PRINTF("recv pkt done len=%d\r\n", data_len);
                        }
                        break;
                    }
                }
            }
        } else {
            AIC_LOG_PRINTF("tx msg fc retry fail\n");
            //up(&sdiodev->tx_priv->cmd_txsema);
            #ifdef CONFIG_SDIO_BUS_PWRCTRL
            sdio_dev.cmd_txstate = false;
            #endif  /* CONFIG_SDIO_BUS_PWRCTRL */
            return -1;
        }
    }else if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8800D80) {
        if (buffer_cnt > 0 && len <= (buffer_cnt * BUFFER_SIZE)) {
            //AIC_LOG_PRINTF("aicwf_sdio_send_pkt, len=%d\n", len);
            err = aicwf_sdio_send_pkt(payload, len);
            if (err) {
                AIC_LOG_PRINTF("aicwf_sdio_send_pkt fail%d\n", err);
                goto txmsg_exit;
            }
            if (msgcfm_poll_en && out) {
                u8 intstatus = 0;
                u32 data_len;
                int ret, idx;
                udelay(100);
                for (idx = 0; idx < 8; idx++) {
                        rtos_mutex_lock(sdiodev->bus_txrx, -1);
                    ret = sdio_readb_cmd52(sdiodev->sdio_reg.misc_int_status_reg, &intstatus);
                    rtos_mutex_unlock(sdiodev->bus_txrx);
                    while (ret == FALSE) {
                        AIC_LOG_PRINTF("ret=%d, intstatus=%x\n",ret, intstatus);
                        rtos_mutex_lock(sdiodev->bus_txrx, -1);
                        ret = sdio_readb_cmd52(sdiodev->sdio_reg.misc_int_status_reg, &intstatus);
                        rtos_mutex_unlock(sdiodev->bus_txrx);
            			}
					if (intstatus & SDIO_OTHER_INTERRUPT) {
            			u8 int_pending;
                        rtos_mutex_lock(sdiodev->bus_txrx, -1);
                        ret = sdio_readb_cmd52(sdiodev->sdio_reg.sleep_reg, &int_pending);
                        rtos_mutex_unlock(sdiodev->bus_txrx);
            			if (ret == FALSE) {
                			AIC_LOG_PRINTF("reg:%d read failed!\n", sdiodev->sdio_reg.sleep_reg);
            			}
            			int_pending &= ~0x01; // dev to host soft irq
                        rtos_mutex_lock(sdiodev->bus_txrx, -1);
            			ret = sdio_writeb_cmd52(sdiodev->sdio_reg.sleep_reg, int_pending);
                        rtos_mutex_unlock(sdiodev->bus_txrx);
            			if (ret == FALSE) {
                			AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.sleep_reg);
            			}
        			}
                    AIC_LOG_PRINTF("[%d] intstatus=%d\n", idx, intstatus);
					if (intstatus > 0) {
                        uint8_t intmaskf2 = intstatus | (0x1UL << 3);
                        if (intstatus == 120U) {    // byte mode
                            u8 byte_len = 0;
                            rtos_mutex_lock(sdiodev->bus_txrx, -1);
                            ret = sdio_readb_cmd52(sdiodev->sdio_reg.bytemode_len_reg, &byte_len);
                            rtos_mutex_unlock(sdiodev->bus_txrx);
                            if (ret) {
                                AIC_LOG_PRINTF("byte mode len read err %d\r\n", ret);
                            }
                            AIC_LOG_PRINTF("byte mode len=%d\r\n", byte_len);
                            data_len = byte_len * 4; //byte_len must<= 128
                        } else {
                            data_len = (intstatus & 0x7FU) * SDIOWIFI_FUNC_BLOCKSIZE;
                        }
                        if (data_len) {
                            ret = aicwf_sdio_recv_pkt(out, data_len, 0);
                            if (ret) {
                                AIC_LOG_PRINTF("recv pkt err %d\r\n", ret);
                                err = ret;
                                goto txmsg_exit;
                            }
                            AIC_LOG_PRINTF("recv pkt done len=%d\r\n", data_len);
                        }
                        //break;
                    }
                }
           }
        } else {
            AIC_LOG_PRINTF("tx msg fc retry fail\n");
            //up(&sdiodev->tx_priv->cmd_txsema);
            #ifdef CONFIG_SDIO_BUS_PWRCTRL
            sdio_dev.cmd_txstate = false;
            #endif  /* CONFIG_SDIO_BUS_PWRCTRL */
            return -1;
        }
    }

txmsg_exit:
#ifdef CONFIG_SDIO_BUS_PWRCTRL
    sdio_dev.cmd_txstate = false;
#endif  /* CONFIG_SDIO_BUS_PWRCTRL */
    return err;
}

int aicwf_sdio_aggr(struct aic_sdio_dev *sdiodev, u8 *pkt_data, u32 pkt_len);
void aicwf_sdio_aggr_send(struct aic_sdio_dev *sdiodev);
void aicwf_sdio_aggrbuf_reset(struct aic_sdio_dev *sdiodev);

int aicwf_sdio_send_check(void)
{
    struct aic_sdio_dev *sdiodev = &sdio_dev;
    u32 aggr_len = 0;

    aggr_len = (sdiodev->tail - sdiodev->head);
    if(((sdiodev->aggr_count == 0) && (aggr_len != 0))
        || ((sdiodev->aggr_count != 0) && (aggr_len == 0))) {
        if (aggr_len > 0)
            aicwf_sdio_aggrbuf_reset(sdiodev);
        AIC_LOG_PRINTF("aggr_count=%d, aggr_len=%d, check fail\n", sdiodev->aggr_count, aggr_len);
        return -1;
    }

    if (sdiodev->aggr_count == (sdiodev->fw_avail_bufcnt - DATA_FLOW_CTRL_THRESH)) {
        if (sdiodev->aggr_count > 0) {
            sdiodev->fw_avail_bufcnt -= sdiodev->aggr_count;
            aic_dbg("cnt equals\n");
            aicwf_sdio_aggr_send(sdiodev); //send and check the next pkt;
            return 1;
        }
    }

    return 0;
}

#ifdef CONFIG_TX_NOCOPY
int aicwf_sdio_aggr_pbuf(struct aic_sdio_dev *sdiodev, struct pbuf *pkt_data, u32 pkt_len)
{
    struct pbuf *q;
    uint32_t offset = 0;
    u8 *start_ptr = sdiodev->tail;
    u8 sdio_header[4];
    u8 adjust_str[4] = {0, 0, 0, 0};
    u32 curr_len = 0;
    int allign_len = 0;
    u32 aggr_len = sdiodev->len + ((pkt_len + sizeof(sdio_header) + (TX_ALIGNMENT - 1)) & (~(TX_ALIGNMENT - 1)));
    if ((aggr_len % TXPKT_BLOCKSIZE) != 0) {
        aggr_len += TAIL_LEN;
    }

    if (aggr_len > MAX_AGGR_TXPKT_LEN) {
        AIC_LOG_PRINTF("sdio aggr overflow:%d/%d/%d/%d\n", sdiodev->len, pkt_len, aggr_len, (u32)(sdiodev->aggr_buf_align - sdiodev->aggr_buf));
        sdiodev->aggr_end = 1;
        return -1;
    }
    #if 1
    sdio_header[0] =((pkt_len) & 0xff);
    sdio_header[1] =(((pkt_len) >> 8)&0x0f);
    sdio_header[2] = 0x01; //data
    if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8800D80)
		sdio_header[3] = crc8_ponl_107(&sdio_header[0], 3); // crc8
    else
		sdio_header[3] = 0; //reserved

    memcpy(sdiodev->tail, (u8 *)&sdio_header, sizeof(sdio_header));
    sdiodev->tail += sizeof(sdio_header);
    // hostdesc + payload
    // memcpy(sdiodev->tail, (u8 *)pkt_data, pkt_len);/////////////////
    // sdiodev->tail += pkt_len;

    for (q = pkt_data; q != NULL; q = q->next) {
        // uint32_t t1 = rt_tick_get();
        memcpy((void *)(sdiodev->tail), (void *)q->payload, q->len);
        //rt_kprintf("memcpy tick %d[%d]\r\n", (rt_tick_get() - t1), q->len);
        sdiodev->tail += q->len;
    }
    // TODO: check tot_len
    #endif

    //word alignment
    curr_len = sdiodev->tail - sdiodev->head;
    if (curr_len & (TX_ALIGNMENT - 1)) {
        allign_len = TX_ALIGNMENT - (curr_len & (TX_ALIGNMENT - 1));
        memcpy(sdiodev->tail, adjust_str, allign_len);
        sdiodev->tail += allign_len;
    }

    if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8801 || g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DC ||
        g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DW) {
        start_ptr[0] = ((sdiodev->tail - start_ptr - 4) & 0xff);
        start_ptr[1] = (((sdiodev->tail - start_ptr - 4)>>8) & 0x0f);
    }

    sdiodev->len = sdiodev->tail - sdiodev->head;
    if (((sdiodev->len + (TXPKT_BLOCKSIZE - 1)) & ~(TXPKT_BLOCKSIZE - 1)) > MAX_AGGR_TXPKT_LEN) {
		printf("over MAX_AGGR_TXPKT_LEN\n");
        sdiodev->aggr_end = 1;
    }
    sdiodev->aggr_count++;
    if (sdiodev->aggr_count == sdiodev->fw_avail_bufcnt - DATA_FLOW_CTRL_THRESH) {
        sdiodev->aggr_end = 1;
    }

    return 0;
}
int aicwf_sdio_send_pbuf(struct pbuf *pkt_data, u32 pkt_len, bool last)
{
    struct sk_buff *pkt;
    struct aic_sdio_dev *sdiodev = &sdio_dev;
#if 0
    int retry_times = 0;
    int max_retry_times = 5;
    if (sdiodev->fw_avail_bufcnt <= 2) { // init val
        sdiodev->fw_avail_bufcnt = aicwf_sdio_flow_ctrl();
        while(sdiodev->fw_avail_bufcnt <=2 && retry_times < max_retry_times) {
            retry_times++;
            sdiodev->fw_avail_bufcnt = aicwf_sdio_flow_ctrl();
        }
        if(sdiodev->fw_avail_bufcnt <= 2) {
            AIC_LOG_PRINTF("fc retry %d fail\n", sdiodev->fw_avail_bufcnt);
            goto done;
        }
    }
    aic_dbg("ava_cnt=%d,last=%d\n",sdiodev->fw_avail_bufcnt,last);
#endif
retry:
    #if 1
    if(sdiodev==NULL || sdiodev->tail==NULL)
        AIC_LOG_PRINTF("null error\n");
    if (aicwf_sdio_aggr_pbuf(sdiodev, pkt_data, pkt_len)) {
        if (sdiodev->aggr_end) {
            sdiodev->fw_avail_bufcnt -= sdiodev->aggr_count;
            aicwf_sdio_aggr_send(sdiodev);
            goto retry;
        } else {
            aicwf_sdio_aggrbuf_reset(sdiodev);
            AIC_LOG_PRINTF("add aggr pkts failed!\n");
            goto done;
        }
    }

    //when aggr finish or there is cmd to send, just send this aggr pkt to fw
    //if ((int)atomic_read(&sdiodev->tx_priv->tx_pktcnt) == 0 || sdiodev->tx_priv->cmd_txstate) { //no more pkt send 11it!
    if (last || sdiodev->aggr_end) {
        sdiodev->fw_avail_bufcnt -= sdiodev->aggr_count;
        aicwf_sdio_aggr_send(sdiodev);
    } else {
        goto done;
    }
    #endif

done:
    return 0;
}
#endif
int aicwf_sdio_send(u8 *pkt_data, u32 pkt_len, bool last)
{
    struct sk_buff *pkt;
    struct aic_sdio_dev *sdiodev = &sdio_dev;
    int retry_times = 0;
    int max_retry_times = 5;

#if 0
    if (sdiodev->fw_avail_bufcnt <= 2) { // init val
        sdiodev->fw_avail_bufcnt = aicwf_sdio_flow_ctrl();
        while(sdiodev->fw_avail_bufcnt <=2 && retry_times < max_retry_times) {
            retry_times++;
            sdiodev->fw_avail_bufcnt = aicwf_sdio_flow_ctrl();
        }
        if(sdiodev->fw_avail_bufcnt <= 2) {
            AIC_LOG_PRINTF("fc retry %d fail\n", sdiodev->fw_avail_bufcnt);
            goto done;
        }
    }
    aic_dbg("ava_cnt=%d,last=%d\n",sdiodev->fw_avail_bufcnt,last);
#endif
retry:
    #if 1
    if(sdiodev==NULL || sdiodev->tail==NULL)
        AIC_LOG_PRINTF("null error\n");
    if (aicwf_sdio_aggr(sdiodev, pkt_data, pkt_len)) {
        if (sdiodev->aggr_end) {
            sdiodev->fw_avail_bufcnt -= sdiodev->aggr_count;
            aicwf_sdio_aggr_send(sdiodev);
            goto retry;
        } else {
            aicwf_sdio_aggrbuf_reset(sdiodev);
            AIC_LOG_PRINTF("add aggr pkts failed!\n");
            goto done;
        }
    }

    //when aggr finish or there is cmd to send, just send this aggr pkt to fw
    //if ((int)atomic_read(&sdiodev->tx_priv->tx_pktcnt) == 0 || sdiodev->tx_priv->cmd_txstate) { //no more pkt send it!
    if (last || sdiodev->aggr_end) {
        sdiodev->fw_avail_bufcnt -= sdiodev->aggr_count;
        aicwf_sdio_aggr_send(sdiodev);
    } else {
        goto done;
    }
    #else
    aicwf_sdio_aggr(sdiodev, pkt_data, pkt_len);
    aicwf_sdio_aggr_send(sdiodev);
    #endif

done:
    return 0;
}

uint8_t crc8_ponl_107(uint8_t *p_buffer, uint16_t cal_size)
{
    uint8_t i;
    uint8_t crc = 0;
    if (cal_size==0) {
        return crc;
    }
    while (cal_size--) {
        for (i = 0x80; i > 0; i /= 2) {
            if (crc & 0x80)  {
                crc *= 2;
                crc ^= 0x07; //polynomial X8 + X2 + X + 1,(0x107)
            } else {
                crc *= 2;
            }
            if ((*p_buffer) & i) {
                crc ^= 0x07;
            }
        }
        p_buffer++;
    }
    return crc;
}

int aicwf_sdio_aggr(struct aic_sdio_dev *sdiodev, u8 *pkt_data, u32 pkt_len)
{
    struct rwnx_txhdr *txhdr = (struct rwnx_txhdr *)pkt_data;
    u8 *start_ptr = sdiodev->tail;
    u8 sdio_header[4];
    u8 adjust_str[4] = {0, 0, 0, 0};
    u32 curr_len = 0;
    int allign_len = 0;
    u32 aggr_len = sdiodev->len + ((pkt_len + sizeof(sdio_header) + (TX_ALIGNMENT - 1)) & (~(TX_ALIGNMENT - 1)));
    if ((aggr_len % TXPKT_BLOCKSIZE) != 0) {
        aggr_len += TAIL_LEN;
    }

    if (aggr_len > MAX_AGGR_TXPKT_LEN) {
        AIC_LOG_PRINTF("sdio aggr overflow:%d/%d/%d/%d\n", sdiodev->len, pkt_len, aggr_len, (u32)(sdiodev->aggr_buf_align - sdiodev->aggr_buf));
        sdiodev->aggr_end = 1;
        return -1;
    }
    #if 0
    sdio_header[0] =((pkt_len - sizeof(struct rwnx_txhdr) + sizeof(struct txdesc_api)) & 0xff);
    sdio_header[1] =(((pkt_len - sizeof(struct rwnx_txhdr) + sizeof(struct txdesc_api)) >> 8)&0x0f);
    sdio_header[2] = 0x01; //data
    sdio_header[3] = 0; //reserved

    memcpy(sdiodev->tail, (u8 *)&sdio_header, sizeof(sdio_header));
    sdiodev->tail += sizeof(sdio_header);
    //payload
    memcpy(sdiodev->tail, (u8 *)(long)&txhdr->sw_hdr->desc, sizeof(struct txdesc_api));
    sdiodev->tail += sizeof(struct txdesc_api); //hostdesc
    memcpy(sdiodev->tail, (u8 *)((u8 *)txhdr + txhdr->sw_hdr->headroom), pkt_len-txhdr->sw_hdr->headroom);
    sdiodev->tail += (pkt_len - txhdr->sw_hdr->headroom);
    #else
    sdio_header[0] =((pkt_len) & 0xff);
    sdio_header[1] =(((pkt_len) >> 8)&0x0f);
    sdio_header[2] = 0x01; //data
    if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8800D80)
		sdio_header[3] = crc8_ponl_107(&sdio_header[0], 3); // crc8
    else
		sdio_header[3] = 0; //reserved

    memcpy(sdiodev->tail, (u8 *)&sdio_header, sizeof(sdio_header));
    sdiodev->tail += sizeof(sdio_header);
    // hostdesc + payload
    memcpy(sdiodev->tail, (u8 *)pkt_data, pkt_len);
    sdiodev->tail += pkt_len;
    #endif

    //word alignment
    curr_len = sdiodev->tail - sdiodev->head;
    if (curr_len & (TX_ALIGNMENT - 1)) {
        allign_len = TX_ALIGNMENT - (curr_len & (TX_ALIGNMENT - 1));
        memcpy(sdiodev->tail, adjust_str, allign_len);
        sdiodev->tail += allign_len;
    }

    if (g_rwnx_hw->chipid == PRODUCT_ID_AIC8801 || g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DC ||
        g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DW) {
        start_ptr[0] = ((sdiodev->tail - start_ptr - 4) & 0xff);
        start_ptr[1] = (((sdiodev->tail - start_ptr - 4)>>8) & 0x0f);
    }

    #if 0
    if(!txhdr->sw_hdr->need_cfm) {
        kmem_cache_free(txhdr->sw_hdr->rwnx_vif->rwnx_hw->sw_txhdr_cache, txhdr->sw_hdr);
        skb_pull(pkt, txhdr->sw_hdr->headroom);
        consume_skb(pkt);
    }
    #endif

    sdiodev->len = sdiodev->tail - sdiodev->head;
    if (((sdiodev->len + (TXPKT_BLOCKSIZE - 1)) & ~(TXPKT_BLOCKSIZE - 1)) > MAX_AGGR_TXPKT_LEN) {
		printf("over MAX_AGGR_TXPKT_LEN\n");
        sdiodev->aggr_end = 1;
    }
    sdiodev->aggr_count++;
    if (sdiodev->aggr_count == sdiodev->fw_avail_bufcnt - DATA_FLOW_CTRL_THRESH) {
        sdiodev->aggr_end = 1;
    }

    return 0;
}

void aicwf_sdio_aggr_send(struct aic_sdio_dev *sdiodev)
{
    u8 *tx_buf = sdiodev->aggr_buf_align;
    int ret = 0;
    int curr_len = 0;

    if (sdiodev->aggr_count > 1) {
        //aic_dbg("sdio ac=%d\n",sdiodev->aggr_count);
    }

    //link tail is necessary
    curr_len = sdiodev->tail - sdiodev->head;
    if ((curr_len % TXPKT_BLOCKSIZE) != 0) {
        memset(sdiodev->tail, 0, TAIL_LEN);
        sdiodev->tail += TAIL_LEN;
    }

    sdiodev->len = sdiodev->tail - sdiodev->head;
//print_hex_dump_bytes(tx_buf, sdiodev->len);
    curr_len = (sdiodev->len + SDIOWIFI_FUNC_BLOCKSIZE - 1) / SDIOWIFI_FUNC_BLOCKSIZE * SDIOWIFI_FUNC_BLOCKSIZE;
    ret = aicwf_sdio_send_pkt(tx_buf, curr_len);
    if (ret < 0) {
        AIC_LOG_PRINTF("fail to send aggr pkt!\n");
    }

    aicwf_sdio_aggrbuf_reset(sdiodev);
}

void aicwf_sdio_aggrbuf_reset(struct aic_sdio_dev *sdiodev)
{
    sdiodev->tail = sdiodev->head;
    sdiodev->len = 0;
    sdiodev->aggr_count = 0;
    sdiodev->aggr_end = 0;
}

void aicwf_sdio_tx_init(void)
{
    int ret;
    struct aic_sdio_dev *sdiodev = &sdio_dev;
    sdiodev->aggr_buf = rtos_malloc(MAX_AGGR_TXPKT_LEN + AGGR_TXPKT_ALIGN_SIZE);
    if(!sdiodev->aggr_buf) {
        AIC_LOG_PRINTF("Alloc sdio tx aggr_buf failed!\n");
        return;
    }
    sdiodev->aggr_buf_align = (u8 *)(((uint32_t)sdiodev->aggr_buf + AGGR_TXPKT_ALIGN_SIZE - 1) & ~(
AGGR_TXPKT_ALIGN_SIZE - 1));
    sdiodev->fw_avail_bufcnt = 0;
    //sdiodev->tx_pktcnt = 0;
    sdiodev->head = sdiodev->aggr_buf_align;
    aicwf_sdio_aggrbuf_reset(sdiodev);
    ret = rtos_mutex_create(&sdiodev->bus_txrx, "sdiodev->bus_txrx");
    if (ret) {
        AIC_LOG_PRINTF("Alloc sdio txrx mutex failed, ret=%d\n", ret);
    }
    AIC_LOG_PRINTF("sdio aggr_buf:%p, aggr_buf_align:%p\n", sdiodev->aggr_buf, sdiodev->aggr_buf_align);
}

void aicwf_sdio_tx_deinit(void)
{
    struct aic_sdio_dev *sdiodev = &sdio_dev;
    if (sdiodev->aggr_buf) {
        rtos_free(sdiodev->aggr_buf);
        sdiodev->aggr_buf = NULL;
        sdiodev->aggr_buf_align = NULL;
    }
    aicwf_sdio_aggrbuf_reset(sdiodev);
    rtos_mutex_delete(sdiodev->bus_txrx);
}


#define DRV_Reg32(addr)               (*(volatile INT32U *)(addr))

static __inline int gpio_request_irq(uint32_t gpio_id, uint32_t trig_type, int (* isr)(uint32_t, uint32_t))
{
//    GPIO_SetInterruptSense(gpio_id, trig_type);
    #if 0
    sprd_gpio_int_reg(gpio_id, (GPIO_CALLBACK)isr);
    #endif
    return TRUE;
}

static __inline void gpio_enable_irq(uint32_t gpio_id)
{
//    GPIO_EnableIntCtl(gpio_id);
}

static __inline void gpio_disable_irq(uint32_t gpio_id)
{
//    GPIO_DisableIntCtl(gpio_id);
}

bool sdio_host_enable_isr(bool enable)
{
#ifdef CONFIG_AIC_SDIO_INT_PINNUM
     if(enable)
        gpio_enable_irq(sdio_gpio_num);
     else
        gpio_disable_irq(sdio_gpio_num);
#endif
    return TRUE;
}

bool aic_sdio_set_block_size(unsigned int blksize)
{
    unsigned char blk[2];
    uint8_t in, out;
    int err_ret = 0;

    if ((blksize == 0) || (blksize > 512))
    {
        blksize = SDIOWIFI_FUNC_BLOCKSIZE;
    }
    #if 0
    blk[0] = blksize & 0x0ff;
    blk[1] = (blksize >> 8)&0x0ff;

    ////sdio_claim_host(sdio_function[FUNC_0]);
    //sdio_writeb(sdio_function[FUNC_0], blk[0], SDIOWIFI_FBR_FUNC1_BLK_SIZE_REG, &err_ret);
    sdio_f0_writeb(func, blk[0], SDIOWIFI_FBR_FUNC1_BLK_SIZE_REG, &err_ret);
    if(err_ret != TRUE){
        AIC_LOG_PRINTF("aic_sdio_set_block_size fail 0!\n");
        ////sdio_release_host(sdio_function[FUNC_0]);
        return FALSE;
    }

    //sdio_writeb(sdio_function[FUNC_0], blk[1], SDIOWIFI_FBR_FUNC1_BLK_SIZE_REG + 1, &err_ret);
    sdio_f0_writeb(func, blk[1], SDIOWIFI_FBR_FUNC1_BLK_SIZE_REG + 1, &err_ret);
    if(err_ret != TRUE){
        AIC_LOG_PRINTF("aic_sdio_set_block_size fail 1!\n");
        ////sdio_release_host(sdio_function[FUNC_0]);
        return FALSE;
    }

    blk[0] = 0;
    blk[1] = 0;
    //blk[0] = sdio_readb(sdio_function[FUNC_0], SDIOWIFI_FBR_FUNC1_BLK_SIZE_REG, &err_ret);
    blk[0] = sdio_f0_readb(func, SDIOWIFI_FBR_FUNC1_BLK_SIZE_REG, &err_ret);
    if(err_ret != TRUE){
        AIC_LOG_PRINTF("sdio_readb 0x%d fail 0!\n", SDIOWIFI_FBR_FUNC1_BLK_SIZE_REG);
        ////sdio_release_host(sdio_function[FUNC_0]);
        return FALSE;
    }

    //blk[1] = sdio_readb(sdio_function[FUNC_0], SDIOWIFI_FBR_FUNC1_BLK_SIZE_REG + 1, &err_ret);
    blk[1] = sdio_f0_readb(func, SDIOWIFI_FBR_FUNC1_BLK_SIZE_REG + 1, &err_ret);
    if(err_ret != TRUE){
        AIC_LOG_PRINTF("sdio_readb 0x%x fail 1!\n", SDIOWIFI_FBR_FUNC1_BLK_SIZE_REG + 1);
        ////sdio_release_host(sdio_function[FUNC_0]);
        return FALSE;
    }

    ////sdio_release_host(sdio_function[FUNC_0]);

    if (((unsigned int)(blk[1] << 8) | blk[0]) != blksize) {
        AIC_LOG_PRINTF("aic_sdio_set_block_size fail, target=%d, readback=%d!\n", blksize, ((unsigned int)(blk[1] << 8
) | blk[0]));
        return FALSE;
    }
    #endif
    sdio_block_size = blksize;
    return TRUE;
}

bool aic_sdio_set_block_size_func2(unsigned int blksize)
{
    unsigned char blk[2];
    uint8_t in, out;
    int err_ret = 0;

    if ((blksize == 0) || (blksize > 512))
    {
        blksize = SDIOWIFI_FUNC_BLOCKSIZE;
    }
    #if 0
    blk[0] = blksize & 0x0ff;
    blk[1] = (blksize >> 8)&0x0ff;

    //sdio_claim_host(sdio_function[FUNC_0]);
    sdio_writeb(sdio_function[FUNC_0], blk[0], SDIOWIFI_FBR_FUNC2_BLK_SIZE_REG, &err_ret);
    if(err_ret != TRUE){
        AIC_LOG_PRINTF("aic_sdio_set_block_size fail0!\n");
        //sdio_release_host(sdio_function[FUNC_0]);
        return FALSE;
    }

    sdio_writeb(sdio_function[FUNC_0], blk[1], SDIOWIFI_FBR_FUNC2_BLK_SIZE_REG + 1, &err_ret);
    if(err_ret != TRUE){
        AIC_LOG_PRINTF("aic_sdio_set_block_size fail0!\n");
        //sdio_release_host(sdio_function[FUNC_0]);
        return FALSE;
    }

    blk[0] = 0;
    blk[1] = 0;
    blk[0] = sdio_readb(sdio_function[FUNC_0], SDIOWIFI_FBR_FUNC2_BLK_SIZE_REG, &err_ret);
    if(err_ret != TRUE){
        AIC_LOG_PRINTF("sdio_readb 0x%d fail0!\n", SDIOWIFI_FBR_FUNC2_BLK_SIZE_REG);
        //sdio_release_host(sdio_function[FUNC_0]);
        return FALSE;
    }

    blk[1] = sdio_readb(sdio_function[FUNC_0], SDIOWIFI_FBR_FUNC2_BLK_SIZE_REG + 1, &err_ret);
    if(err_ret != TRUE){
        AIC_LOG_PRINTF("sdio_readb 0x%x fail0!\n", SDIOWIFI_FBR_FUNC2_BLK_SIZE_REG + 1);
        //sdio_release_host(sdio_function[FUNC_0]);
        return FALSE;
    }

    //sdio_release_host(sdio_function[FUNC_0]);

    if (((unsigned int)(blk[1] << 8) | blk[0]) != blksize) {
        AIC_LOG_PRINTF("aic_sdio_set_block_size fail, target=%d, readback=%d!\n", blksize, ((unsigned int)(blk[1] << 8
) | blk[0]));
        return FALSE;
    }
    #endif
    sdio_block_size = blksize;
    return TRUE;
}

uint32_t sdio_get_block_size(void)
{
    return sdio_block_size;
}

bool sdio_set_clk(uint32_t clk)
{
    return TRUE;
}

bool sdio_set_bwidth(uint8_t bwidth)
{
    //if(SDIO_BUS_1_BIT == bwidth)
    //    return sdio_set_bus_width(USE_ONE_BUS);

    //if(SDIO_BUS_4_BIT == bwidth)
    //    return sdio_set_bus_width(USE_FOUR_BUS);
    return TRUE;
}

void sdio_host_isr (uint32_t gpio_id, uint32_t gpio_state)
{
    //AIC_LOG_PRINTF("sdio_host_isr gpio_id=%d\n", gpio_id);
    //if (SDHCI_INTR_STS_CARD_INTR & arg)
    {
        #if 0
        sdio_host_enable_isr(FALSE);
        #endif
        #if 0
        if (g_sdio_isr_func)
        {
            g_sdio_isr_func();
        }
        #endif
    }
    rtos_semaphore_signal(sdio_rx_sema, true);
}

#if (FHOST_RX_SW_VER == 3)
void sdio_buf_init(void)
{
    int idx, ret;
    ret = rtos_mutex_create(&sdio_rx_buf_list.mutex, "sdio_rx_buf_list.mutex");
    if (ret) {
        aic_dbg("sdio rx buf mutex create fail: %d\n", ret);
        return;
    }
    co_list_init(&sdio_rx_buf_list.list);
    //if (rtos_semaphore_create(&sdio_rx_buf_list.sdio_rx_node_sema, "sdio_rx_buf_list.sdio_rx_node_sema", SDIO_RX_BUF_COUNT, 0)) {
    //    ASSERT_ERR(0);
    //}
    for (idx = 0; idx < SDIO_RX_BUF_COUNT; idx++) {
        struct sdio_buf_node_s *node = &sdio_rx_buf_node[idx];
        node->buf_raw = &sdio_rx_buf_pool[idx][0];
        node->buf = NULL;
        node->buf_len = 0;
        node->pad_len = 0;
        co_list_push_back(&sdio_rx_buf_list.list, &node->hdr);
        //rtos_semaphore_signal(sdio_rx_buf_list.sdio_rx_node_sema, 0);
    }
    //AIC_LOG_PRINTF("sdio_rx_node_sema initial count:%d\n", rtos_semaphore_get_count(sdio_rx_buf_list.1155sdio_rx_node_sema));
}

struct sdio_buf_node_s *sdio_buf_alloc(uint16_t buf_len)
{
    struct sdio_buf_node_s *node = NULL;
    int ret = 0; //rtos_semaphore_wait(sdio_rx_buf_list.sdio_rx_node_sema, 100);

    if (ret == 0) {
        rtos_mutex_lock(sdio_rx_buf_list.mutex, -1);
        if(co_list_is_empty(&sdio_rx_buf_list.list)) {
            // printf("Wait sdio rx buf\n");
            rtos_mutex_unlock(sdio_rx_buf_list.mutex);
            return node;
        } else
            //AIC_LOG_PRINTF("sdio_rx_buf cnt %d\n", co_list_cnt(&sdio_rx_buf_list.list));
        node = (struct sdio_buf_node_s *)co_list_pop_front(&sdio_rx_buf_list.list);
        if (buf_len > SDIO_RX_BUF_SIZE) {
            uint8_t *buf_raw = rtos_malloc(buf_len + SYS_CACHE_LINE_SIZE);
            if (buf_raw == NULL) {
                AIC_LOG_PRINTF("sdio buf alloc fail(len=%d)!!!\n", buf_len);
                node->buf = NULL;
                node->buf_len = 0;
                node->pad_len = 0;
            } else {
                node->buf = (uint8_t *)WCN_CACHE_ALIGNED(buf_raw);
                node->buf_len = buf_len;
                node->pad_len = node->buf - buf_raw;
            }
        } else {
            node->buf = node->buf_raw;
            node->buf_len = buf_len;
            node->pad_len = 0;
        }
        rtos_mutex_unlock(sdio_rx_buf_list.mutex);
    } else {
        AIC_LOG_PRINTF("sdio_rx_node_sema wait failed\n");
    }
    return node;
}

void sdio_buf_free(struct sdio_buf_node_s *node)
{
    rtos_mutex_lock(sdio_rx_buf_list.mutex, -1);
    if (node->buf_len == 0) {
        AIC_LOG_PRINTF("null sdio buf free, buf=%p\n", node->buf);
    } else if (node->buf_len > SDIO_RX_BUF_SIZE) {
        uint8_t *buf_raw = node->buf - node->pad_len;
        rtos_free(buf_raw);
    }
    node->buf = NULL;
    node->buf_len = 0;
    node->pad_len = 0;
    co_list_push_back(&sdio_rx_buf_list.list, &node->hdr);
    rtos_mutex_unlock(sdio_rx_buf_list.mutex);
    //rtos_semaphore_signal(sdio_rx_buf_list.sdio_rx_node_sema, 0);
}
#endif

void sdio_rx_task(void *argv)
{
    struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)argv; //g_rwnx_hw;
    //printf("sdio_rx_task\n");
    while (1) {
        int polling = 1;

        #ifdef CONFIG_SDIO_BUS_PWRCTRL
        int32_t ret = rtos_semaphore_wait(sdio_rx_sema, 1000);
        #else
        int32_t ret = rtos_semaphore_wait(sdio_rx_sema, 10); // polling to avoid irq missing
        #endif
        //AIC_LOG_PRINTF("aft sdio sema\n");

        if (msgcfm_poll_en == 0) { // process sdio rx in task
    	    u8 intstatus = 0;
            u32 data_len;
            int ret = 0, idx, retry_cnt = 0;
            static uint32_t max_data_len = 0;
		    struct aic_sdio_dev *sdiodev = &sdio_dev;
            rtos_mutex_lock(sdiodev->bus_txrx, -1);
		    if (rwnx_hw->chipid == PRODUCT_ID_AIC8801) {
            ret = sdio_readb_cmd52(sdiodev->sdio_reg.block_cnt_reg, &intstatus);
        	    while ((ret == FALSE) || (intstatus & SDIO_OTHER_INTERRUPT)) {
				    AIC_LOG_PRINTF("ret=%d, intstatus=%x\n",ret, intstatus);
                    ret = sdio_readb_cmd52(sdiodev->sdio_reg.block_cnt_reg, &intstatus);
				    retry_cnt++;
				    if (retry_cnt >= 8) {
                	    break;
				    }
        	    }
    		} else if (rwnx_hw->chipid == PRODUCT_ID_AIC8800DC || rwnx_hw->chipid == PRODUCT_ID_AIC8800DW) {
    			if (func_flag_rx)
                    ret = sdio_readb_cmd52_func2(sdiodev->sdio_reg.block_cnt_reg, &intstatus);
    			else
                    ret = sdio_readb_cmd52(sdiodev->sdio_reg.block_cnt_reg, &intstatus);
    			while ((ret == FALSE) || (intstatus & SDIO_OTHER_INTERRUPT)) {
    				AIC_LOG_PRINTF("ret=%d, intstatus=%x\n",ret, intstatus);
    				if (func_flag_rx)
                        ret = sdio_readb_cmd52_func2(sdiodev->sdio_reg.block_cnt_reg, &intstatus);
    				else
                        ret = sdio_readb_cmd52(sdiodev->sdio_reg.block_cnt_reg, &intstatus);
    				retry_cnt++;
    				if (retry_cnt >= 8) {
    					break;
    				}
    			}
    		} else if (rwnx_hw->chipid == PRODUCT_ID_AIC8800D80) {
                ret = sdio_readb_cmd52(sdiodev->sdio_reg.misc_int_status_reg, &intstatus);
                while (ret == FALSE) {
                    AIC_LOG_PRINTF("ret=%d, intstatus=%x\n",ret, intstatus);
                    ret = sdio_readb_cmd52(sdiodev->sdio_reg.misc_int_status_reg, &intstatus);
                    retry_cnt++;
                    if (retry_cnt >= 8) {
                    	break;
                    }
                }
    			if (intstatus & SDIO_OTHER_INTERRUPT) {
                	u8 int_pending;
                    ret = sdio_readb_cmd52(sdiodev->sdio_reg.sleep_reg, &int_pending);
                	if (ret == FALSE) {
                        AIC_LOG_PRINTF("reg:%d read failed!\n", sdiodev->sdio_reg.sleep_reg);
                	}
                	int_pending &= ~0x01; // dev to host soft irq
                    ret = sdio_writeb_cmd52(sdiodev->sdio_reg.sleep_reg, int_pending);
                	if (ret == FALSE) {
                        AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.sleep_reg);
                	}
    			}
    		}
            rtos_mutex_unlock(sdiodev->bus_txrx);

            //AIC_LOG_PRINTF("[task] intstatus=%d, retry_cnt=%d\n", intstatus, retry_cnt);
            if ((intstatus > 0) && (retry_cnt < 8)) {
				if (rwnx_hw->chipid == PRODUCT_ID_AIC8800D80) {
					uint8_t intmaskf2 = intstatus | (0x1UL << 3);
					u8 byte_len = 0;
                	if (intstatus == 120U) { // byte mode
                    	rtos_mutex_lock(sdiodev->bus_txrx, -1);
                        ret = sdio_readb_cmd52(sdiodev->sdio_reg.bytemode_len_reg, &byte_len);
                    	rtos_mutex_unlock(sdiodev->bus_txrx);
						if (ret == FALSE) {
                            AIC_LOG_PRINTF("byte mode len read err %d\r\n", ret);
                    	}
                    	AIC_LOG_PRINTF("byte mode len=%d\r\n", byte_len);
                    	data_len = byte_len * 4; //byte_len must<= 128
                	} else { // block mode
                    	data_len = (intstatus & 0x7FU) * SDIOWIFI_FUNC_BLOCKSIZE;
                	}
				} else {
					if (intstatus < 64) {
                    	data_len = intstatus * SDIOWIFI_FUNC_BLOCKSIZE;
                	} else {
                    	u8 byte_len = 0;

                    	rtos_mutex_lock(sdiodev->bus_txrx, -1);
                    	if (rwnx_hw->chipid == PRODUCT_ID_AIC8800DC || rwnx_hw->chipid == PRODUCT_ID_AIC8800DW) {
                        	if (func_flag_rx)
                            	ret = sdio_readb_cmd52_func2(sdiodev->sdio_reg.bytemode_len_reg, &byte_len);
                        	else
                            	ret = sdio_readb_cmd52(sdiodev->sdio_reg.bytemode_len_reg, &byte_len);
                    	}
                    	rtos_mutex_unlock(sdiodev->bus_txrx);
                    	if (ret == FALSE) {
                        	AIC_LOG_PRINTF("byte mode len read err %d\r\n", ret);
                    	}
                    	AIC_LOG_PRINTF("byte mode len=%d\r\n", byte_len);
                    	data_len = byte_len * 4; //byte_len must<= 128
                	}
                }
                if (data_len) {
                    if (max_data_len < data_len) {
                        max_data_len = data_len;
                        aic_dbg("max_data_len=%d\n", max_data_len);
                    }
                    #if (FHOST_RX_SW_VER == 3)
                    do {
                        uint8_t *buf_rx;
                        int ret;
                        struct sdio_buf_node_s *node = sdio_buf_alloc(data_len);
                        if ((node == NULL) || (node != NULL && node->buf == NULL)) {
                            //AIC_LOG_PRINTF("node/buf alloc fail(len=%d),node=%p,buf=%p!!!\n", data_len, node, node-buf);
                            if (node) {
                                sdio_buf_free(node);
                                node = NULL;
                            }
                            break;
                        }
                        buf_rx = node->buf;
                        ret = aicwf_sdio_recv_pkt(buf_rx, data_len, 1);
                        if (ret) {
                            AIC_LOG_PRINTF("recv pkt err %d\n", ret);
                            sdio_buf_free(node);
                            break;
                        }
					    #ifdef CONFIG_SDIO_BUS_PWRCTRL
                        rtos_mutex_lock(sdio_dev.rx_sema, -1);
                        sdio_dev.rx_cnt++;
                        //AIC_LOG_PRINTF("%s, rx_cnt: %d", __func__, sdio_dev.rx_cnt);
                        rtos_mutex_unlock(sdio_dev.rx_sema);
                        #endif  /* CONFIG_SDIO_BUS_PWRCTRL */
                        //aic_dbg("enq,%p,%d, node:%p\n",buf_rx, data_len, node);
                        fhost_rxframe_enqueue(node);
                        //printf("enq cnt %d\n", co_list_cnt(&fhost_rx_env.rxq.list));
                        rtos_semaphore_signal(fhost_rx_env.rxq_trigg, 0);
                    } while (0);
                    #endif
                }
            } else if (!polling) {
                AIC_LOG_PRINTF("Interrupt but no data, intstatus=%d, retry_cnt=%d\n", intstatus, retry_cnt);
            }
            sdio_irq_rearm();
        }
        //aic_dbg("#*So\n");
   }
}

bool is_truly_isr(void)
{
    return TRUE;
}

/* card interrupt of the host controller, called in interrupt context */
static void aicwf_sdio_irq_hdl(const struct device *dev, int reason, const void *user_data)
{
    if (sdio_rx_sema != NULL) {
        rtos_semaphore_signal(sdio_rx_sema, true);
    }
}

/* the card interrupt is one-shot in the host controller: arm it again */
static void sdio_irq_rearm(void)
{
    if (sdhc_dev != NULL) {
        sdhc_enable_interrupt(sdhc_dev, aicwf_sdio_irq_hdl, SDHC_INT_SDIO, NULL);
    }
}

/* master and per function interrupt enable of the card (CCCR 0x04) */
static int sdio_card_irq_set(int func, bool enable)
{
    uint8_t ien = 0;
    int ret;

    if (sdio_card == NULL) {
        return -ENODEV;
    }
    ret = sdio_read_byte(&sdio_card->func0, SDIO_CCCR_INT_EN, &ien);
    if (ret != 0) {
        return ret;
    }
    if (enable) {
        ien |= BIT(0) | BIT(func);
    } else {
        ien &= ~BIT(func);
        if ((ien & ~BIT(0)) == 0) {
            ien = 0;
        }
    }

    return sdio_write_byte(&sdio_card->func0, SDIO_CCCR_INT_EN, ien);
}

static int sdio_interrupt_init(void)
{
    int ret;

    ret = sdio_card_irq_set(FUNC_1, true);
    if (ret == 0 && (g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DC || g_rwnx_hw->chipid == PRODUCT_ID_AIC8800DW)) {
        ret = sdio_card_irq_set(FUNC_2, true);
    }
    if (ret != 0) {
        AIC_LOG_PRINTF("sdio_host_init:card irq enable failed %d\n", ret);
        return ret;
    }
    sdio_irq_rearm();

    return 0;
}

#if (CONFIG_TXMSG_TEST_EN)
void aic_txmsg_test(void)
{
    __align(64) static u8 buffer[64] = {0,};
    __align(64) static u8 buffer_rx[512] = {0,};
    int err;
    int len = sizeof(struct lmac_msg) + sizeof(struct dbg_mem_read_req);
    struct dbg_mem_read_cfm rd_mem_addr_cfm;
    struct dbg_mem_read_cfm *cfm = &rd_mem_addr_cfm;
    struct lmac_msg *msg;
    struct dbg_mem_read_req *req;
    int index = 0;
    buffer[0] = (len+4) & 0x00ff;
    buffer[1] = ((len+4) >> 8) &0x0f;
    buffer[2] = 0x11;
    buffer[3] = 0x0;
    index += 4;
    //there is a dummy word
    index += 4;
    msg = (struct lmac_msg *)&buffer[index];
    msg->id = DBG_MEM_READ_REQ;
    msg->dest_id = TASK_DBG;
    msg->src_id = 100;
    msg->param_len = sizeof(struct dbg_mem_read_req);
    req = (struct dbg_mem_read_req *)&msg->param[0];
    req->memaddr = 0x40500000;
    err = aicwf_sdio_tx_msg(buffer, len + 8, buffer_rx);
    if (!err) {
        AIC_LOG_PRINTF("tx msg done\n");
    }
}
#endif

int aic_gpio_ind_init(void)
{
    int err = 0;
    #if 0
    __align(64) static u8 buffer[64] = {0,};
    __align(64) static u8 buffer_rx[512] = {0,};
    int len = sizeof(struct lmac_msg) + sizeof(struct dbg_mem_write_req);
    struct dbg_mem_write_cfm wr_mem_cfm;
    struct dbg_mem_write_cfm *cfm = &wr_mem_cfm;
    struct lmac_msg *msg;
    struct dbg_mem_write_req *req;
    int index = 0;
    buffer[0] = (len+4) & 0x00ff;
    buffer[1] = ((len+4) >> 8) &0x0f;
    buffer[2] = 0x11;
    buffer[3] = 0x0;
    index += 4;
    //there is a dummy word
    index += 4;
    msg = (struct lmac_msg *)&buffer[index];
    msg->id = DBG_MEM_WRITE_REQ;
    msg->dest_id = TASK_DBG;
    msg->src_id = 100;
    msg->param_len = sizeof(struct dbg_mem_write_req);
    req = (struct dbg_mem_write_req *)&msg->param[0];
    #if 1
    do {
        req->memaddr = 0x40500028;
        req->memdata = 0x00000000;
        err = aicwf_sdio_tx_msg(buffer, len + 8, buffer_rx);
        if (err) {
            AIC_LOG_PRINTF("tx msg [0] fail\n");
            break;
        }
        req->memaddr = 0x4050301C;
        req->memdata = 0x00000007;
        err = aicwf_sdio_tx_msg(buffer, len + 8, buffer_rx);
        if (err) {
            AIC_LOG_PRINTF("tx msg [1] fail\n");
            break;
        }
        req->memaddr = 0x40100054;
        req->memdata = 0x00000001;
        err = aicwf_sdio_tx_msg(buffer, len + 8, buffer_rx);
        if (err) {
            AIC_LOG_PRINTF("tx msg [2] fail\n");
            break;
        }
        req->memaddr = 0x4024107C;
        req->memdata = 0x00000001;
        err = aicwf_sdio_tx_msg(buffer, len + 8, buffer_rx);
        if (err) {
            AIC_LOG_PRINTF("tx msg [3] fail\n");
            break;
        }
        req->memaddr = 0x402400F0;
        req->memdata = 0x00340022;
        err = aicwf_sdio_tx_msg(buffer, len + 8, buffer_rx);
        if (err) {
            AIC_LOG_PRINTF("tx msg [4] fail\n");
            break;
        }
    } while (0);
    #else
    int idx, cnt = 0;
    uint32_t (*p_tbl)[2] = NULL;
    // for 8800d
    const uint32_t gpio_cfg_tbl_8800d[][2] = {
        {0x40500028, 0x00000000},
        {0x4050301C, 0x00000007},
        {0x40100054, 0x00000001},
        {0x4024107C, 0x00000001},
        {0x402400F0, 0x00340022},
    };
    // for 8800dc/dw
    const uint32_t gpio_cfg_tbl_8800dcdw[][2] = {
        {0x40500040, 0x00000000},
        {0x4050401C, 0x00000007},
        {0x40100030, 0x00000001},
        {0x40241020, 0x00000001},
        {0x402400F0, 0x00340022},
    };
    if (rwnx_hw->chipid == PRODUCT_ID_AIC8801) {
        p_tbl = gpio_cfg_tbl_8800d;
        cnt = sizeof(gpio_cfg_tbl_8800d) / sizeof(uint32_t) / 2;
    } else if ((rwnx_hw->chipid == PRODUCT_ID_AIC8800DC) || (rwnx_hw->chipid == PRODUCT_ID_AIC8800DW)) {
        p_tbl = gpio_cfg_tbl_8800dcdw;
        cnt = sizeof(gpio_cfg_tbl_8800dcdw) / sizeof(uint32_t) / 2;
    } else {
        AIC_LOG_PRINTF("unsupported chipid\n");
        return -1;
    }
    for (idx = 0; idx < cnt; idx++) {
        req->memaddr = p_tbl[idx][0];
        req->memdata = p_tbl[idx][1];
        err = aicwf_sdio_tx_msg(buffer, len + 8, buffer_rx);
        if (err) {
            AIC_LOG_PRINTF("tx msg [%d] fail\n", idx);
            break;
        }
    }
    #endif
    #endif
    msgcfm_poll_en = 0;
    return err;
}

uint32_t sdio_drv_probe(void) {
    TRACE_IN();
    if (aic_sdio_card_probe() != 0) {
        return -ENODEV;
    }
    if (sdio_find_func(SDIO_ANY_ID, SDIO_ANY_ID, SDIO_ANY_ID) == NULL) {
        AIC_LOG_PRINTF("sdio_drv_probe: get func err!\n");
        return -ENODEV;
    }
    TRACE_OUT();
    return 0;
}
void aicwf_sdio_reg_init(struct rwnx_hw *rwnx_hw, struct aic_sdio_dev *sdiodev)
{
    AIC_LOG_PRINTF("%s\n", __func__);

    if(rwnx_hw->chipid == PRODUCT_ID_AIC8801 || rwnx_hw->chipid == PRODUCT_ID_AIC8800DC ||
       rwnx_hw->chipid == PRODUCT_ID_AIC8800DW){
        sdiodev->sdio_reg.bytemode_len_reg =       SDIOWIFI_BYTEMODE_LEN_REG;
        sdiodev->sdio_reg.intr_config_reg =        SDIOWIFI_INTR_CONFIG_REG;
        sdiodev->sdio_reg.sleep_reg =              SDIOWIFI_SLEEP_REG;
        sdiodev->sdio_reg.wakeup_reg =             SDIOWIFI_WAKEUP_REG;
        sdiodev->sdio_reg.flow_ctrl_reg =          SDIOWIFI_FLOW_CTRL_REG;
        sdiodev->sdio_reg.register_block =         SDIOWIFI_REGISTER_BLOCK;
        sdiodev->sdio_reg.bytemode_enable_reg =    SDIOWIFI_BYTEMODE_ENABLE_REG;
        sdiodev->sdio_reg.block_cnt_reg =          SDIOWIFI_BLOCK_CNT_REG;
        sdiodev->sdio_reg.rd_fifo_addr =           SDIOWIFI_RD_FIFO_ADDR;
        sdiodev->sdio_reg.wr_fifo_addr =           SDIOWIFI_WR_FIFO_ADDR;
	} else if (rwnx_hw->chipid == PRODUCT_ID_AIC8800D80){
        sdiodev->sdio_reg.bytemode_len_reg =       SDIOWIFI_BYTEMODE_LEN_REG_V3;
        sdiodev->sdio_reg.intr_config_reg =        SDIOWIFI_INTR_ENABLE_REG_V3;
        sdiodev->sdio_reg.sleep_reg =              SDIOWIFI_INTR_PENDING_REG_V3;
        sdiodev->sdio_reg.wakeup_reg =             SDIOWIFI_INTR_TO_DEVICE_REG_V3;
        sdiodev->sdio_reg.flow_ctrl_reg =          SDIOWIFI_FLOW_CTRL_Q1_REG_V3;
        sdiodev->sdio_reg.bytemode_enable_reg =    SDIOWIFI_BYTEMODE_ENABLE_REG_V3;
        sdiodev->sdio_reg.misc_int_status_reg =    SDIOWIFI_MISC_INT_STATUS_REG_V3;
        sdiodev->sdio_reg.rd_fifo_addr =           SDIOWIFI_RD_FIFO_ADDR_V3;
        sdiodev->sdio_reg.wr_fifo_addr =           SDIOWIFI_WR_FIFO_ADDR_V3;
    }
}
bool sdio_host_init(struct rwnx_hw *rwnx_hw, void (*sdio_isr)(void))
{
    int32_t ret = 0;
    uint8_t in;
    int err_ret = 0;
    int i =0;
    uint8_t block_bit0 = 0x1;
    uint8_t byte_mode_disable = 0x1;//1: no byte mode

    struct aic_sdio_dev *sdiodev = &sdio_dev;

    /* power the chip and bring up the card on the bus */
    ret = aic_sdio_card_probe();
    if (ret != 0) {
        AIC_LOG_PRINTF("%s: card probe failed (%d)\n", __func__, ret);
        return FALSE;
    }
    aicwf_sdio_tx_init();

    /* Enable Function 1 */
   if (rwnx_hw->chipid == PRODUCT_ID_AIC8800DC || rwnx_hw->chipid == PRODUCT_ID_AIC8800DW) {
		sdio_function[FUNC_0] = sdio_find_func(0x7, ~0, ~0);
		if(!sdio_function[FUNC_0]){
			ret = -ENODEV;
			AIC_LOG_PRINTF("%s: sdio_find_func func0 failed!!(%d)\n", __func__, ret);
			//return FALSE;
		}
        ret = aic_sdio_enable_func(sdio_function[FUNC_0]);
        if (ret < 0) {
            //AIC_LOG_PRINTF("sdio_host_init:enable func0 err! ret is %d, num %d\r\n", ret, sdio_function[FUNC_0]->num);
            //return FALSE;
        }
        sdio_function[FUNC_1] = sdio_find_func(0x7, 0xc8a1, 0xc08d);
		if(!sdio_function[FUNC_1]){
			ret = -ENODEV;
			AIC_LOG_PRINTF("%s: sdio_find_func func1 failed!!(%d)\n", __func__, ret);
			return FALSE;
		}
	} else if (rwnx_hw->chipid == PRODUCT_ID_AIC8800D80) {
		sdio_function[FUNC_1] = sdio_find_func(0x7, 0xc8a1, 0x82);
		if(!sdio_function[FUNC_1]){
			ret = -ENODEV;
			AIC_LOG_PRINTF("%s: sdio_find_func func1 failed!!(%d)\n", __func__, ret);
			return FALSE;
		}
	}

	sdio_card = sdio_function[FUNC_1]->card;
    sdio_claim_host(sdio_card);
    ret = aic_sdio_enable_func(sdio_function[FUNC_1]);
    sdio_release_host(sdio_card);
    if (ret < 0) {
        AIC_LOG_PRINTF("sdio_host_init:enable func1 err! ret is %d", ret);
        return FALSE;
    }
    AIC_LOG_PRINTF("sdio_host_init:enable func1 ok!\n");
#if 0
    u8 sdio_tx_phase = 2;//0:90 2:180
    //sdio phase setting
    ret = sdio_writeb_cmd52(0x13, sdio_tx_phase);
    if (ret){
        AIC_LOG_PRINTF("sdio host init:set sdio rx phase err! ret is sd", ret);
        return FALSE;
    }
#endif
	if (rwnx_hw->chipid == PRODUCT_ID_AIC8800D80) {
		u8 val = 0;
		sdio_f0_writeb(sdio_function[FUNC_1], 0x7F, 0xF2, &ret);
    	if (ret) {
        	AIC_LOG_PRINTF("set fn0 0xF2 fail %d\n", ret);
        	return FALSE;
    	}
    	if (sdio_card->card_speed == SD_TIMING_DDR50) {
        	val = 0x21;//0x1D;//0x5;
    	} else {
        	val = 0x01;//0x19;//0x1;
    	}
    	val |= SDIOCLK_FREE_RUNNING_BIT;
    	sdio_f0_writeb(sdio_function[FUNC_1], val, 0xF0, &ret);
    	if (ret) {
        	AIC_LOG_PRINTF("set iopad ctrl fail %d\n", ret);
        	return FALSE;
    	}
    	sdio_f0_writeb(sdio_function[FUNC_1], 0x0, 0xF8, &ret);
    	if (ret) {
        	AIC_LOG_PRINTF("set iopad delay2 fail %d\n", ret);
        	return FALSE;
    	}
    	sdio_f0_writeb(sdio_function[FUNC_1], 0x80, 0xF1, &ret);
    	if (ret) {
        	AIC_LOG_PRINTF("set iopad delay1 fail %d\n", ret);
        	return FALSE;
    	}
    	udelay(100);
#if 0 //SDIO CLOCK SETTING
		if ((host->ios.clock > 0) && (host->ios.timing != SDIO_SPEED_DDR50)) {
			host->ios.clock = FEATURE_SDIO_CLOCK_V3;
			host->ops->set_ios(host, &host->ios);
			AIC_LOG_PRINTF("Set SDIO Clock %d MHz\n", host->ios.clock/1000000);
		}
#else
//		ret = HAL_SDC_Update_Clk(card->host, FEATURE_SDIO_CLOCK_V3);
//		AIC_LOG_PRINTF("Set SDIO Clock %d MHz\n", FEATURE_SDIO_CLOCK_V3/1000000);
#endif
		AIC_LOG_PRINTF("sdio_host_init:aic_sdio_set_block_size %d\n", SDIOWIFI_FUNC_BLOCKSIZE);
    	aic_sdio_set_block_size(SDIOWIFI_FUNC_BLOCKSIZE);

    	AIC_LOG_PRINTF("sdio_host_init:sdio_set_block_size %d\n", SDIOWIFI_FUNC_BLOCKSIZE);
    	aic_sdio_set_func_block_size(sdio_function[FUNC_1], SDIOWIFI_FUNC_BLOCKSIZE);
    	if (sdio_block_size != SDIOWIFI_FUNC_BLOCKSIZE) {
        	AIC_LOG_PRINTF("sdio_host_init: blksize set failed\n");
        	return FALSE;
    	}

    	//1: no byte mode
    	ret = sdio_writeb_cmd52(sdiodev->sdio_reg.bytemode_enable_reg, byte_mode_disable);
    	if (ret == FALSE) {
        	AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.bytemode_enable_reg);
        	return FALSE;
    	}

		//enable sdio interrupt
		sdio_f0_writeb(sdio_function[FUNC_1], 0x07, 0x04, &ret);
		if (ret) {
    		AIC_LOG_PRINTF("set func0 int en fail %d\n", ret);
        }
		ret = sdio_writeb_cmd52(sdiodev->sdio_reg.intr_config_reg, 0x7);
    	if (ret == FALSE) {
        	AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.intr_config_reg);
        	return FALSE;
    	}
	}else {

		sdio_f0_writeb(sdio_function[FUNC_1], 2, 0x13, &ret);

        AIC_LOG_PRINTF("sdio_host_init:aic_sdio_set_block_size %d\n", SDIOWIFI_FUNC_BLOCKSIZE);
        aic_sdio_set_block_size(SDIOWIFI_FUNC_BLOCKSIZE);

    	aic_sdio_set_func_block_size(sdio_function[FUNC_1], SDIOWIFI_FUNC_BLOCKSIZE);
    	if (sdio_block_size != SDIOWIFI_FUNC_BLOCKSIZE) {
        	AIC_LOG_PRINTF("sdio_host_init: blksize set failed\n");
        	return FALSE;
    	}

    	ret = sdio_writeb_cmd52(sdiodev->sdio_reg.register_block, block_bit0);
    	if (ret == FALSE) {
        	AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.register_block);
        	return FALSE;
    	}

        //1: no byte mode
    ret = sdio_writeb_cmd52(sdiodev->sdio_reg.bytemode_enable_reg, byte_mode_disable);
    if (ret < 0) {
        AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.bytemode_enable_reg);
            return FALSE;
        }

        //enable sdio interrupt
    ret = sdio_writeb_cmd52(sdiodev->sdio_reg.intr_config_reg, 0x7);
        if (ret == FALSE) {
        AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.intr_config_reg);
            return FALSE;
        }
    }

    if (rwnx_hw->chipid == PRODUCT_ID_AIC8800DC || rwnx_hw->chipid == PRODUCT_ID_AIC8800DW) {
#if 1
        /* Enable Function 2 */
        sdio_function[FUNC_2] = sdio_find_func(0x7, 0xc8a1, 0xc18d);
        if(!sdio_function[FUNC_2]){
            ret = -ENODEV;
            AIC_LOG_PRINTF("%s: sdio_find_func func2 failed!!(%d)\n", __func__, ret);
            return FALSE;
        }

        sdio_claim_host(sdio_function[FUNC_2]->card);
        ret = aic_sdio_enable_func(sdio_function[FUNC_2]);
        sdio_release_host(sdio_function[FUNC_2]->card);
        if (ret < 0) {
            AIC_LOG_PRINTF("sdio_host_init:enable func2 err! ret is %d", ret);
            return FALSE;
        }
        AIC_LOG_PRINTF("sdio_host_init:enable fun2 ok!\n");

        AIC_LOG_PRINTF("sdio_host_init:func2:aic_sdio_set_block_size %d\n", SDIOWIFI_FUNC_BLOCKSIZE);
        aic_sdio_set_block_size_func2(SDIOWIFI_FUNC_BLOCKSIZE);
        AIC_LOG_PRINTF("sdio_host_init:func2:sdio_set_block_size %d\n", SDIOWIFI_FUNC_BLOCKSIZE);
        aic_sdio_set_func_block_size(sdio_function[FUNC_2], SDIOWIFI_FUNC_BLOCKSIZE);
        if (sdio_block_size != SDIOWIFI_FUNC_BLOCKSIZE) {
            AIC_LOG_PRINTF("sdio_host_init:func2: blksize set failed\n");
        }

        ret = sdio_writeb_cmd52_func2(sdiodev->sdio_reg.register_block, block_bit0);
        if (ret < 0) {
            AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.register_block);
            return FALSE;
        }

        //1: no byte mode
        ret = sdio_writeb_cmd52_func2(sdiodev->sdio_reg.bytemode_enable_reg, byte_mode_disable);
        if (ret < 0) {
            AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.bytemode_enable_reg);
            return FALSE;
        }

        //enable sdio interrupt
        ret = sdio_writeb_cmd52_func2(sdiodev->sdio_reg.intr_config_reg, 0x7);
        if (ret < 0) {
            AIC_LOG_PRINTF("reg:%d write failed!\n", sdiodev->sdio_reg.intr_config_reg);
            return FALSE;
        }
#endif
    }

    aicwf_sdio_reg_init(rwnx_hw, sdiodev);

    if (sdio_interrupt_init()) {
        AIC_LOG_PRINTF("sdio_host_init: sdio_interrupt_init failed\n");
        return FALSE;
    }
    //sdio_enable_rx_irq();

    #if 0
    /* disable sdio control interupt at first */
    sdio_host_enable_isr(FALSE);
    #endif

    /* install isr here */
    //g_sdio_isr_func = sdio_isr;

    /* prepare for sdio isr */
    if (aic_gpio_ind_init()) {
        AIC_LOG_PRINTF("gpio ind init fail\n");
        return FALSE;
    }

    #if (FHOST_RX_SW_VER == 3)
    sdio_buf_init();
    #endif

#ifdef CONFIG_SDIO_BUS_PWRCTRL
    sdio_dev.rx_cnt = 0;
    sdio_dev.tx_pktcnt = 0;
    sdio_dev.cmd_txstate = false;
    sdio_dev.pwrctl.bus_status = SDIO_BUS_ACTIVE;
    sdio_dev.pwrctl.init_status = false;
    sdio_dev.pwrctl.active_duration = 10;   //

    rtos_mutex_create(&sdio_dev.tx_sema, "tx_sema lock");
    if (sdio_dev.tx_sema == NULL)
    {
        ret = -1; //EREMOTEIO;
        AIC_LOG_PRINTF("sdio tx_sema create fail\n");
        return FALSE;
    }
    rtos_mutex_create(&sdio_dev.rx_sema, "rx_sema lock");
    if (sdio_dev.rx_sema == NULL)
    {
        ret = -1; //EREMOTEIO;
        AIC_LOG_PRINTF("sdio rx_sema create fail\n");
        return FALSE;
    }

    rtos_mutex_create(&sdio_dev.pwrctl.bus_wakeup_sema, "bus_wakeup_sema");
    if (sdio_dev.pwrctl.bus_wakeup_sema == NULL)
    {
        ret = -1; //EREMOTEIO;
        AIC_LOG_PRINTF("sdio bus wakeup sema create fail\n");
        return FALSE;
    }
    rtos_mutex_create(&sdio_dev.pwrctl.pwrctl_lock, "pwrctl_lock");
    if (sdio_dev.pwrctl.pwrctl_lock == NULL)
    {
        ret = -1; //EREMOTEIO;
        AIC_LOG_PRINTF("sdio bus pwrctl_lock sema create fail\n");
        return FALSE;
    }
    rtos_semaphore_create(&sdio_dev.pwrctl.pwrctl_trgg, "pwrctrl_trgg", 8, 0);
    if (sdio_dev.pwrctl.pwrctl_trgg == NULL) {
        ret = -1; //EREMOTEIO;
        AIC_LOG_PRINTF("sdio bus pwrctrl_trgg sema create fail\n");
        return FALSE;
    }
#endif /* CONFIG_SDIO_BUS_PWRCTRL */

    rtos_semaphore_create(&sdio_rx_sema, "sdio_rx_sema", 8, 0);
    if (sdio_rx_sema == NULL) {
        AIC_LOG_PRINTF("sdio rx sema create fail\n");
        return FALSE;
    }

    ret = rtos_task_create(sdio_rx_task, "sdio_rx_task", SDIO_DATRX_TASK,
                           sdio_datrx_stack_size, (void*)rwnx_hw, sdio_datrx_priority,
                           &sdio_task_hdl);
    if (ret || (sdio_task_hdl == NULL)) {
        AIC_LOG_PRINTF("sdio task create fail\n");
        return FALSE;
    }

    #if 0
    /* enable sdio control interupt */
    sdio_host_enable_isr(TRUE);
    #endif

    AIC_LOG_PRINTF("sdio_host_init:host_int ok\n");

    #if (CONFIG_TXMSG_TEST_EN)
    aic_txmsg_test();
    #endif

    return TRUE;
}

bool sdio_host_deinit(struct rwnx_hw *rwnx_hw)
{
    int ret = 0;

    AIC_LOG_PRINTF("sdio_host_deinit\n");

    if (sdhc_dev != NULL) {
        sdhc_disable_interrupt(sdhc_dev, SDHC_INT_SDIO);
    }
    rtos_semaphore_delete(sdio_rx_sema);
    sdio_rx_sema = NULL;

#ifdef CONFIG_SDIO_BUS_PWRCTRL
	RTOS_SEM_DEL_A(sdio_dev.tx_sema);
	RTOS_SEM_DEL_A(sdio_dev.rx_sema);
	RTOS_SEM_DEL_A(sdio_dev.pwrctl.bus_wakeup_sema);
	RTOS_SEM_DEL_A(sdio_dev.pwrctl.pwrctl_trgg);
#endif /* CONFIG_SDIO_BUS_PWRCTRL */

    aicwf_sdio_tx_deinit();
    #if 0
    sdio_host_enable_isr(0);
    #endif
    if (rwnx_hw->chipid == PRODUCT_ID_AIC8800DC || rwnx_hw->chipid == PRODUCT_ID_AIC8800DW) {
        func_flag_tx = true;
        func_flag_rx = true;
    }

    return TRUE;
}

