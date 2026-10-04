#ifndef __QUEUE_CLIENT_H__
#define __QUEUE_CLIENT_H__
#include "stdint.h"
#include "aic_adp_type.h"

#ifndef SERVER
#define SERVER 0
#endif
#ifndef CLIENT
#define CLIENT 1
#endif

#ifndef OK
#define OK     0
#endif
#define ERR   -1
#ifndef FALSE
#define FALSE  0
#endif
#ifndef TRUE
#define TRUE   1
#endif

#define SOCKER_BUFFER_SIZE (1024)
#define BLE_HOST_EVT_FLAG   0x01
#define BT_HOST_EVT_FLAG    0x02

typedef void(*app_queue_cb)(uint8_t *data, uint16_t length);

extern app_queue_cb app_queue_client_callback;
extern app_queue_cb app_queue_client_bt_msg_callback;

typedef enum _host_msg {
        BLE_INIT_DONE                              ,
        BLE_MSG_DONE                               ,
        BLE_ADV_ENABLE                             ,
        BLE_ADV_DISABLE                            ,
        BLE_SCAN_ENABLE                            ,
        BLE_SCAN_DISABLE                           ,
        BLE_ADV_REPORT                             ,
        BLE_CONNECTION_IND                         ,
        BLE_DISCONNECTED                           ,
        BLE_CONNECTION_PARAM_UPDATE                ,
        BLE_EXCHANGED_MTU_IND                      ,
        BLE_SMARTCONFIG_DATA_RECV                  ,
        BLE_PKT_SIZE_IND                           ,
        BLE_SERVICE_CHANGED_IND                    ,
        BLE_NTF_SENT_DONE                          ,
        BLE_IND_SENT_DONE                          ,
        BLE_CON_UPDATE_CMP_DONE                    ,
        BLE_HOGPRH_DATA_RECV                       ,
        BLE_LTK_IND                                ,
        BLE_ADDR_IND                               ,
        BLE_UDFC_DATABASE_IND                      ,
        BLE_UDFC_NTF_DATA_RECV                     ,
        BLE_UDFC_IND_DATA_RECV                     ,
        BLE_RSSI_IND                               ,
        BLE_IRK_IND                                ,
        BLE_PSK_IND                                ,
        BLE_UDFC_RD_IND                            ,
        BLE_ADV_DATA_CHANGED_IND                   ,
        BLE_SCAN_RSP_CHANGED_IND                   ,
        BLE_HOST_MAX_MSG                     = 0x7F,
        BT_HOST_MSG                          = 0xFF,
}host_msg;

typedef struct {
    uint8_t msg_id;
    uint16_t len;
    uint8_t * buff;
} BLE_HST_MSG;

typedef struct {
    uint32_t EventId;
    uint16_t len;
    uint8_t * buff;
} BT_HST_MSG;

typedef struct {
    uint8_t msg_id;
    uint16_t len;
    uint8_t * buff;
} APP_BLE_ONLY_MSG;

typedef struct
{
    ///6-byte array address value
    uint8_t  addr[6];
} bd_addr_t;

struct gap_bdaddr
{
    /// BD Address of device
    bd_addr_t addr;
    /// Address type of the device 0=public/1=private random
    uint8_t addr_type;
};

struct gapm_ext_adv_report_ind
{
    /// Activity identifier
    uint8_t actv_idx;
    /// Bit field providing information about the received report (@see enum gapm_adv_report_info)
    uint8_t info;
    /// Transmitter device address
    struct gap_bdaddr trans_addr;
    /// Target address (in case of a directed advertising report)
    struct gap_bdaddr target_addr;
    /// TX power (in dBm)
    int8_t tx_pwr;
    /// RSSI (between -127 and +20 dBm)
    int8_t rssi;
    /// Primary PHY on which advertising report has been received
    uint8_t phy_prim;
    /// Secondary PHY on which advertising report has been received
    uint8_t phy_second;
    /// Advertising SID
    /// Valid only for periodic advertising report
    uint8_t adv_sid;
    /// Periodic advertising interval (in unit of 1.25ms, min is 7.5ms)
    /// Valid only for periodic advertising report
    uint16_t period_adv_intv;
    /// Report length
    uint16_t length;
    /// Report
    uint8_t data[];
};

