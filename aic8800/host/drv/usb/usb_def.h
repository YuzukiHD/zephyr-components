/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
 *
 * All Rights Reserved
 */

#ifndef _USB_DEF_H_
#define _USB_DEF_H_

/* from aicwf_usb.h start */
/* USB Device ID */
#define USB_VENDOR_ID_AIC                0xA69C

#ifndef CONFIG_USB_BT
#define USB_PRODUCT_ID_AIC               0x8800
#else
#define USB_PRODUCT_ID_AIC               0x8801
#endif

#ifdef CONFIG_REORD_FORWARD_LIST
#define AICWF_USB_RX_URBS               (8)//(200)
#else
#define AICWF_USB_RX_URBS               (25)//(200)
#endif
#define AICWF_USB_TX_URBS               (80)//(100)
#ifdef CONFIG_USB_MSG_IN_EP
#define AICWF_USB_MSG_RX_URBS           (10)
#endif
#define AICWF_USB_TX_LOW_WATER         (AICWF_USB_TX_URBS/4)//(AICWF_USB_TX_URBS/4)//25%
#define AICWF_USB_TX_HIGH_WATER        (AICWF_USB_TX_LOW_WATER*3)//75%
#define AICWF_USB_MAX_PKT_SIZE          (2048)

typedef enum {
    USB_TYPE_DATA         = 0X00,
    USB_TYPE_CFG          = 0X10,
    USB_TYPE_CFG_CMD_RSP  = 0X11,
    USB_TYPE_CFG_DATA_CFM = 0X12,
    USB_TYPE_CFG_PRINT    = 0X13
} usb_type;

enum aicwf_usb_state {
    USB_DOWN_ST,
    USB_UP_ST,
    USB_SLEEP_ST
};
/* from aicwf_usb.h end */


/* from aicwf_txrxif.h start */
#define CMD_BUF_MAX                 1536
#define DATA_BUF_MAX                2048
#define TXPKT_BLOCKSIZE             512
#define MAX_AGGR_TXPKT_LEN          (1536*64)
#define CMD_TX_TIMEOUT              5000
#define TX_ALIGNMENT                4

#define RX_HWHRD_LEN                60 //58->60 word allined
#define CCMP_OR_WEP_INFO            8
#define MAX_RXQLEN                  2000
#define RX_ALIGNMENT                4

#define DEBUG_ERROR_LEVEL           0
#define DEBUG_DEBUG_LEVEL           1
#define DEBUG_INFO_LEVEL            2

#define DBG_LEVEL                   DEBUG_DEBUG_LEVEL

#define txrx_err(fmt, ...)          printk("txrx_err:<%s,%d>: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define sdio_err(fmt, ...)          printk("sdio_err:<%s,%d>: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define usb_err(fmt, ...)           printk("usb_err:<%s,%d>: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#if DBG_LEVEL >= DEBUG_DEBUG_LEVEL
#define txrx_dbg(fmt, ...)          printk("txrx: " fmt, ##__VA_ARGS__)
#define sdio_dbg(fmt, ...)          printk("aicsdio: " fmt, ##__VA_ARGS__)
#ifndef PLATFORM_SUNPLUS_ECOS
#define usb_dbg(fmt, ...)           printk("aicusb: " fmt, ##__VA_ARGS__)
#endif
#else
#define txrx_dbg(fmt, ...)
#define sdio_dbg(fmt, ...)
#define usb_dbg(fmt, ...)
#endif
#if DBG_LEVEL >= DEBUG_INFO_LEVEL
#define txrx_info(fmt, ...)         printk("txrx: " fmt, ##__VA_ARGS__)
#define sdio_info(fmt, ...)         printk("aicsdio: " fmt, ##__VA_ARGS__)
#define usb_info(fmt, ...)          printk("aicusb: " fmt, ##__VA_ARGS__)
#else
#define txrx_info(fmt, ...)
#define sdio_info(fmt, ...)
#ifndef PLATFORM_SUNPLUS_ECOS
#define usb_info(fmt, ...)
#endif
#endif

enum aicwf_bus_state {
    BUS_DOWN_ST,
    BUS_UP_ST
};
/* from aicwf_txrxif.h end */


#endif /* _USB_DEF_H_ */
