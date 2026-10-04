/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
 *
 * All Rights Reserved
 */

#ifndef _SDIO_DEF_H_
#define _SDIO_DEF_H_

/* -------------- h/w register ------------------- */
#define SDIOWIFI_FUNC_BLOCKSIZE         512

#define SDIOWIFI_BYTEMODE_LEN_REG       0x02
#define SDIOWIFI_INTR_CONFIG_REG        0x04
#define SDIOWIFI_SLEEP_REG              0x05
#define SDIOWIFI_WAKEUP_REG             0x09
#define SDIOWIFI_FLOW_CTRL_REG          0x0A
#define SDIOWIFI_REGISTER_BLOCK         0x0B
#define SDIOWIFI_BYTEMODE_ENABLE_REG    0x11
#define SDIOWIFI_BLOCK_CNT_REG          0x12
#define SDIOWIFI_FLOWCTRL_MASK_REG      0x7F
#define SDIOWIFI_WR_FIFO_ADDR			    0x07
#define SDIOWIFI_RD_FIFO_ADDR			    0x08

#define SDIOWIFI_INTR_ENABLE_REG_V3         0x00
#define SDIOWIFI_INTR_PENDING_REG_V3        0x01
#define SDIOWIFI_INTR_TO_DEVICE_REG_V3      0x02
#define SDIOWIFI_FLOW_CTRL_Q1_REG_V3        0x03
#define SDIOWIFI_MISC_INT_STATUS_REG_V3     0x04
#define SDIOWIFI_BYTEMODE_LEN_REG_V3        0x05
#define SDIOWIFI_BYTEMODE_LEN_MSB_REG_V3    0x06
#define SDIOWIFI_BYTEMODE_ENABLE_REG_V3     0x07
#define SDIOWIFI_MISC_CTRL_REG_V3           0x08
#define SDIOWIFI_FLOW_CTRL_Q2_REG_V3        0x09
#define SDIOWIFI_CLK_TEST_RESULT_REG_V3     0x0A
#define SDIOWIFI_RD_FIFO_ADDR_V3            0x0F
#define SDIOWIFI_WR_FIFO_ADDR_V3            0x10

#define SDIOWIFI_FBR_FUNC1_BLK_SIZE_REG 0x110
#define SDIOWIFI_FBR_FUNC2_BLK_SIZE_REG 0x210

#define SDIOCLK_FREE_RUNNING_BIT        (1 << 6)
#define FEATURE_SDIO_CLOCK_V3       150000000

#define SDIOWIFI_PWR_CTRL_INTERVAL      30
#define FLOW_CTRL_RETRY_COUNT           50
#define BUFFER_SIZE                     1536
#define TAIL_LEN                        4
#define TXQLEN                          (2048*4)

#define SDIO_BUS_SLEEP                  0
#define SDIO_BUS_ACTIVE                 1

#define TXPKT_BLOCKSIZE                 512
#ifdef CONFIG_TX_NOCOPY
#define MAX_AGGR_TXPKT_CNT              100//28
#else
#define MAX_AGGR_TXPKT_CNT              28
#endif
#define MAX_AGGR_TXPKT_LEN              ((1500 + 44 + 4)*MAX_AGGR_TXPKT_CNT)
#define TX_ALIGNMENT                    4
#define RX_ALIGNMENT                    4
#define AGGR_TXPKT_ALIGN_SIZE           64

#define DATA_FLOW_CTRL_THRESH           2

#define SDIO_OTHER_INTERRUPT            (0x1ul << 7)

typedef enum {
    SDIO_TYPE_DATA         = 0X00,
    SDIO_TYPE_CFG          = 0X10,
    SDIO_TYPE_CFG_CMD_RSP  = 0X11,
    SDIO_TYPE_CFG_DATA_CFM = 0X12
} sdio_type;

#if((RECOVER_ENABLE == 1)&&(RECOVER_MECHANISM == 1))
#define SDIO_REG_INT_MASK		0xfa
#else
#define SDIO_REG_INT_MASK		0xfe
#endif


/* -------------- debug        ------------------- */
#define	_SDIO_DEBUG	    1
#define _SDIO_TRACE		0
#define _SDIO_WARN		1

#define SDIO_DEBUG(fmt, ...)		do { if (_SDIO_DEBUG) AIC_LOG_PRINTF("[DEBUG ] " fmt, ##__VA_ARGS__); } while (0)
#define SDIO_TRACE(fmt, ...)		do { if (_SDIO_TRACE) AIC_LOG_PRINTF("[TRACE ] " fmt, ##__VA_ARGS__); } while (0)
#define SDIO_WARN(fmt, ...)         do { if (_SDIO_WARN)  AIC_LOG_PRINTF("[WARN  ] " fmt, ##__VA_ARGS__); } while (0)
#define SDIO_ERROR(fmt, ...)		{ AIC_LOG_PRINTF("[Error!] %s() : ", __FUNCTION__); AIC_LOG_PRINTF(fmt, ##__VA_ARGS__); }
#define SDIO_FATAL(fmt, ...)		{ AIC_LOG_PRINTF("[Fatal!] %s (#%d) : ", __FILE__, __LINE__); AIC_LOG_PRINTF(fmt, ##__VA_ARGS__); AIC_LOG_PRINTF("program halt!!!\n"); exit(1); }
#define SDIO_FAIL(fmt, ...)		    { AIC_LOG_PRINTF("[Fail! ] %s() : ", __FUNCTION__); AIC_LOG_PRINTF(fmt, ##__VA_ARGS__); }
#define SDIO_FAIL_RET(r, fmt, ...)  { AIC_LOG_PRINTF("[Fail! ] %s() : ", __FUNCTION__); AIC_LOG_PRINTF(fmt, ##__VA_ARGS__); return (r); }

#endif /* _SDIO_DEF_H_ */