struct ble_connection_ind
{
    /// Connection handle
    uint16_t conhdl;
    /// Connection interval
    uint16_t con_interval;
    /// Connection latency
    uint16_t con_latency;
    /// Link supervision timeout
    uint16_t sup_to;
    /// Clock accuracy
    uint8_t clk_accuracy;
    /// Peer address type
    uint8_t peer_addr_type;
    /// Peer BT address
    bd_addr_t peer_addr;
    /// Role of device in connection (0 = Master / 1 = Slave)
    uint8_t role;
};

struct ble_disconnect_ind
{
    /// Connection handle
    uint16_t conhdl;
    /// Reason of disconnection
    uint8_t reason;
};

struct ble_param_update_req_ind
{
    /// Connection interval minimum
    uint16_t intv_min;
    /// Connection interval maximum
    uint16_t intv_max;
    /// Latency
    uint16_t latency;
    /// Supervision timeout
    uint16_t time_out;
};

struct ble_param_updated_ind
{
    ///Connection interval value
    uint16_t            con_interval;
    ///Connection latency value
    uint16_t            con_latency;
    ///Supervision timeout
    uint16_t            sup_to;
};

struct ble_mtu_changed_ind
{
    /// Exchanged MTU value
    uint16_t mtu;
    /// operation sequence number
    uint16_t seq_num;
};

struct ble_pkt_size_ind
{
    ///The maximum number of payload octets in TX
    uint16_t max_tx_octets;
    ///The maximum time that the local Controller will take to TX
    uint16_t max_tx_time;
    ///The maximum number of payload octets in RX
    uint16_t max_rx_octets;
    ///The maximum time that the local Controller will take to RX
    uint16_t max_rx_time;
};

/// HID report info
struct hogprh_report
{
    /// Report Length
    uint8_t length;
    /// Report value
    uint8_t value[];
};

struct hogprh_report_ind
{
    /// HIDS Instance
    uint8_t hid_idx;
    /// HID Report Index
    uint8_t report_idx;
    /// Report data
    struct hogprh_report report;
};

struct ble_ltk_ind
{
    uint8_t ltk[16];
    uint16_t ediv;
    uint8_t randnb[8];
    uint8_t key_size;
};

struct ble_db_chac_info
{
    /// Attribute value UUID
    uint8_t  val_uuid[16];
    uint8_t  val_prop;
};

struct ble_db_svc_info
{
    /// Service UUID
    uint8_t  uuid[16];
    /// attribute information present in the service
    uint8_t num_of_char;
    /// (length = end_hdl - start_hdl)
    struct ble_db_chac_info info[];
};

struct ble_irk_ind
{
    uint8_t peer_irk[16];
    uint8_t addr[6];
    uint8_t addr_type;
    uint8_t local_irk[16];
};

/*****************************************************/
//BLE api
/*****************************************************/
int app_ble_deinit_start(void);

/*---------------------------------------------------------------------------
 * app_queue_reg_callback()
 *
 *     register a callback for ble application
 *
 * Parameters:
 *
 *     ble application callback
 *
 * Returns:
 *
 *     void
 */
void app_queue_reg_callback(app_queue_cb cb);
/*---------------------------------------------------------------------------
 * app_ble_gap_name_set_msg_send()
 *
 *     set device name in gap.
 *
 * Parameters:
 *
 *     buff : ptr for name.
 *     len : len for name.
 * Returns:
 *
 *     void
 */
