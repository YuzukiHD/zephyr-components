/**
 ****************************************************************************************
 *
 * @file fhost_rx.h
 *
 * @brief Definitions of the fully hosted RX task.
 *
 * Copyright (C) RivieraWaves 2017-2019
 *
 ****************************************************************************************
 */

#ifndef _FHOST_RX_H_
#define _FHOST_RX_H_

/**
 ****************************************************************************************
 * @defgroup FHOST_RX FHOST_RX
 * @ingroup FHOST
 * @brief Fully Hosted RX task implementation.
 * This module creates a task that will be used to handle the RX descriptors passed by
 * the WiFi task.
 * @{
 ****************************************************************************************
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "rtos_al.h"
#include "cfgrwnx.h"
#include "co_math.h"
#include "co_list.h"
#include "rwnx_rx.h"
#ifdef CONFIG_SDIO_SUPPORT
#include "sdio_port.h"
#endif
#ifdef CONFIG_USB_SUPPORT
#include "usb_port.h"
#endif

/// Maximum number MSDUs supported in one received A-MSDU
#define NX_MAX_MSDU_PER_RX_AMSDU 8

/// Decryption status mask.
#define RX_HD_DECRSTATUS 0x0000007C

/// Decryption type offset
#define RX_HD_DECRTYPE_OFT 2
/// Frame decrypted using WEP.
#define RX_HD_DECR_WEP (0x01 << RX_HD_DECRTYPE_OFT)
/// Frame decrypted using TKIP.
#define RX_HD_DECR_TKIP (0x02 << RX_HD_DECRTYPE_OFT)
/// Frame decrypted using CCMP 128bits.
#define RX_HD_DECR_CCMP128 (0x03 << RX_HD_DECRTYPE_OFT)

/// Packet contains an A-MSDU
#define RX_FLAGS_IS_AMSDU_BIT         CO_BIT(0)
/// Packet contains a 802.11 MPDU
#define RX_FLAGS_IS_MPDU_BIT          CO_BIT(1)
/// Packet contains 4 addresses
#define RX_FLAGS_4_ADDR_BIT           CO_BIT(2)
/// Packet is a Mesh Beacon received from an unknown Mesh STA
#define RX_FLAGS_NEW_MESH_PEER_BIT    CO_BIT(3)
#define RX_FLAGS_NEED_TO_REORD_BIT    CO_BIT(5)
#define RX_FLAGS_UPLOAD_BIT           CO_BIT(6)
#define RX_FLAGS_MONITOR_BIT          CO_BIT(7)

/// Offset of the VIF index field
#define RX_FLAGS_VIF_INDEX_OFT  8
/// Mask of the VIF index field
#define RX_FLAGS_VIF_INDEX_MSK  (0xFF << RX_FLAGS_VIF_INDEX_OFT)
/// Offset of the STA index field
#define RX_FLAGS_STA_INDEX_OFT  16
/// Mask of the STA index field
#define RX_FLAGS_STA_INDEX_MSK  (0xFF << RX_FLAGS_STA_INDEX_OFT)
/// Offset of the destination STA index field
#define RX_FLAGS_DST_INDEX_OFT  24
/// Mask of the destination STA index field
#define RX_FLAGS_DST_INDEX_MSK  (0xFF << RX_FLAGS_DST_INDEX_OFT)

/// Bitmask indicating that a received packet is not a MSDU
#define RX_FLAGS_NON_MSDU_MSK        (RX_FLAGS_IS_AMSDU_BIT | RX_FLAGS_IS_MPDU_BIT |     \
                                      RX_FLAGS_4_ADDR_BIT | RX_FLAGS_NEW_MESH_PEER_BIT)

/*
 * TYPE DEFINITIONS
 ****************************************************************************************
 */
#define FHOST_RX_BUF_SIZE           (RX_MAX_AMSDU_SUBFRAME_LEN + 1)
#define FHOST_RX_BUF_COUNT          (32)
#define FHOST_RX_BUF_ALIGN_NUM      (64)
#define FHOST_RX_SW_VER             (3) // 1: handle msg & forward data buf in sdio_rx_task
                                        // 2: handle msg in sdio_rx_task, cpy & forward data buf to fhost_rx_task
                                        // 3: handle msg & forward data buf in fhost_rx_task

