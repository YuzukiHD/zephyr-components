#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <stdbool.h>
#include "queue_client.h"
#ifdef CONFIG_LBH_CODEC
#include "lsbc.h"
#include "aacdec.h"
#include "coder.h"
#endif //CONFIG_LBH_CODEC
#include <sys/stat.h>
#include "lbh_codec.h"

#ifndef SOCKER_BUFFER_SIZE
#define SOCKER_BUFFER_SIZE  1024
#endif
void lbh_terminate(void);

//char ble_userconfig[] = "\"service\":{\"uuid\":[\"0xfb\",\"0x34\",\"0x9b\",\"0x5f\",\"0x80\",\"0x00\",\"0x00\",\"0x80\",\"0x00\",\"0x10\",\"0x00\",\"0x00\",\"0xe7\",\"0xfe\",\"0x00\",\"0x00\"],\"sec_en\":0},\"characteristic\":[{\"uuid\":[\"0xfb\",\"0x34\",\"0x9b\",\"0x5f\",\"0x80\",\"0x00\",\"0x00\",\"0x80\",\"0x00\",\"0x10\",\"0x00\",\"0x00\",\"0xc8\",\"0xfe\",\"0x00\",\"0x00\"],\"properties\":[\"notification\",\"indication\",\"read\"],\"max_len\":200},{\"uuid\":[\"0xfb\",\"0x34\",\"0x9b\",\"0x5f\",\"0x80\",\"0x00\",\"0x00\",\"0x80\",\"0x00\",\"0x10\",\"0x00\",\"0x00\",\"0xc7\",\"0xfe\",\"0x00\",\"0x00\"],\"properties\":[\"write\"],\"max_len\":200},{\"uuid\":[\"0xfb\",\"0x34\",\"0x9b\",\"0x5f\",\"0x80\",\"0x00\",\"0x00\",\"0x80\",\"0x00\",\"0x10\",\"0x00\",\"0x00\",\"0xc9\",\"0xfe\",\"0x00\",\"0x00\"],\"properties\":[\"notification\",\"indication\",\"read\"],\"max_len\":200},{\"uuid\":[\"0xfb\",\"0x34\",\"0x9b\",\"0x5f\",\"0x80\",\"0x00\",\"0x00\",\"0x80\",\"0x00\",\"0x10\",\"0x00\",\"0x00\",\"0xca\",\"0xfe\",\"0x00\",\"0x00\"],\"properties\":[\"write\",\"write_no_response\"],\"max_len\":200}]";
#if 1
char ble_userconfig[] = " \
{\n\
  \"service\": [\n\
    {\n\
      \"uuid\": [\n\
        \"0xfb\",\n\
        \"0x34\",\n\
        \"0x9b\",\n\
        \"0x5f\",\n\
        \"0x80\",\n\
        \"0x00\",\n\
        \"0x00\",\n\
        \"0x80\",\n\
        \"0x00\",\n\
        \"0x10\",\n\
        \"0x00\",\n\
        \"0x00\",\n\
        \"0xe7\",\n\
        \"0xfe\",\n\
        \"0x00\",\n\
        \"0x00\"\n\
      ],\n\
      \"sec_en\": 0,\n\
      \"characteristic\": [\n\
        {\n\
          \"uuid\": [\n\
            \"0xfb\",\n\
            \"0x34\",\n\
            \"0x9b\",\n\
            \"0x5f\",\n\
            \"0x80\",\n\
            \"0x00\",\n\
            \"0x00\",\n\
            \"0x80\",\n\
            \"0x00\",\n\
            \"0x10\",\n\
            \"0x00\",\n\
            \"0x00\",\n\
            \"0xc8\",\n\
            \"0xfe\",\n\
            \"0x00\",\n\
            \"0x00\"\n\
          ],\n\
          \"properties\": [\n\
            \"notification\",\n\
            \"indication\",\n\
            \"read\"\n\
          ],\n\
          \"max_len\": 200\n\
        }\n\
      ]\n\
    },\n\
    {\n\
      \"uuid\": [\n\
        \"0xfb\",\n\
        \"0x34\",\n\
        \"0x9b\",\n\
        \"0x5f\",\n\
        \"0x80\",\n\
        \"0x00\",\n\
        \"0x00\",\n\
        \"0x80\",\n\
        \"0x00\",\n\
        \"0x10\",\n\
        \"0x00\",\n\
        \"0x00\",\n\
        \"0xe7\",\n\
        \"0xff\",\n\
        \"0x00\",\n\
        \"0x00\"\n\
      ],\n\
      \"sec_en\": 0,\n\
      \"characteristic\": [\n\
        {\n\
          \"uuid\": [\n\
            \"0xfb\",\n\
            \"0x34\",\n\
            \"0x9b\",\n\
            \"0x5f\",\n\
            \"0x80\",\n\
            \"0x00\",\n\
            \"0x00\",\n\
            \"0x80\",\n\
            \"0x00\",\n\
            \"0x10\",\n\
            \"0x00\",\n\
            \"0x00\",\n\
            \"0xc8\",\n\
            \"0xff\",\n\
            \"0x00\",\n\
            \"0x00\"\n\
          ],\n\
          \"properties\": [\n\
            \"notification\",\n\
            \"indication\",\n\
            \"read\"\n\
          ],\n\
          \"max_len\": 200\n\
        }\n\
      ]\n\
    }\n\
  ]\n\
}\n\
";
#endif
uint8_t test_buff[] = {0xfb,0x34,0x9b,0x5f,0x80,0x00,0x00,0x80,0x00,0x10,0x00,0x00,0xc8,0xfe,0x00,0x00,\
                   0x00,0x22,0x33,0x44,0x55,0x11,0x22,0x33,0x44,0x55,0x11,0x22,0x33,0x44,0x55,0x11,0x22,0x33,0x44,0x55,\
                   0x11,0x22,0x33,0x44,0x55,0x11,0x22,0x33,0x44,0x55,0x11,0x22,0x33,0x44,0x55,0x11,0x22,0x33,0x44,0x55,\
                   0x11,0x22,0x33,0x44,0x55,0x11,0x22,0x33,0x44,0x55,0x11,0x22,0x33,0x44,0x55,0x11,0x22,0x33,0x44,0x55,\
                   0x11,0x22,0x33,0x44,0x55,0x11,0x22,0x33,0x44,0x55,0x11,0x22,0x33,0x44,0x55,0x11,0x22,0x33,0x44,0x55};
