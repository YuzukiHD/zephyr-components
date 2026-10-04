#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <pthread.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <net/if_arp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <stdbool.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include "queue_client.h"
#ifdef CONFIG_LBH_CODEC
#include "lsbc.h"
#include "aacdec.h"
#include "coder.h"
#endif //CONFIG_LBH_CODEC
#include <sys/stat.h>
#include "lbh_wav.h"
#include "lbh_codec.h"
#include "lbh_bt.h"

BT_ADDR connect_addr;
const char bt_local_name[32]="AIC_BTT";
//uint8_t tmptest[]={0xA4,0x2F,0xA6,0x3A,0xCE,0xD8,0x00,0x23,0x74,0x3D,0xD9,0x82,0xE7,0x7B,0xC1,0x56,0x06,0x95,0xD4,0x66,0xB8,0x4F,0xAE,0x04,0x01};

#ifdef CONFIG_LBH_CODEC
pthread_t lbh_a2dp_pcm_tx_test_th = NULL;
pthread_t lbh_a2dp_sink_avrcp_key_test_th = NULL;
#endif

app_a2dp_state_t t_a2dp_state = A2DP_IDLE;

app_a2dp_state_t app_get_a2dp_state(void)
{
    return t_a2dp_state;
}

void app_set_a2dp_state(app_a2dp_state_t state)
{
    if (t_a2dp_state == state){
        return;
    }
    printf("APP:set a2dp state = %d\n",state);
    t_a2dp_state = state;
}