/// FHOST RX environment structure
struct fhost_rx_buf_tag
{
   // union {
   //     net_buf_rx_t net_buf;
        //struct co_list_hdr hdr;
   // } net_hdr;
    #if (FHOST_RX_SW_VER == 2)
    struct co_list_hdr hdr;
    uint32_t headroom;
    #endif

    /// Structure containing the information about the received payload
    struct rx_info info;
    /// Payload buffer space
    uint32_t payload[CO_ALIGN4_HI(FHOST_RX_BUF_SIZE)/sizeof(uint32_t)];
};

enum fhost_rx_async_frame_type {
    RX_ASYNC_UNKNOWN_FRAME = 0,
    RX_ASYNC_RX_FRAME,
    RX_ASYNC_REORDER_FRAME,
};

struct fhost_rx_async_desc_tag
{
    struct co_list_hdr hdr;
    uint8_t *data;
    uint32_t len;
    void *net_if;
    void *frame_ptr;
    uint8_t frame_type;
    bool from_heap;
};

#if NX_UF_EN
/// Structure for receive Vector 1
struct rx_vector_1
{
    /// Contains the bytes 4 - 1 of Receive Vector 1
    uint32_t            recvec1a;
    /// Contains the bytes 8 - 5 of Receive Vector 1
    uint32_t            recvec1b;
    /// Contains the bytes 12 - 9 of Receive Vector 1
    uint32_t            recvec1c;
    /// Contains the bytes 16 - 13 of Receive Vector 1
    uint32_t            recvec1d;
};

struct rx_vector_desc
{
    /// Structure containing the information about the PHY channel that was used for this RX
    struct phy_channel_info phy_info;

    /// Structure containing the rx vector 1
    struct rx_vector_1 rx_vec_1;

    /// Used to mark a valid rx vector
    uint32_t pattern;
};
/// FHOST UF environment structure
struct fhost_rx_uf_buf_tag
{
    struct co_list_hdr hdr;
    struct rx_vector_desc rx_vector;

    /// Payload buffer space (empty in case of UF buffer)
    uint32_t payload[];
};

/// Structure used, when receiving UF, for the buffer elements exchanged between the FHOST RX and the MAC thread
struct fhost_rx_uf_buf_desc_tag
{
    /// Id of the RX buffer
    uint32_t host_id;
};
#endif // NX_UF_EN


#define IS_QOS_DATA(frame_cntrl) ((frame_cntrl & MAC_FCTRL_TYPESUBTYPE_MASK) == MAC_FCTRL_QOS_DATA)



typedef struct {
    struct co_list list;
    rtos_mutex mutex;
} fhost_rxq_t;

/// FHOST RX environment structure
struct fhost_rx_env_tag
{
    #if FHOST_RX_REORDER
    struct co_list rx_reorder_list;
    rtos_semaphore rx_reorder_lock;
    rtos_timer     rx_reorder_timer;
    #endif /* FHOST_RX_REORDER */
    uint32_t  flags;
    uint16_t  next_rx_seq_no;
    /// Management frame Callback function pointer
    cb_fhost_rx mgmt_cb;
    /// Management Callback parameter
    void *mgmt_cb_arg;
    /// Monitor Callback function pointer
    cb_fhost_rx monitor_cb;
    /// Monitor Callback parameter
    void *monitor_cb_arg;
    #if (FHOST_RX_SW_VER == 2)
    fhost_rxq_t rxq_free;
    fhost_rxq_t rxq_post;
    rtos_semaphore rxq_trigg;
    #elif (FHOST_RX_SW_VER == 3)
    #ifdef CONFIG_SDIO_SUPPORT
    struct sdio_buf_list_s rxq;
    #endif
    #ifdef CONFIG_USB_SUPPORT
    struct usb_buf_list_s rxq;
    #endif
    rtos_semaphore rxq_trigg;
    fhost_rxq_t rxq_async_free;
    fhost_rxq_t rxq_async_post;
    #endif
};