int app_ble_gap_name_set_msg_send(uint8_t *buff,uint8_t len);
/*---------------------------------------------------------------------------
 * app_ble_adv_data_set_msg_send()
 *
 *     set adv data before adv is running.
 *
 * Parameters:
 *
 *     buff : ptr for adv data.  
 *            eg: | ad_len | ad_type | ad_value | ad_len | ad_type | ad_value | ....
 *     len : len for adv data.
 *           max len is 29.
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_adv_data_set_msg_send(uint8_t *buff,uint8_t len);
/*---------------------------------------------------------------------------
 * app_ble_scan_response_set_msg_send()
 *
 *     set scan response .
 *
 * Parameters:
 *
 *     buff : ptr for scan response.  
 *            eg: | ad_len | ad_type | ad_value | ad_len | ad_type | ad_value | ....
 *     len : len for scan response.
 *           max len is 29.
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_scan_response_set_msg_send(uint8_t *buff,uint8_t len);
/*---------------------------------------------------------------------------
 * app_ble_adv_param_set_msg_send()
 *
 *     set adv parameter .
 *
 * Parameters:
 *
 *     adv_intv_min : min adv interval. The unit is 0.625 milliseconds
 *     adv_intv_max : max adv interval. The unit is 0.625 milliseconds
 *     max_tx_power : max tx level is 10.
 * 
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_adv_param_set_msg_send(         uint32_t adv_intv_min, uint32_t adv_intv_max,int8_t max_tx_power);
/*---------------------------------------------------------------------------
 * app_ble_adv_data_update_msg_send()
 *
 *     update adv data after adv is running .
 *
 * Parameters:
 *
 *     buff : ptr for adv data.  
 *            eg: | ad_len | ad_type | ad_value | ad_len | ad_type | ad_value | ....
 *     len : len for adv data.
 *           max len is 29.
 * 
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_adv_data_update_msg_send(uint8_t *buff,uint8_t len);
/*---------------------------------------------------------------------------
 * app_ble_adv_param_update_msg_send()
 *
 *     update adv param after adv is running .
 *
 * Parameters:
 *
 *     adv_intv_min : min adv interval. The unit is 0.625 milliseconds
 *     adv_intv_max : max adv interval. The unit is 0.625 milliseconds
 *     max_tx_power : max tx level is 10.
 * 
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_adv_param_update_msg_send(         uint32_t adv_intv_min, uint32_t adv_intv_max,int8_t max_tx_power);
/*---------------------------------------------------------------------------
 * app_ble_scan_response_update_msg_send()
 *
 *     update adv scan response after adv is running .
 *
 * Parameters:
 *
 *     buff : ptr for adv scan response data.  
 *            eg: | ad_len | ad_type | ad_value | ad_len | ad_type | ad_value | ....
 *     len : len for scan response data.
 *           max len is 29.
 * 
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_scan_response_update_msg_send(uint8_t *buff,uint8_t len);
/*---------------------------------------------------------------------------
 * app_ble_adv_stop_msg_send()
 *
 *     update stop adv after adv is running .
 *
 * Parameters:
 * 
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_adv_stop_msg_send(void);
/*---------------------------------------------------------------------------
 * app_ble_adv_start_msg_send()
 *
 *     update start adv before adv is running .
 *
 * Parameters:
 * 
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_adv_start_msg_send(void);
/*---------------------------------------------------------------------------
 * app_ble_disconnect_msg_send()
 *
 *     disconnect the connection between lbh_server and remote device .
 *
 * Parameters:
 * 
 *     conidx : connection index
 * 
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_disconnect_msg_send(uint8_t conidx);
/*---------------------------------------------------------------------------
 * app_ble_con_param_msg_send()
 *
 *     update connection parameter after link is connected between lbh_server and remote device .
 *
 * Parameters:
 *
 *     conidx : connection index
 *     conn_intv_min : min connection interval. The unit is 1.25 milliseconds .Time Range:7.5ms to 4s.
 *     conn_intv_max : max connection interval. The unit is 1.25 milliseconds .Time Range:7.5ms to 4s.
 *     conn_latency : Range 0x0000 to 0x01F3.
 *     time_out :  Range N is 0x000A to 0x0C80
 *                 Time = N *10ms
 *                 Time Range is 100ms to 32s
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_con_param_msg_send(uint8_t conidx, uint16_t conn_intv_min, uint16_t conn_intv_max,uint16_t conn_latency, uint16_t time_out);
/*---------------------------------------------------------------------------
 * app_ble_del_bond_msg_send()
 *
 *     delete bonding between lbh_server and remote device .
 *
 * Parameters:
 * 
 *     conidx : connection index
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_del_bond_msg_send(uint8_t conidx);
/*---------------------------------------------------------------------------
 * app_ble_set_scan_param()
 *
 *     set scan interval and window before scan was starting. .
 *
 * Parameters:
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_set_scan_param(uint16_t scan_wd,uint16_t scan_intv);
/*---------------------------------------------------------------------------
 * app_ble_scan_msg_start()
 *
 *     start scan process and will receive adv from remote device .
 *
 * Parameters:
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_scan_msg_start(void);
/*---------------------------------------------------------------------------
 * app_ble_scan_msg_stop()
 *
 *     stop scan process  .
 *
 * Parameters:
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_scan_msg_stop(void);
/*---------------------------------------------------------------------------
 * app_ble_init_msg_start()
 *
 *     start init process.
 *
 * Parameters:
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_init_msg_start(void);
/*---------------------------------------------------------------------------
 * app_ble_init_msg_stop()
 *
 *     stop init process  .
 *
 * Parameters:
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_init_msg_stop(void);
/*---------------------------------------------------------------------------
 * app_ble_smartconfig_send_notification()
 *
 *     send notification by UUID in buff.
 *
 * Parameters:
 *
 *      buff : conidx+UUID+data
 *      len : len of(conidx+UUID+data)
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_smartconfig_send_notification(uint8_t *buff ,uint16_t len);
/*---------------------------------------------------------------------------
 * app_ble_smartconfig_send_indication()
 *
 *     send indication by UUID in buff.
 *
 * Parameters:
 *
 *      buff : conidx+UUID+data
 *      len : len of(conidx+UUID+data)
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_smartconfig_send_indication(uint8_t *buff ,uint16_t len);
/*---------------------------------------------------------------------------
 * app_ble_smartconfig_update_read_value()
 *
 *     update value by UUID which properties have read.
 *
 * Parameters:
 *
 *      buff : conidx+UUID+data
 *      len : len of(conidx+UUID+data)
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_smartconfig_update_read_value(uint8_t *buff ,uint16_t len);
/*---------------------------------------------------------------------------
 * app_ble_set_lbh_print()
 *
 *     set lbh debug print
 *
 * Parameters:
 *
 *      en_flag : enable or not
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_set_lbh_print(unsigned int en_flag);
/*---------------------------------------------------------------------------
 * app_ble_udfc_connect()
 *
 *     start a ble connection
 *
 * Parameters:
 *
 *      addr : peer MAC address
 *      addr_type : peer address type
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_udfc_connect(uint8_t *addr, uint8_t addr_type);
/*---------------------------------------------------------------------------
 * app_ble_udfc_connect_cancel()
 *
 *     cancel ble connecting procedure
 *
 * Parameters:
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_udfc_connect_cancel(void);
/*---------------------------------------------------------------------------
 * app_ble_udfc_write()
 *
 *     write gatt server's characteristic
 *
 * Parameters:
 * 
 *     conidx : connection index
 *     buff : UUID+data
 *     len : len of(UUID+data)
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_udfc_write(uint8_t conidx, uint8_t *buff, uint16_t len);
/*---------------------------------------------------------------------------
 * app_ble_udfc_write_no_response()
 *
 *     write without response gatt server's characteristic
 *
 * Parameters:
 * 
 *     conidx : connection index
 *     buff : UUID+data
 *     len : len of(UUID+data)
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_udfc_write_no_response(uint8_t conidx, uint8_t *buff, uint16_t len);
/*---------------------------------------------------------------------------
 * app_ble_udfc_read()
 *
 *     read gatt server's characteristic
 *
 * Parameters:
 * 
 *     conidx : connection index
 *     val_uuid : UUID
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_udfc_read(uint8_t conidx, uint8_t *val_uuid);
/*---------------------------------------------------------------------------
 * app_ble_udfc_notification_cfg()
 *
 *     write enable gatt server's notification characteristic
 *
 * Parameters:
 * 
 *     conidx : connection index
 *     val_uuid : UUID
 *     cfg_en : enable or disable
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_udfc_notification_cfg(uint8_t conidx, uint8_t *val_uuid, bool cfg_en);
/*---------------------------------------------------------------------------
 * app_ble_udfc_indication_cfg()
 *
 *     write enable gatt server's indication characteristic
 *
 * Parameters:
 * 
 *     conidx : connection index
 *     val_uuid : UUID
 *     cfg_en : enable or disable
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_udfc_indication_cfg(uint8_t conidx, uint8_t *val_uuid, bool cfg_en);
/*---------------------------------------------------------------------------
 * app_ble_set_bt_addr()
 *
 *     set local bt addr .if not set, local device will use default mac addr in efuse.
 *
 * Parameters:
 *
 *      addr : bt addr .eg: 0x00,0x01,0x02,0x03,0x04,0x05
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_set_bt_addr(uint8_t *addr);
/*---------------------------------------------------------------------------
 * app_ble_get_bt_addr()
 *
 *     get local bt addr .if not set, local device will return default mac addr in efuse.
 *
 * Parameters:
 *
 *      addr : bt addr .eg: 0x00,0x01,0x02,0x03,0x04,0x05
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_get_bt_addr(void);
/*---------------------------------------------------------------------------
 * app_ble_set_data_len()
 *
 *     set packet len in air tx .if not set,default value is 23 byte.
 *
 * Parameters:
 *
 *      conidx
 *      tx_octets  range 0x001B to 0x00FB
 *      tx_time    range 0x0148 to 0x4290
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_set_data_len(uint8_t conidx, uint16_t tx_octets,uint16_t tx_time);
/*---------------------------------------------------------------------------
 * app_ble_set_ltk()
 *
 *     set ltk.
 *
 * Parameters:
 *
 *      ltk
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_set_ltk(struct ble_ltk_ind ltk);
/*---------------------------------------------------------------------------
 * app_ble_clear_ltk()
 *
 *     clear ltk.
 *
 * Parameters:
 *
 *      
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_clear_ltk(void);
/*---------------------------------------------------------------------------
 * app_ble_set_defalut_mtu()
 *
 *     set defalut mtu, must bt set before connected.
 *
 * Parameters:
 *
 *      max_mtu
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_set_defalut_mtu(uint16_t max_mtu);
/*---------------------------------------------------------------------------
 * app_ble_get_curr_rssi()
 *
 *     get current rssi, must have existing le connection.
 *
 * Parameters:
 *
 *      conidx
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_get_curr_rssi(uint8_t conidx);
/*---------------------------------------------------------------------------
 * app_ble_set_irk()
 *
 *     set irk.
 *
 * Parameters:
 *
 *      irk
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_set_irk(struct ble_irk_ind irk);
/*---------------------------------------------------------------------------
 * app_ble_set_sec_mode()
 *
 *     set security mode.
 *
 * Parameters:
 *
 *      mode
 *        0: no bond
 *        1: just work
 *        2: passkey display
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_set_sec_mode(uint8_t mode);
/*---------------------------------------------------------------------------
 * app_ble_hogprh_connect()
 *
 *     start a ble connection
 *
 * Parameters:
 *
 *      addr : peer MAC address
 *      addr_type : peer address type
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_hogprh_connect(uint8_t *addr, uint8_t addr_type);
/*---------------------------------------------------------------------------
 * app_ble_hogprh_connect_cancel()
 *
 *     cancel ble connecting procedure
 *
 * Parameters:
 *
 * Returns:
 *
 *     0: success.
 *    -1: send error.
 */
