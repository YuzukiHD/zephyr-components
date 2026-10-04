/**
 * rwnx_utils.h
 */
#ifndef _RWNX_UTILS_H_
#define _RWNX_UTILS_H_

#include "aic_log.h"

/// Macro defining an invalid VIF index
#define INVALID_VIF_IDX 0xFF
#define INVALID_STA_IDX 0xFF

#define AICWF_RX_REORDER    1
#define AICWF_RWNX_TIMER_EN 1

#define DBG_REORD(...)  do {} while (0)//printk(__VA_ARGS__) 
#define WRN_REORD(...)  do {} while (0)//printk(__VA_ARGS__) 
#define DBG_TIMER(...)  do {} while (0)//printk(__VA_ARGS__)
#define WRN_TIMER(...)  do {} while (0)//printk(__VA_ARGS__)

#define FRAMES_11MBPS_ENABLED   (0)

//typedef uint32_t (*patch_tbl_array_t)[2];

//extern patch_tbl_array_t patch_tbl;
//extern patch_tbl_array_t patch_tbl_func;
//extern uint32_t *patch_func;
//extern uint32_t patch_tbl_size;
//extern uint32_t patch_tbl_func_size;
//extern uint32_t patch_func_size;
extern uint32_t aon_patch_start_addr;
extern uint32_t aon_patch_num;
extern uint32_t patch_store_addr;
extern uint32_t fw_patch_addr;

#define TRACE_NONE(fmt, ...)

#if 0//def CONFIG_RWNX_DBG
#define RWNX_DBG printk
#else
#define RWNX_DBG(a...) do {} while (0)
#endif

#define RWNX_FN_ENTRY_STR ">>> %s()\n", __func__

enum rwnx_dev_flag {
    RWNX_DEV_RESTARTING,
    RWNX_DEV_STACK_RESTARTING,
    RWNX_DEV_STARTED,
};
/// Define used for Rx hostbuf validity.
/// This value should appear only when hostbuf was used for a Reception.
#define RX_DMA_OVER_PATTERN 0xAAAAAA00

/**
 * Define used for MSG buffers validity.
 * This value will be written only when a MSG buffer is used for sending from Emb to App.
 */
#define IPC_MSGE2A_VALID_PATTERN 0xADDEDE2A
#define IPC_MSGE2A_BUF_CNT      8

extern struct co_list rx_buffer_free_list;
enum ipc_rx_buffer_status {
    RX_BUFFER_PUSHED = 0,
    RX_BUFFER_FREE   = 0x5A5A5A5A,
    RX_BUFFER_READY  = RX_DMA_OVER_PATTERN,
    RX_UF_VEC_VALID_PATTERN = 0x0000C0DE,
};

struct rwnx_hw;
#ifdef CONFIG_SDIO_SUPPORT
#if defined(PLATFORM_ALLWIN_RT_THREAD) || defined(PLATFORM_ALLWIN_FREERTOS)
typedef void* dma_addr_t;
#endif
#endif

/// 802.11 Status Code
#define MAC_ST_SUCCESSFUL                   0
#define MAC_ST_FAILURE                      1
#define MAC_ST_RESERVED                     2
#define MAC_ST_CAPA_NOT_SUPPORTED           10
#define MAC_ST_REASSOC_NOT_ASSOC            11
#define MAC_ST_ASSOC_DENIED                 12
#define MAC_ST_AUTH_ALGO_NOT_SUPPORTED      13
#define MAC_ST_AUTH_FRAME_WRONG_SEQ         14
#define MAC_ST_AUTH_CHALLENGE_FAILED        15
#define MAC_ST_AUTH_TIMEOUT                 16
#define MAC_ST_ASSOC_TOO_MANY_STA           17
#define MAC_ST_ASSOC_RATES_NOT_SUPPORTED    18
#define MAC_ST_ASSOC_PREAMBLE_NOT_SUPPORTED 19

#define MAC_ST_ASSOC_SPECTRUM_REQUIRED   22
#define MAC_ST_ASSOC_POWER_CAPA          23
#define MAC_ST_ASSOC_SUPPORTED_CHANNEL   24
#define MAC_ST_ASSOC_SLOT_NOT_SUPPORTED  25

#define MAC_ST_REFUSED_TEMPORARILY       30
#define MAC_ST_INVALID_MFP_POLICY        31

#define MAC_ST_INVALID_IE                40             // draft 7.0 extention
#define MAC_ST_GROUP_CIPHER_INVALID      41             // draft 7.0 extention
#define MAC_ST_PAIRWISE_CIPHER_INVALID   42             // draft 7.0 extention
#define MAC_ST_AKMP_INVALID              43             // draft 7.0 extention
#define MAC_ST_UNSUPPORTED_RSNE_VERSION  44             // draft 7.0 extention
#define MAC_ST_INVALID_RSNE_CAPA         45             // draft 7.0 extention
#define MAC_ST_CIPHER_SUITE_REJECTED     46             // draft 7.0 extention
/** @} */