static volatile struct lbh_ble_env_t {
    volatile bool advertising;
    volatile bool scanning;
    volatile bool connected;
    volatile bool terminating;
} lbh_ble_env;
#if 0
void lbh_tx_test_thread( void *arg ){
    int test_round = 0;
    sleep(4);
    printf("lbh_tx_test_thread ready %p ", arg);
    //app_ble_adv_start_msg_send();
    printf("%s exit", __func__);
    sleep(4);
    app_ble_smartconfig_send_notification(test_buff,80);
    //app_ble_smartconfig_send_indication(test_buff,80);
    while(test_round < 10){
        sleep(4);
        app_ble_smartconfig_update_read_value(test_buff,20);
        test_buff[16]++;
        test_round++;
    }
    lbh_terminate();
}

void lbh_terminate()
{
    lbh_ble_env.terminating = true;
    if (lbh_ble_env.advertising) {
        app_ble_adv_stop_msg_send();
    }
    if (lbh_ble_env.scanning) {
        app_ble_scan_msg_stop();
    }
    if (lbh_ble_env.connected) {
        app_ble_disconnect_msg_send(0);
    }
    while (lbh_ble_env.advertising || lbh_ble_env.scanning || lbh_ble_env.connected) {
        sleep(1);
    }
    lbh_ble_env.terminating = false;
}
#endif
void client_queue_msg_callback(uint8_t *data, uint16_t length)
{
    int i = 0;
    BLE_HST_MSG msg;
    if(length>0){
        for(i = 3; i < length; i++){
            printf("%02X ", data[i]);
        }
        printf("\r\n");
        msg.msg_id = data[0];
        msg.len = length -3;
        msg.buff = data+3;
        switch(msg.msg_id ){
            case BLE_INIT_DONE:
                {
                    //uint8_t buff[64] = {0x0b,0x09,0x54,0x38,0x34,0x31,0x36,0x5f,0x78,0x78,0x78,0x78};
                    uint8_t scan_response_buff[] = {0x0F,0x09,0x6E,0x6F,0x6F,0x69,0x65,0x2D,0x30,0x30,0x30,0x62,0x2D,0x30,0x31,0x64};
                    uint8_t adv_data_buff[] = {0x18,0x21,0x00,0x50,0xEB,0x37,0x6F,0x26,0x15,0xB7,0x17,0x42,0x56,0x56,0x58,0x11,\
                                        0x3D,0x3E,0x0B,0x00,0x4F,0x4E,0x1D,0x00,0x00};
#ifdef CFG_AUTOMATED_TEST
                    uint8_t btaddr[6] = {0x90,0x91,0x92,0x93,0x94,0x95};
#else
                    uint8_t btaddr[6] = {0x00,0x01,0x02,0x03,0x04,0x05};
#endif
                    char ble_gap_name[] = "BLE_SMARTCONFIG_TEST_CASE";
                    printf("BLE  INITIATIVE  DONE\r\n");
                    app_ble_set_bt_addr(btaddr);
                    app_ble_set_sec_mode(1);
                    app_ble_gap_name_set_msg_send((uint8_t *)ble_gap_name,strlen(ble_gap_name));
                    app_ble_adv_param_set_msg_send(160,160,6);
                    app_ble_adv_data_set_msg_send(adv_data_buff,sizeof(adv_data_buff));
                    app_ble_scan_response_set_msg_send(scan_response_buff,sizeof(scan_response_buff));
                    //app_ble_set_scan_param(0x30,0xa0);
                    app_ble_set_defalut_mtu(512);

                    // app_ble_scan_msg_start();
                    // app_ble_set_lbh_print(0xf0000000);
                    app_ble_adv_start_msg_send();
                }
                break;
            case BLE_MSG_DONE:
                printf("BLE_CLEITN_MSG_SEND_DONE\r\n");
                break;
            case BLE_ADV_ENABLE:
                printf("BLE_ADV_ENABLE\r\n");
                lbh_ble_env.advertising = true;
                break;
            case BLE_ADV_DISABLE:
                printf("BLE_ADV_DISABLE\r\n");
                lbh_ble_env.advertising = false;
                break;
            case BLE_SCAN_ENABLE:
                printf("BLE_SCAN_ENABLE\r\n");
                lbh_ble_env.scanning = true;
                break;
            case BLE_SCAN_DISABLE:
                printf("BLE_SCAN_DISABLE\r\n");
                lbh_ble_env.scanning = false;
                break;
            case BLE_ADV_REPORT:
                {
                    struct gapm_ext_adv_report_ind *ptr = (struct gapm_ext_adv_report_ind *)msg.buff;

                    printf("BLE_ADV_REPORT\n");
                    printf("actv_idx %x, info %x\n",ptr->actv_idx,ptr->info);
                    printf("trans_addr.addr_type %x, addr %x,%x,%x,%x,%x,%x \n",ptr->trans_addr.addr_type,ptr->trans_addr.addr.addr[0],ptr->trans_addr.addr.addr[1]\
                        ,ptr->trans_addr.addr.addr[2],ptr->trans_addr.addr.addr[3],ptr->trans_addr.addr.addr[4],ptr->trans_addr.addr.addr[5]);
                    printf("target_addr.addr_type %x, addr %x,%x,%x,%x,%x,%x \n",ptr->target_addr.addr_type,ptr->target_addr.addr.addr[0],ptr->target_addr.addr.addr[1]\
                        ,ptr->target_addr.addr.addr[2],ptr->target_addr.addr.addr[3],ptr->target_addr.addr.addr[4],ptr->target_addr.addr.addr[5]);
                    printf("tx_pwr %x, rssi %d,phy_prim %x, phy_prim %x,phy_second %x,adv_sid %x,period_adv_intv %x\n"\
                        ,ptr->tx_pwr,ptr->rssi,ptr->phy_prim,ptr->phy_second,ptr->adv_sid,ptr->period_adv_intv);
                    printf("length %d,data %x,%x,%x,%x,%x,%x,%x\n",ptr->length,ptr->data[0],ptr->data[1],ptr->data[2],ptr->data[3],ptr->data[4],ptr->data[5],ptr->data[6]);
                }
                break;
            case BLE_SMARTCONFIG_DATA_RECV:
                {
#ifdef CFG_AUTOMATED_TEST
                    uint8_t exp_uuid[] = {0xfb,0x34,0x9b,0x5f,0x80,0x00,0x00,0x80,0x00,0x10,0x00,0x00,0xc7,0xfe,0x00,0x00};
                    uint8_t exp_data[] = "hello";
                    uint8_t ack[] = {0xfb,0x34,0x9b,0x5f,0x80,0x00,0x00,0x80,0x00,0x10,0x00,0x00,0xc8,0xfe,0x00,0x00, 'a', 'c', 'k', 0x00};

                    if (!memcmp(exp_uuid, msg.buff, 16)) {
                        if (!memcmp(exp_data, msg.buff + 16, 5)) {
                            uint8_t type = *(msg.buff + 16 + 5) - 0x30;

                            printf("type: %d\n", type);
                            ack[sizeof(ack) - 1] = type + 0x30;
                            switch (type) {
                                case 1:
                                    app_ble_smartconfig_send_indication(ack, 20);
                                    break;
                                case 2:
                                    app_ble_smartconfig_send_notification(ack, 20);
                                    break;
                                case 3:
                                    app_ble_smartconfig_update_read_value(ack, 20);
                                    break;
                                default:
                                    break;
                            }
                        }
                    }
#endif
                    uint8_t conidx=*msg.buff;

                    printf("BLE_SMARTCONFIG_DATA_RECV conidx: %d, uuid: ",conidx);
                    for (uint8_t i=0;i<16;i++) {
                        printf("%02x ",msg.buff[i+1]);
                    }
                    printf("\n");
                    printf("data: ");
                    for (uint16_t i=0;i<msg.len-17;i++){
                        printf("%02x ",msg.buff[i+17]);
                    }
                    printf("\n");
                }
                break;
            case BLE_CONNECTION_IND:
                {
                    uint8_t conidx = msg.buff[0];
                    struct ble_connection_ind *ptr = (struct ble_connection_ind *)(msg.buff+1) ;
                    printf("BLE_CONNECTION conhdl 0x%x,interval %d, peer_addr_type %d,addr: 0x%x,0x%x,0x%x,0x%x,0x%x,0x%x \r\n",
                        ptr->conhdl,ptr->con_interval,ptr->peer_addr_type,
                        ptr->peer_addr.addr[0],ptr->peer_addr.addr[1],ptr->peer_addr.addr[2],
                        ptr->peer_addr.addr[3],ptr->peer_addr.addr[4],ptr->peer_addr.addr[5]);
                    app_ble_set_data_len(conidx, 128,0x200);
                    lbh_ble_env.connected = true;  
                    if(0)//open for test notification or indication or update read value
                    {
                        // pthread_t lbh_tx_test_th;
                        // pthread_create( &lbh_tx_test_th, NULL, lbh_tx_test_thread, NULL);
                    }
                }
                break;
            case BLE_DISCONNECTED:
                {
                    struct ble_disconnect_ind *ptr = (struct ble_disconnect_ind *)msg.buff ;
                    printf("BLE_DISCONNECT conhdl 0x%x ,reason 0x%x \r\n",ptr->conhdl,ptr->reason);
                    lbh_ble_env.connected = false;
                    if (!lbh_ble_env.terminating) {
                        app_ble_adv_start_msg_send();
                    }
                }
                break;
            case BLE_CONNECTION_PARAM_UPDATE:
                {
                    struct ble_param_update_req_ind *ptr = (struct ble_param_update_req_ind *)msg.buff ;
                    printf("BLE_CONNECTION_PARAM_UPDATE intv_min 0x%x ,intv_max 0x%x, latency 0x%x,time_out 0x%x\r\n",
                        ptr->intv_min,ptr->intv_max,ptr->latency,ptr->time_out);
                }
                break;
            case BLE_EXCHANGED_MTU_IND:
                {
                    struct ble_mtu_changed_ind *ptr = (struct ble_mtu_changed_ind *)msg.buff ;
                    printf("BLE_EXCHANGED_MTU_IND mtu 0x%x ,seq_num 0x%x \r\n",
                        ptr->mtu,ptr->seq_num);
                }
                break;
            case BLE_PKT_SIZE_IND:
                {
                    struct ble_pkt_size_ind *ptr = (struct ble_pkt_size_ind *)msg.buff ;
                    printf("BLE_PKT_SIZE_IND max_rx_octets 0x%x ,max_rx_time 0x%x ,max_tx_octets 0x%x ,max_tx_time 0x%x\r\n",
                        ptr->max_rx_octets,ptr->max_rx_time,ptr->max_tx_octets,ptr->max_tx_time);
                }
                break;
            case BLE_SERVICE_CHANGED_IND:
                {
                    uint8_t conidx = *msg.buff;
                    uint8_t *uuid = msg.buff + 1;
                    uint8_t val = *(msg.buff + 17);

                    printf("BLE_SERVICE_CHANGED_IND\n");
                    if (val == 0x01) {
                        // notification
                        printf("notification enabled for ");
                        for (int i=0;i<16;i++){
                            printf("%02x ", uuid[i]);
                        }
                        printf("\n");
                    } else if (val == 0x02) {
                        // indication
                        printf("indication enabled for ");
                        for (int i=0;i<16;i++){
                            printf("%02x ", uuid[i]);
                        }
                        printf("\n");
                    }
                }
                break;
            case BLE_NTF_SENT_DONE:
                {
                    printf("BLE_NTF_SENT_DONE\r\n");
#ifndef CFG_AUTOMATED_TEST
                    app_ble_smartconfig_send_notification(test_buff,80);
#endif
                }
                break;
            case BLE_IND_SENT_DONE:
                {
                    uint8_t status = msg.buff[17];
                    printf("BLE_IND_SENT_DONE %d\r\n",status);
#ifndef CFG_AUTOMATED_TEST
                    if(status == 0)
                        app_ble_smartconfig_send_indication(test_buff,80);
#endif
                }
                break;
            case BLE_CON_UPDATE_CMP_DONE:
                {
                    struct ble_param_updated_ind *ptr = (struct ble_param_updated_ind *)msg.buff ;
                    printf("BLE_CON_UPDATE_CMP_DONE ,interval 0x%x, latency 0x%x, to 0x%x\r\n",ptr->con_interval,ptr->con_latency,ptr->sup_to);
                }
                break;
            case BLE_HOGPRH_DATA_RECV:
                {
                    struct hogprh_report_ind *ptr= (struct hogprh_report_ind *)msg.buff;
                    printf("BLE_HOGPRH_DATA_RECV: hid_idx %d,report_idx %d,report_len %d\n",ptr->hid_idx,ptr->report_idx,ptr->report.length);
                }
                break;
            case BLE_LTK_IND:
                {
                    struct ble_ltk_ind *ptr=(struct ble_ltk_ind *)msg.buff;
                    printf("BLE_LTK_IND\n");
                    printf("ltk: \n");
                    for (int i=0;i<16;i++){
                        printf("%02x ", ptr->ltk[i]);
                    }
                    printf("\n");
                    printf("ediv: %d\n", ptr->ediv);
                    printf("rand: \n");
                    for (int i=0;i<8;i++){
                        printf("%02x ", ptr->randnb[i]);
                    }
                    printf("\n");
                    printf("key size: %d\n", ptr->key_size);
                }
                break;
            case BLE_ADDR_IND:
                {
                    uint8_t *ptr=msg.buff;
                    printf("BLE_ADDR_IND\n");
                    for (int i=0;i<6;i++){
                        printf("%02x ", ptr[i]);
                    }
                    printf("\n");
                }
                break;
            case BLE_UDFC_DATABASE_IND:
                {
                    struct ble_db_svc_info *ptr=(struct ble_db_svc_info *)msg.buff;

                    printf("service uuid: ");
                    for (uint8_t i=0;i<16;i++){
                        printf("%02x ", ptr->uuid[i]);
                    }
                    printf("\n");
                    printf("num of chars: %d\n", ptr->num_of_char);
                    for (uint8_t i=0;i<ptr->num_of_char;i++){
                        struct ble_db_chac_info *ptr_char = &ptr->info[i];

                        printf("val uuid: ");
                        for (uint8_t j=0;j<16;j++){
                            printf("%02x ", ptr_char->val_uuid[j]);
                        }
                        printf("\n");                        

                        if(ptr_char->val_prop & 0x01){
                            printf("Value property : Boardcast Permitted\n");
                        }
                        if(ptr_char->val_prop & 0x02){
                            printf("Value property : Read Permitted\n");
                        }
                        if(ptr_char->val_prop & 0x04){
                            printf("Value property : Write Without Response Permitted\n");
                        }
                        if(ptr_char->val_prop & 0x08){
                            printf("Value property : Write Permitted\n");
                        }
                        if(ptr_char->val_prop & 0x10){
                            printf("Value property : Notify Permitted\n");
                        }
                        if(ptr_char->val_prop & 0x20){
                            printf("Value property : Indicate Permitted\n");
                        }
                        if(ptr_char->val_prop & 0x40){
                            printf("Value property : Authenticated Signed Writes Permitted\n");
                        }
                        if(ptr_char->val_prop & 0x80){
                            printf("Value property : Extended Properties Permitted\n");
                        }
                    }
                }
                break;
            case BLE_UDFC_NTF_DATA_RECV:
                {
                }
                break;
            case BLE_UDFC_IND_DATA_RECV:
                {
                }
                break;
            case BLE_UDFC_RD_IND:
                {
                }
                break;
            case BLE_RSSI_IND:
                {
                    int8_t rssi = (int8_t)msg.buff[0];
                    printf("BLE_RSSI_IND: %d\n", rssi);
                }
                break;
            case BLE_IRK_IND:
                {
                    struct ble_irk_ind *ptr=(struct ble_irk_ind *)msg.buff;
                    printf("BLE_IRK_IND\n");
                    printf("peer irk: \n");
                    for (int i=0;i<16;i++){
                        printf("%02x ", ptr->peer_irk[i]);
                    }
                    printf("\n");
                    printf("addr: %02x:%02x:%02x:%02x:%02x:%02x\n", ptr->addr[0],ptr->addr[1],ptr->addr[2],ptr->addr[3],ptr->addr[4],ptr->addr[5]);
                    printf("addr type: %d\n",ptr->addr_type);
                    printf("local irk: \n");
                    for (int i=0;i<16;i++){
                        printf("%02x ", ptr->local_irk[i]);
                    }
                    printf("\n");
                }
                break;
            case BLE_PSK_IND:
                {
                    uint32_t passkey = (uint32_t)msg.buff[0] | ((uint32_t)msg.buff[1] << 8) | ((uint32_t)msg.buff[2] << 16) | ((uint32_t)msg.buff[3] << 24);

                    printf("passkey is: %d\n", passkey);
                }
                break;
            default:
                break;
        }
    }
}