int app_ble_hogprh_connect_cancel(void);

/*************************************************************************************************/
//bt function api

#define KEY_ID_BUILD(name, func)    ((uint32_t)((name) | (func)))
#define KEY_NAME_GET(key_id)        (key_id & 0xFFFF0000)
#define KEY_FUNC_GET(key_id)        ((key_id) & 0xFFFF)

//key name
#define APP_KEY_NUM0             (0<<16)
#define APP_KEY_NUM1             (1<<16)
#define APP_KEY_NUM2             (2<<16)
#define APP_KEY_NUM3             (3<<16)
#define APP_KEY_NUM4             (4<<16)
#define APP_KEY_NUM5             (5<<16)
#define APP_KEY_NUM6             (6<<16)
#define APP_KEY_NUM7             (7<<16)
#define APP_KEY_NUM8             (8<<16)
#define APP_KEY_NUM9             (9<<16)

#define APP_KEY_POWER            (10<<16)
#define APP_KEY_PLAY             (11<<16)
#define APP_KEY_NEXT             (12<<16)
#define APP_KEY_PREV             (13<<16)
#define APP_KEY_VOLADD           (14<<16)
#define APP_KEY_VOLSUB           (15<<16)
#define APP_KEY_EQ               (16<<16)
#define APP_KEY_MUTE             (17<<16)
#define APP_KEY_GSENSOR          (18<<16)
#define APP_KEY_NULL             (19<<16)