/// Structure used for the buffer elements exchanged between the FHOST RX and the MAC thread
struct fhost_rx_buf_desc_tag
{
    /// Id of the RX buffer
    uint32_t host_id;
    /// Address of the payload inside the buffer
    uint32_t addr;
};
#if 0//NX_UF_EN
struct rx_leg_vect
{
    uint8_t    dyn_bw_in_non_ht     : 1;
    uint8_t    chn_bw_in_non_ht     : 2;
    uint8_t    rsvd_nht             : 4;
    uint8_t    lsig_valid           : 1;
} __PACKED;

struct rx_ht_vect
{
    uint16_t   sounding             : 1;
    uint16_t   smoothing            : 1;
    uint16_t   short_gi             : 1;
    uint16_t   aggregation          : 1;
    uint16_t   stbc                 : 1;
    uint16_t   num_extn_ss          : 2;
    uint16_t   lsig_valid           : 1;
    uint16_t   mcs                  : 7;
    uint16_t   fec                  : 1;
    uint16_t   length               :16;
} __PACKED;

struct rx_vht_vect
{
    uint8_t   sounding              : 1;
    uint8_t   beamformed            : 1;
    uint8_t   short_gi              : 1;
    uint8_t   rsvd_vht1             : 1;
    uint8_t   stbc                  : 1;
    uint8_t   doze_not_allowed      : 1;
    uint8_t   first_user            : 1;
    uint8_t   rsvd_vht2             : 1;
    uint16_t  partial_aid           : 9;
    uint16_t  group_id              : 6;
    uint16_t  rsvd_vht3             : 1;
    uint32_t  mcs                   : 4;
    uint32_t  nss                   : 3;
    uint32_t  fec                   : 1;
    uint32_t  length                :20;
    uint32_t  rsvd_vht4             : 4;
} __PACKED;
struct rx_he_vect
{
    uint8_t   sounding              : 1;
    uint8_t   beamformed            : 1;
    uint8_t   gi_type               : 2;
    uint8_t   stbc                  : 1;
    uint8_t   rsvd_he1              : 3;

    uint8_t   uplink_flag           : 1;
    uint8_t   beam_change           : 1;
    uint8_t   dcm                   : 1;
    uint8_t   he_ltf_type           : 2;
    uint8_t   doppler               : 1;
    uint8_t   rsvd_he2              : 2;

    uint8_t   bss_color             : 6;
    uint8_t   rsvd_he3              : 2;

    uint8_t   txop_duration         : 7;
    uint8_t   rsvd_he4              : 1;

    uint8_t   pe_duration           : 4;
    uint8_t   spatial_reuse         : 4;

    uint8_t  rsvd_he5               : 8;

    uint32_t  mcs                   : 4;
    uint32_t  nss                   : 3;
    uint32_t  fec                   : 1;
    uint32_t  length                :20;
    uint32_t  rsvd_he6              : 4;
}__PACKED;

struct uf_rx_vector_1 {
    uint8_t     format_mod         : 4;
    uint8_t     ch_bw              : 3;
    uint8_t     pre_type           : 1;
    uint8_t     antenna_set        : 8;
    int32_t     rssi_leg           : 8;
    uint32_t    leg_length         :12;
    uint32_t    leg_rate           : 4;
    int32_t     rssi1              : 8;

    union
    {
        struct rx_leg_vect leg;
        struct rx_ht_vect ht;
        struct rx_vht_vect vht;
        struct rx_he_vect he;
    };
} __PACKED;

____INLINE uint32_t hal_desc_get_vht_length(struct rx_vector_1 *rx_vec_1)
{
    uint32_t length;

    length = ((((rx_vec_1->recvec1d) & 0xF) << 16) | (((rx_vec_1->recvec1c) & 0xFFFF0000) >> 16));

    return length;
}
#endif /* NX_UF_EN */

/*
 * GLOBAL VARIABLES
 ****************************************************************************************
 */
/// FHOST RX environment
extern struct fhost_rx_env_tag fhost_rx_env;

/*
 * FUNCTIONS
 ****************************************************************************************
 */