/**
 * struct rwnx_ipc_elem - Generic IPC buffer of fixed size
 *
 * @addr: Host address of the buffer.
 * @dma_addr: DMA address of the buffer.
 */
struct rwnx_ipc_elem {
    //void *addr;
    dma_addr_t dma_addr;
};
struct rwnx_ipc_host_elem {
    struct co_list_hdr hdr;
    uint32_t addr;
};
/**
 * struct rwnx_ipc_elem_pool - Generic pool of IPC buffers of fixed size
 *
 * @nb: Number of buffers currenlty allocated in the pool
 * @buf: Array of buffers (size of array is @nb)
 * @pool: DMA pool in which buffers have been allocated
 */
struct rwnx_ipc_elem_pool {
    int nb;
    //struct rwnx_ipc_elem *buf;
    struct rwnx_ipc_elem buf[IPC_MSGE2A_BUF_CNT];
    //struct dma_pool *pool;
};

/**
 * struct rwnx_ipc_elem - Generic IPC buffer of variable size
 *
 * @addr: Host address of the buffer.
 * @dma_addr: DMA address of the buffer.
 * @size: Size, in bytes, of the buffer
 */
struct rwnx_ipc_elem_var {
    void *addr;
    dma_addr_t dma_addr;
    size_t size;
};

#if 1//def CONFIG_RWNX_FULLMAC

/**
 * struct rwnx_skb_cb - Control Buffer structure for RX buffer
 *
 * @dma_addr: DMA address of the data buffer
 * @pattern: Known pattern (used to check pointer on skb)
 * @idx: Index in &struct rwnx_hw.rxbuff_table that contains address of this
 * buffer
 */
struct rwnx_skb_cb {
    dma_addr_t dma_addr;
    uint32_t pattern;
    uint32_t idx;
};
#endif /* CONFIG_RWNX_FULLMAC */

#if (AICWF_RWNX_TIMER_EN)
typedef void (*rwnx_timer_cb_t)(void * arg);

enum rwnx_timer_state_e {
    RWNX_TIMER_STATE_FREE   = 0,
    RWNX_TIMER_STATE_POST   = 1,
    RWNX_TIMER_STATE_STOP   = 2,
};

enum rwnx_timer_action_e {
    RWNX_TIMER_ACTION_CREATE    = 0,
    RWNX_TIMER_ACTION_START     = 1,
    RWNX_TIMER_ACTION_RESTART   = 2,
    RWNX_TIMER_ACTION_STOP      = 3,
    RWNX_TIMER_ACTION_DELETE    = 4,
};

struct rwnx_timer_node_s {
    struct co_list_hdr hdr;
    rwnx_timer_cb_t cb;
    void *arg;
    uint32_t expired_ms; // remained
    enum rwnx_timer_state_e state;
    bool periodic;
    bool auto_load;
};

typedef struct rwnx_timer_node_s * rwnx_timer_handle;
#endif

#if (AICWF_RX_REORDER)
#define MAX_REORD_RXFRAME       250
#define REORDER_UPDATE_TIME     300
#define AICWF_REORDER_WINSIZE   64
#define SN_LESS(a, b)           (((a-b)&0x800) != 0)
#define SN_EQUAL(a, b)          (a == b)

struct reord_ctrl {
    uint8_t enable;
    uint8_t wsize_b;
    uint16_t ind_sn;
    uint16_t list_cnt;
    struct co_list reord_list;
    rtos_mutex reord_list_lock;
    #if (AICWF_RWNX_TIMER_EN)
    rwnx_timer_handle reord_timer;
    #else
    rtos_timer reord_timer;
    #endif
};

struct reord_ctrl_info {
    struct co_list_hdr hdr;
    uint8_t mac_addr[6];
    struct reord_ctrl preorder_ctrl[8];
};

struct fhost_rx_buf_tag;

struct recv_msdu {
    struct co_list_hdr hdr;
    struct fhost_rx_buf_tag *rx_buf;
    uint16_t buf_len;
    uint16_t seq_num;
    uint8_t  tid;
    uint8_t  forward;
    //struct reord_ctrl *preorder_ctrl;
    #ifdef CONFIG_RX_NOCOPY
    struct pbuf* rx_pbuf;
    #endif /* CONFIG_RX_NOCOPY */
};

int rwnx_rxdataind_aicwf(struct fhost_rx_buf_tag *buf);
int reord_single_frame_ind(struct recv_msdu *prframe);
#endif

void rwnx_reord_init(void);
void rwnx_reord_deinit(void);

void *rwnx_ipc_fw_trace_desc_get(struct rwnx_hw *rwnx_hw);

uint32_t rwnx_tx_post_list_cnt(void);

int8_t data_pkt_rssi_get(uint8_t *mac_addr);

void reord_deinit_sta_by_mac(const uint8_t *mac_addr);


#endif /* _RWNX_UTILS_H_ */