//key function
#define APP_KEY_REPEAT           (1)
#define APP_KEY_HOLD             (2)
#define APP_KEY_DOWN             (3)
#define APP_KEY_UP               (4)
#define APP_KEY_PRESS            (5)
#define APP_KEY_HOLD_2S          (6)
#define APP_KEY_HOLD_3S          (7)
#define APP_KEY_HOLD_5S          (8)
#define APP_KEY_DOUBLE_CLICK     (9)
#define APP_KEY_TRIPLE_CLICK     (10)


typedef struct{
    uint32_t current_clk;
    uint32_t counterNum;
    uint32_t master_current_samplerate;
    uint32_t current_bt_counter;
    uint32_t current_bt_counter_offset;
    uint32_t strb_cycle_offset;
}AppTwsMediaDataStruct;

typedef struct {
    uint8_t len;
    uint8_t maxResp;
}AppBtInquiryP;

typedef struct {
    BT_ADDR bdaddr;
    uint8_t on_off;
    uint8_t codec_type;
    uint8_t samp_freq;
}AppA2dpP;

typedef struct {
    uint8_t tws_sync_ctrl;
}AppTwsP;

typedef struct {
    BT_ADDR bdaddr;
    AppBtLinkPolicy policy;
}AppBtLinkPolicyP;