#if 0
/**
 ****************************************************************************************
 * @brief Initialization of the RX task.
 * This function initializes the different data structures used for the RX and creates the
 * RTOS task dedicated to the RX processing.
 *
 * @return 0 on success and != 0 if error occurred.
 ****************************************************************************************
 */
int fhost_rx_init(void);
#endif

/**
 ****************************************************************************************
 * @brief Set the callback to call when receiving management frames (i.e. they have
 * not been processed by the wifi task).
 *
 * @attention The callback is called with a @ref fhost_frame_info parameter that is only
 * valid during the callback. If needed the callback is responsible to save the frame for
 * futher processing.
 *
 * @param[in] cb   Callback function pointer
 * @param[in] arg  Callback parameter (NULL if not needed)
 ****************************************************************************************
 */
void fhost_rx_set_mgmt_cb(cb_fhost_rx cb, void *arg);

/**
 ****************************************************************************************
 * @brief Set the callback to call when receiving packets in monitor mode.
 *
 * @attention The callback is called with a @ref fhost_frame_info parameter that is only
 * valid during the callback. If needed the callback is responsible to save the frame for
 * futher processing.
 *
 * @param[in] cb   Callback function pointer
 * @param[in] arg  Callback parameter (NULL if not needed)
 ****************************************************************************************
 */
void fhost_rx_set_monitor_cb(cb_fhost_rx cb, void *arg);
uint8_t machdr_len_get(uint16_t frame_cntl);
void fhost_rx_buf_push(void *net_buf);
#ifndef CONFIG_RX_NOCOPY
void fhost_rx_buf_forward(struct fhost_rx_buf_tag *buf);
#else
void fhost_rx_buf_forward(struct fhost_rx_buf_tag *buf, struct pbuf *p_buf);
#endif
void fhost_rx_monitor_cb(void *buf, bool uf);
void e2a_data_send(uint8_t *mac80211_data, uint32_t length);

#if NX_UF_EN
void fhost_rx_uf_buf_push(struct fhost_rx_uf_buf_tag *);
#endif /* NX_UF_EN */

int fhost_rx_data_resend(net_if_t *net_if, struct fhost_rx_buf_tag *buf,
                         struct mac_addr *da, struct mac_addr *sa, uint8_t machdr_len);

#if (FHOST_RX_SW_VER == 2)
void fhost_rx_init(void);
void fhost_rx_deinit(void);
struct co_list_hdr *fhost_rxq_pop(fhost_rxq_t *rxq);
void fhost_rxq_push(fhost_rxq_t *rxq, struct co_list_hdr *hdr);
#endif
#if (FHOST_RX_SW_VER == 3)
void fhost_rx_init(struct rwnx_hw *rwnx_hw);
#ifdef CONFIG_SDIO_SUPPORT
struct sdio_buf_node_s * fhost_rxframe_dequeue(void);
void fhost_rxframe_enqueue(struct sdio_buf_node_s *node);
#endif
#ifdef CONFIG_USB_SUPPORT
struct aicwf_usb_buf * fhost_rxframe_dequeue(void);
void fhost_rxframe_enqueue(struct aicwf_usb_buf *node);
#endif
#ifdef CONFIG_FHOST_RX_ASYNC
struct fhost_rx_async_desc_tag * fhost_rx_async_desc_alloc(void);
void fhost_rx_async_desc_free(struct fhost_rx_async_desc_tag *desc);
struct fhost_rx_async_desc_tag * fhost_rx_async_post_dequeue(void);
void fhost_rx_async_post_enqueue(struct fhost_rx_async_desc_tag *desc);
uint32_t fhost_rx_async_post_cnt(bool lock);
void *fhost_rx_frame_in_process_get(void);
#ifdef CONFIG_REORD_FORWARD_LIST
void *fhost_rx_frame_match(struct fhost_rx_buf_tag *buf);
#else
bool fhost_rx_frame_match(struct fhost_rx_buf_tag *buf, struct fhost_rx_async_desc_tag *desc);
#endif
#endif
#endif

/// @}

#endif // _FHOST_RX_H_