void client_queue_bt_msg_callback(uint8_t *data, uint16_t length)
{
    int i = 0;
    BT_HST_MSG msg;
    if(length>0){
        msg.EventId = ((uint32_t)data[3]<<24) | ((uint32_t)data[2]<<16) | ((uint32_t)data[1]<<8) | ((uint32_t)data[0]);
        msg.len = length-6;
        msg.buff = data+6;
        if(msg.EventId != AIC_ADP_A2DP_STREAM_DATA_IND && msg.EventId != AIC_ADP_HFP_AUDIO_DATA_IND
        && msg.EventId != AIC_ADP_HFP_AUDIO_DATA_SENT && msg.EventId != AIC_ADP_A2DP_STREAM_PACKET_SENT){
            for(i = 0; i < length; i++){
                printf("%02X ", data[i]);
            }
            printf("\r\n");
        }
        switch(msg.EventId ){
            case AIC_ADP_STACK_INIT:
                {
                    printf("APP: bt_stack_init success.\n");
                    app_bt_set_name(bt_local_name, strlen(bt_local_name)+1);
                    //app_bt_trace_onoff(0,0xffffffff);
                    //app_bt_set_stored_database(tmptest,sizeof(tmptest));
                    app_bt_setscanmode(BT_ALLSCAN);
                    app_ble_set_lbh_print(0xf0000000);
#ifdef CONFIG_LBH_CODEC
                    app_codec_socket_init();
#endif//CONFIG_LBH_CODEC
                    //app_bt_inquiry_dev(8,8);
                }
                break;
            case AIC_ADP_ACCESSIBLE_CHANGE:
                {
                    uint8_t mode = msg.buff[0];
                    printf("APP:accessible mode = %d\n",mode);
                }
                break;
            case AIC_ADP_ENCRYPYION_CHANGE:
                printf("APP: AIC_ADP_ENCRYPYION_CHANGE.\n");
                break;
            case AIC_ADP_SAVE_DATABASE:
                {
                    BtDeviceRecord *records = NULL;
                    uint8_t i;
                    uint8_t num_record = msg.len/sizeof(BtDeviceRecord);
                    printf("APP: AIC_ADP_SAVE_DATABASE num_record = %d",num_record);
                    records = (BtDeviceRecord *)malloc(msg.len);
                    if(!records){
                        printf("APP: records malloc error");
                        break;
                    }
                    memcpy((void *)records,msg.buff,msg.len);
                    for(i=0;i<num_record;i++){
                        printf("record addr:");
                        printf("%x,%x,%x,%x,%x,%x",records[i].bdAddr.addr[0],records[i].bdAddr.addr[1],records[i].bdAddr.addr[2],\
                                                   records[i].bdAddr.addr[3],records[i].bdAddr.addr[4],records[i].bdAddr.addr[5]);
                        printf("record linkkey:");
                        printf("%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x",records[i].linkKey[0],records[i].linkKey[1],records[i].linkKey[2],records[i].linkKey[3],\
                                                                                 records[i].linkKey[4],records[i].linkKey[5],records[i].linkKey[6],records[i].linkKey[7],\
                                                                                 records[i].linkKey[8],records[i].linkKey[9],records[i].linkKey[10],records[i].linkKey[11],\
                                                                                 records[i].linkKey[12],records[i].linkKey[13],records[i].linkKey[14],records[i].linkKey[15]);
                    }
                    free(records);
                }
                break;
/*********************************************************************/
            case AIC_ADP_ACL_CONNECT_IND:
                {
                    uint8_t *p = msg.buff;
                    uint8_t connect_index = p[0];
                    bt_err_type reason = p[1];
                    uint8_t peer_addr[6];
                    //app_bt_setscanmode(BT_NOSCAN);
                    peer_addr[0] = p[2];
                    peer_addr[1] = p[3];
                    peer_addr[2] = p[4];
                    peer_addr[3] = p[5];
                    peer_addr[4] = p[6];
                    peer_addr[5] = p[7];
                    printf("APP: AIC_ADP_ACL_CONNECT_IND.conidx %d, reason %d, addr: 0x%x,0x%x,0x%x,0x%x,0x%x,0x%x\n",connect_index,reason,peer_addr[0],\
                        peer_addr[1],peer_addr[2],peer_addr[3],peer_addr[4],peer_addr[5]);
                }
                break;
            case AIC_ADP_ACL_CONNECT_CNF:
                {
                    uint8_t *p = msg.buff;
                    uint8_t connect_index = p[0];
                    bt_err_type reason = p[1];
                    uint8_t peer_addr[6];
                    //app_bt_setscanmode(BT_NOSCAN);
                    peer_addr[0] = p[2];
                    peer_addr[1] = p[3];
                    peer_addr[2] = p[4];
                    peer_addr[3] = p[5];
                    peer_addr[4] = p[6];
                    peer_addr[5] = p[7];
                    printf("APP: AIC_ADP_ACL_CONNECT_CNF.conidx %d, reason %d, addr: 0x%x,0x%x,0x%x,0x%x,0x%x,0x%x\n",connect_index,reason,peer_addr[0],\
                        peer_addr[1],peer_addr[2],peer_addr[3],peer_addr[4],peer_addr[5]);
                    switch(reason){
                        case BT_NO_ERROR:
                            break;
                        case BT_PAGE_TIMEOUT:
                            
                            //app_bt_source_connect_a2dp(&connect_addr);
                            break;
                        default:
                            break;
                    }
                }
                break;
            case AIC_ADP_ACL_DISCONNECT:
                {
                    uint8_t *p = msg.buff;
                    bt_err_type reason = p[0];
                    uint8_t connect_index = p[1];
                    uint8_t peer_addr[6];
                    peer_addr[0] = p[2];
                    peer_addr[1] = p[3];
                    peer_addr[2] = p[4];
                    peer_addr[3] = p[5];
                    peer_addr[4] = p[6];
                    peer_addr[5] = p[7];
                    printf("APP:acl_disconnect reason 0x%x\n",reason);

                }
                break;
            case AIC_ADP_PAIRING_COMPLETE:
                {
                    uint8_t *p = msg.buff;
                    bt_err_type reason = p[0];
                    printf("APP: AIC_ADP_PAIRING_COMPLETE. %d\n",reason);


                }
                break;
            case AIC_ADP_SCO_CONNECT_IND:
                printf("APP: AIC_ADP_SCO_CONNECT_IND.\n");
                break;
            case AIC_ADP_SCO_CONNECT_CNF:
                printf("APP: AIC_ADP_SCO_CONNECT_CNF.\n");
                break;
            case AIC_ADP_SCO_DISCONNECTED:
                printf("APP: AIC_ADP_SCO_DISCONNECTED.\n");

                break;
            case AIC_ADP_INQUIRY_RESULT:
                {
                    uint8_t *p = msg.buff;
                    uint8_t tem_addr[6]={0xaa,0xc0,0x00,0x88,0x88,0x33};
                    //uint8_t tem_addr[6]={0xac,0x5f,0xe8,0xfb,0x42,0x41};
                    //uint8_t tem_addr[6]={0x65,0xb8,0xe4,0x11,0x11,0x12};
                    uint8_t addr[6];
                    uint32_t cod = (uint32_t)p[0]|(((uint32_t)p[1])<<8)|(((uint32_t)p[2])<<16)|(((uint32_t)p[3])<<24);
                    memcpy(addr,p+4,6);
                    printf("APP: aic_adp_inquiry_result. cod = 0x%x,addr 0x%x,0x%x,0x%x,0x%x,0x%x,0x%x\n",cod,\
                        addr[5],addr[4],addr[3],addr[2],addr[1],addr[0]);
                    uint8_t inqMode = (uint8_t)p[10];
                    signed char rssi = (signed char)p[11];
                    printf("APP: bt_inquiry_mode: %d,rssi = %d\n",inqMode,rssi);
                    if(inqMode == 2){
                        char name[240];
                        uint8_t len = 0;
                        uint8_t type = 0;
                        uint8_t *eir = &p[i+12];
                        for(uint8_t idx = 0; idx < 240; idx++){
                            len = eir[idx];
                            type = eir[idx+1];
                            if(type==0x09){
                                memcpy((uint8_t *)name,&eir[idx+2],len-1);
                                name[len-1] = '\0';
                                printf("APP: complete_name = %s\n",name);
                            }
                            idx += len;
                        }
                    }
                    if(!memcmp(addr,tem_addr,6)){
                        memcpy(connect_addr.addr,addr,6);
                        app_bt_inquiry_cancel();
                        app_bt_source_connect_a2dp(&connect_addr);
                    }
                }
                break;
            case AIC_ADP_INQUIRY_COMPLETE:
                printf("APP: aic_adp_inquiry_complete.\n");
                break;
                /** The Inquiry process is canceled. */
            case AIC_ADP_INQUIRY_CANCELED:
                printf("APP: aic_adp_inquiry_canceled.\n");
                break;
            case AIC_ADP_REMOTE_NAME_RES:
                {
                    uint8_t *p = msg.buff;
                    uint8_t addr[6];
                    uint8_t len;
                    char name[251];
                    memset(name,0,251);
                    memcpy(addr,p,6);
                    len = p[6];
                    printf("APP:remote name addr:");
                    printf("0x%x,0x%x,0x%x,0x%x,0x%x,0x%x.\n ",addr[0],addr[1],addr[2],addr[3],addr[4],addr[5]);
                    printf("APP:remote name len %d : \n",len);
                    if(len){
                        memcpy((uint8_t *)name,p+7,len);
                        printf("%s",name);
                    }
                }
                break;
/*********************************************************************/
            case AIC_ADP_A2DP_STREAM_OPEN:
                {
                    uint8_t *p = msg.buff;
                    uint8_t a2dp_role = p[0];
                    uint8_t codec_type = p[1];
                    uint8_t sample_rate_bit = p[2];
                    uint8_t sbc_frame_number = p[3];
                    uint8_t channel = p[4];
                    uint8_t numBlocks = p[5];
                    uint8_t numSubBands = p[6];
                    uint8_t maxbitpool = p[7];
                    uint8_t addr[6];
                    addr[0] = p[8];
                    addr[1] = p[9];
                    addr[2] = p[10];
                    addr[3] = p[11];
                    addr[4] = p[12];
                    addr[5] = p[13];
                    printf("APP: aic_adp_a2dp_stream_open.\n");
                    printf("APP: a2dp_role %d,codec_type %d,sample_rate_bit %d,sbc_frame_number %d,channel %d,numBlocks %d,numSubBands %d,maxbitpool %d\n",\
                                a2dp_role,codec_type,sample_rate_bit,sbc_frame_number,channel,numBlocks,numSubBands,maxbitpool);
#ifdef CONFIG_LBH_CODEC
		    app_codec_client_msg_send(&msg,RX_DATA_MODE);
#endif //CONFIG_LBH_CODEC
                    app_set_a2dp_state(A2DP_CONNECTED);
                    app_bt_a2dp_start(&connect_addr,1);
                }
                break;
            case AIC_ADP_A2DP_STREAM_STARTED:
                {
                    uint8_t *p = msg.buff;
                    uint8_t a2dp_role = p[0];
                    uint8_t addr[6];
                    addr[0] = p[1];
                    addr[1] = p[2];
                    addr[2] = p[3];
                    addr[3] = p[4];
                    addr[4] = p[5];
                    addr[5] = p[6];
                    printf("APP: aic_adp_a2dp_stream_started.a2dp_role %d\n",a2dp_role);
                    app_ble_set_lbh_print(0xf0000000);
                    app_bt_trace_onoff(0,0xffffffff);
                    app_set_a2dp_state(A2DP_PLAY);
#ifdef CONFIG_LBH_CODEC
                    if(a2dp_role == 0)//a2dp source
                    {
                        if(lbh_a2dp_pcm_tx_test_th == NULL)
                            pthread_create( &lbh_a2dp_pcm_tx_test_th, NULL, lbh_a2dp_pcm_data_tx_test_thread, NULL);
                    }else{// a2dp sink
                        if(lbh_a2dp_sink_avrcp_key_test_th == NULL)
                            pthread_create( &lbh_a2dp_sink_avrcp_key_test_th, NULL, lbh_a2dp_sink_avrcp_key_test_thread, NULL);
                    }
#endif //CONFIG_LBH_CODEC
                }
                break;
            case AIC_ADP_A2DP_STREAM_SUSPENDED:
                {
                    uint8_t *p = msg.buff;
                    uint8_t a2dp_role = p[0];
                    uint8_t addr[6];
                    addr[0] = p[1];
                    addr[1] = p[2];
                    addr[2] = p[3];
                    addr[3] = p[4];
                    addr[4] = p[5];
                    addr[5] = p[6];
                    printf("APP: aic_adp_a2dp_stream_suspended.\n");
                    app_ble_set_lbh_print(0xffffffff);
                    app_bt_trace_onoff(1,0xffffffff);
                    app_set_a2dp_state(A2DP_CONNECTED);
                }
                break;
            case AIC_ADP_A2DP_STREAM_CLOSED:
                {
                    uint8_t *p = msg.buff;
                    uint8_t a2dp_role = p[0];
                    uint8_t addr[6];
                    addr[0] = p[1];
                    addr[1] = p[2];
                    addr[2] = p[3];
                    addr[3] = p[4];
                    addr[4] = p[5];
                    addr[5] = p[6];
                    printf("APP: aic_adp_a2dp_stream_closed.\n");
                    app_ble_set_lbh_print(0xffffffff);
                    app_bt_trace_onoff(1,0xffffffff);
#ifdef CONFIG_LBH_CODEC
                    app_codec_client_msg_send(&msg,RX_DATA_MODE);
#endif //CONFIG_LBH_CODEC
                    app_set_a2dp_state(A2DP_IDLE);
                }
                break;
            case AIC_ADP_A2DP_STREAM_PACKET_SENT:
                {
                    //printf("APP:a2dp source data send succ!\n");
                }
                break;
            case AIC_ADP_A2DP_STREAM_DATA_IND:
                {
                    uint8_t *p = msg.buff;
                    uint16_t a2dp_headerlen = (uint16_t)p[0]|(((uint16_t)p[1])<<8);
                    uint8_t *p_data = &p[2+a2dp_headerlen];
                    uint16_t p_len = msg.len-2-a2dp_headerlen;
                    //printf("APP: aic_adp_a2dp_stream_data_ind. len = %d\n",p_len);
#ifdef CONFIG_LBH_CODEC
                    app_codec_client_msg_send(&msg,RX_DATA_MODE);
#endif //CONFIG_LBH_CODEC
                }
                break;
/*********************************************************************/
            case AIC_ADP_AVRCP_CONNECT:
                {
                    uint8_t *p = msg.buff;
                    uint8_t addr[6];
                    addr[0] = p[0];
                    addr[1] = p[1];
                    addr[2] = p[2];
                    addr[3] = p[3];
                    addr[4] = p[4];
                    addr[5] = p[5];
                    printf("APP: aic_adp_avrcp_connect.\n");
                }
                break;
            case AIC_ADP_AVRCP_DISCONNECT:
                {
                    uint8_t *p = msg.buff;
                    uint8_t addr[6];
                    addr[0] = p[0];
                    addr[1] = p[1];
                    addr[2] = p[2];
                    addr[3] = p[3];
                    addr[4] = p[4];
                    addr[5] = p[5];
                    printf("APP:app_avrcp_disconnect\n");
                }
                break;
            case AIC_ADP_AVRCP_PANEL_PRESS:
                {
                    uint8_t *p = msg.buff;
                    bt_avrcpPanelOp op = ((uint16_t)p[0] | (uint16_t)p[1]<<8);
                    printf("APP: aic_adp_avrcp_panel_press.0x%x\n",op);
                    switch(op)
                    {
                        case APP_AVRCP_PANEL_VOLUME_UP:
                            printf("APP:avrcp_key_volume_up\n");
                            break;
                        case APP_AVRCP_PANEL_VOLUME_DOWN:
                            printf("APP: avrcp_key_volume_down\n");
                            break;
                        case APP_AVRCP_PANEL_PLAY:
                            printf("APP:avrcp start stream \n");
                            app_bt_a2dp_start(&connect_addr,1);
                            break;
                        case APP_AVRCP_PANEL_PAUSE:
                            printf("APP:acrcp stop stream \n");
                            app_bt_a2dp_start(&connect_addr,0);
                            break;
                        default :
                            break;
                    }
                }
                break;
            case AIC_ADP_AVRCP_ADV_VOLUME_CHANGE:
                {
                    uint8_t *p = msg.buff;
                    uint8_t volume = p[0];
                    printf("APP: aic_adp_avrcp_adv_volume_change %d.\n",volume);
                }
                break;
            case AIC_ADP_AVRCP_ADV_PLAYSTAUTS_CHANGE:
                {
                    uint8_t *p = msg.buff;
                    AvrcpMediaStatus playstauts = p[0];
                    printf("APP: aic_adp_avrcp_adv_playstauts_change %d.\n",playstauts);
                }
                break;
/*********************************************************************/
            case AIC_ADP_HFP_CONNECTED:
                {
                    uint8_t *p = msg.buff;
                    uint8_t role = p[0];
                    uint8_t result = p[1];
                    printf("APP:app_hfp_connect %d,%d\n",role,result);
                }
                break;
            case AIC_ADP_HFP_DISCONNECTED:
                {
                    uint8_t *p = msg.buff;
                    uint8_t role = p[0];
                    uint8_t discReason = p[1];
                    printf("APP:app_hfp_disconnect %d,%d\n",role,discReason);
                }
                break;
            case AIC_ADP_HFP_AUDIO_CONNECTED:
                {
                    uint8_t *p = msg.buff;
                    uint8_t role = p[0];
                    uint16_t negotiated_codec = (uint16_t)p[1]|(((uint16_t)p[2])<<8);
                    printf("APP:hfp_sco_connect %d,%d\n",role,negotiated_codec);
#ifdef CONFIG_LBH_CODEC
                    app_codec_client_msg_send(&msg,RX_DATA_MODE);
#endif //CONFIG_LBH_CODEC
                }
                break;
            case AIC_ADP_HFP_AUDIO_DISCONNECTED:
                {
                    uint8_t *p = msg.buff;
                    uint8_t role = p[0];
                    printf("APP: aic_adp_hfp_audio_disconnected.%d\n",role);
#ifdef CONFIG_LBH_CODEC
                    app_codec_client_msg_send(&msg,RX_DATA_MODE);
#endif //CONFIG_LBH_CODEC
                }
                break;
            case AIC_ADP_HFP_AUDIO_DATA_SENT:
                {
                    uint8_t *p = msg.buff;
                    uint8_t role = p[0];
                    //printf("APP: aic_adp_hfp_audio_data_sent %d.\n",role);
                }
                break;
            case AIC_ADP_HFP_AUDIO_DATA_IND:
                {
                    uint8_t *p = msg.buff;
                    uint8_t p_len = msg.len;
                    printf("APP: aic_adp_hfp_audio_data_ind len %d.\n",p_len);
#ifdef CONFIG_LBH_CODEC
                    app_codec_client_msg_send(&msg,RX_DATA_MODE);
#endif //CONFIG_LBH_CODEC
                }
                break;
            case AIC_ADP_HFP_CALL_STATUS_IND:
                {
                    uint8_t *p = msg.buff;
                    uint32_t call_state = (uint32_t)p[0]|(((uint32_t)p[1])<<8)|(((uint32_t)p[2])<<16)|(((uint32_t)p[3])<<24);
                    printf("APP: aic_adp_hfp_call_status_ind call_state=0x%x.\n",call_state);
                }
                break;
            case AIC_ADP_HFP_CALLSETUP_STATUS_IND:
                {
                    uint8_t *p = msg.buff;
                    uint32_t call_state = (uint32_t)p[0]|(((uint32_t)p[1])<<8)|(((uint32_t)p[2])<<16)|(((uint32_t)p[3])<<24);
                    printf("APP: aic_adp_hfp_callsetup_status_ind call_state=0x%x.\n",call_state);
                }
                break;
            case AIC_ADP_HFP_CALLHOLD_STATUS_IND:
                {
                    uint8_t *p = msg.buff;
                    uint8_t hfp_call_hold = p[0];
                    printf("APP: aic_adp_hfp_callhold_status_ind hfp_call_hold=0x%x.\n",hfp_call_hold);
                }
                break;
            case AIC_ADP_HFP_RING_IND:
                {
                    printf("APP: aic_adp_hfp_ring_ind.\n");
                }
                break;
            case AIC_ADP_HFP_SIRI_STATUS:
                {
                    uint8_t *p = msg.buff;
                    uint8_t siriStatus = p[0];
                    printf("APP: aic_adp_hfp_siri_status siriStatus=0x%x.\n",siriStatus);
                }
                break;
            case AIC_ADP_HFP_CURRENT_CALL_NUM:
                {
                    uint8_t *p = msg.buff;
                    char call_num[32];
                    memcpy((uint8_t *)call_num,p,32);
                    printf("APP: aic_adp_hfp_current_call_num %s.\n",call_num);
                }
                break;
            case AIC_ADP_HFP_SPEAKER_VOLUME:
                {
                    uint8_t *p = msg.buff;
                    uint8_t volume = p[0];
                    printf("APP: aic_adp_hfp_speaker_volume %d.\n",volume);
                }
                break;
/*********************************************************************/
            case AIC_ADP_SPP_CONNECTED_IND:
                {
                    uint8_t *p = msg.buff;
                    uint8_t result = p[0];
                    if(result == 0){
                        printf("APP:spp connect ind\n");
                    }
                }
                break;
            case AIC_ADP_SPP_CONNECTED_CNF:
                {
                    uint8_t *p = msg.buff;
                    uint8_t result = p[0];
                    if(result == 0){
                        printf("APP:spp connect cnf\n");
                    }
                }
            break;
            case AIC_ADP_SPP_DISCONNECTED:
                {
                    uint8_t *p = msg.buff;
                    uint8_t result = p[0];
                    printf("APP:spp disconnected %d",result);
                }
            break;
            case AIC_ADP_SPP_DATA_SENT:
                {
                    uint8_t *p = msg.buff;
                    uint16_t txdonelen = msg.len;
                    printf("APP: aic_adp_spp_data_sent txdonelen=%d.\n",txdonelen);
                }
            break;
            case AIC_ADP_SPP_DATA_IND:
                {
                    uint8_t *p = msg.buff;
                    uint16_t rxlen = msg.len;
                    printf("APP: aic_adp_spp_data_ind rxlen=%d.\n",rxlen);
                }
            break;

            default:
                break;
        }
    }
}