typedef struct {
    BT_ADDR bdaddr;
    AppBtSniffInfo sniff_info;
    uint32_t timeout;
}AppBtSniffP;

typedef struct {
    uint8_t dev_id;
    uint32_t num;
}AppBtOtaP;

typedef struct _bt_msg_param {
    union {
        BT_ADDR bdaddr;
        host_data_struct buff;
        AppA2dpP a2dp_param;
        AppBtInquiryP inquiry_param;
        AppTwsP tws_param;
        AppBtLinkPolicyP linkpolicy_param;
        AppBtSniffP btsniff_param;
        AppBtOtaP btota_param;
        AppBtScanMode mode;
        uint8_t dut;
        uint8_t lp_level;
        uint8_t audio_mode;
        AppTwsMediaDataStruct sync_param;
        uint32_t key;
        uint8_t wearing_state;
    }p;
} bt_msg_param;

typedef struct {
    uint32_t msg_id;
    bt_msg_param param;
} APP_BT_MSG;

typedef struct _BtDeviceRecord
{
    BT_ADDR  bdAddr;
    bool        trusted;
    uint8_t     linkKey[16];
    uint8_t     keyType;
    uint8_t     pinLen;
} BtDeviceRecord;

#define KEY_ID_BUILD(name, func)    ((uint32_t)((name) | (func)))
#define KEY_NAME_GET(key_id)        (key_id & 0xFFFF0000)
#define KEY_FUNC_GET(key_id)        ((key_id) & 0xFFFF)

//key name
#define APP_KEY_NUM0             (0<<16)
#define APP_KEY_NUM1             (1<<16)
#define APP_KEY_NUM2             (2<<16)
#define APP_KEY_NUM3             (3<<16)
#define APP_KEY_NUM4             (4<<16)
#define APP_KEY_NUM5             (5<<16)
#define APP_KEY_NUM6             (6<<16)
#define APP_KEY_NUM7             (7<<16)
#define APP_KEY_NUM8             (8<<16)
#define APP_KEY_NUM9             (9<<16)

#define APP_KEY_POWER            (10<<16)
#define APP_KEY_PLAY             (11<<16)
#define APP_KEY_NEXT             (12<<16)
#define APP_KEY_PREV             (13<<16)
#define APP_KEY_VOLADD           (14<<16)
#define APP_KEY_VOLSUB           (15<<16)
#define APP_KEY_EQ               (16<<16)
#define APP_KEY_MUTE             (17<<16)
#define APP_KEY_GSENSOR          (18<<16)
#define APP_KEY_NULL             (19<<16)

//key function
#define APP_KEY_REPEAT           (1)
#define APP_KEY_HOLD             (2)
#define APP_KEY_DOWN             (3)
#define APP_KEY_UP               (4)
#define APP_KEY_PRESS            (5)
#define APP_KEY_HOLD_2S          (6)
#define APP_KEY_HOLD_3S          (7)
#define APP_KEY_HOLD_5S          (8)
#define APP_KEY_DOUBLE_CLICK     (9)
#define APP_KEY_TRIPLE_CLICK     (10)

typedef uint8_t CodecType;
#define AVDTP_CODEC_TYPE_SBC           0x00
#define AVDTP_CODEC_TYPE_MPEG_AAC      0x02

typedef uint8_t samp_freq;
#define A2DP_SAMP_FREQ_44     0x20    /* b5:44.1kHz */
#define A2DP_SAMP_FREQ_48     0x10    /* b4:48  kHz */

/*****************************************************/
//BT api
/*****************************************************/
void app_queue_reg_bt_msg_callback(app_queue_cb cb);

int app_bt_role_switch(BT_ADDR* bdaddr);

int app_bt_disconnect_acl(BT_ADDR* bdaddr);

int app_bt_disconnect_all_acl(void);

int app_bt_stop_sniff(BT_ADDR* bdaddr);

int app_bt_setscanmode(AppBtScanMode mode);

int app_bt_set_linkpolicy(BT_ADDR* bdaddr, AppBtLinkPolicy policy);

int app_bt_set_sniff_timer(BT_ADDR *bdaddr,
                                        AppBtSniffInfo* sniff_info,
                                        TimeT Time);

int app_bt_connect_a2dp(BT_ADDR *bdaddr);

int app_bt_source_connect_a2dp(BT_ADDR *bdaddr);

int app_bt_close_a2dp(BT_ADDR *bdaddr);

int app_bt_connect_hfg(BT_ADDR *bdaddr);

int app_bt_connect_hfp(BT_ADDR *bdaddr);

int app_bt_disconnect_hfp(BT_ADDR *bdaddr);

int app_bt_hfp_connect_sco(BT_ADDR *bdaddr);

int app_bt_hfp_disconnect_sco(BT_ADDR *bdaddr);

int app_bt_a2dp_start(BT_ADDR* bdaddr,uint32_t on);

int app_bt_send_key(   uint32_t Key);

int app_bt_dut_mode(void);
int app_bt_wr_scan_en(unsigned int scan_en,unsigned int dut);
int app_bt_inquiry_dev(unsigned int len,unsigned int maxResp);
int app_bt_inquiry_cancel(void);
int app_bt_spp_send_data(uint8_t *data,uint16_t len);
int app_bt_sco_send_data(uint8_t *data,uint16_t len);
int app_bt_a2dp_send_data(uint8_t *data,uint16_t len,uint8_t frameSize);
int app_bt_trace_onoff(uint8_t onoff,uint32_t level);
int app_bt_set_pcm(uint8_t routing, uint8_t role, uint8_t out_edge, uint8_t sync_mode, uint8_t data_shift_mode, uint8_t clock_rate, uint8_t simple_rate);
int app_bt_set_name(const uint8_t *name, uint8_t len);
int app_bt_hfp_call_release(void);
int app_bt_hfp_call_answer(void);
int app_bt_hfp_send_volume(uint8_t gain);
int app_bt_hfp_call_redial(void);
int app_bt_hfp_siri_voicerecognition(void);
int app_bt_a2dp_send_reconfig(CodecType codec_type,samp_freq samp_freq);
int app_bt_set_stored_database(uint8_t *buff, uint8_t len);
int app_bt_delete_device_in_database(BT_ADDR *bdaddr);


/*---------------------------------------------------------------------------
 * lbh_client_socket_and_msg_cb_init()
 *
 *     initialializes a socket to link the lbh_server socket, and bond the callback function registered in the specified function
 *     app_queue_reg_callback() or app_queue_reg_bt_msg_callback().
 * Parameters:
 *
 *     ble application callback
 *
 * Returns:
 *
 *     void
 */
int lbh_client_socket_and_msg_cb_init(void);

#endif
