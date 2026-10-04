#ifndef LBH_CODEC_H_
#define LBH_CODEC_H_
#include "queue_client.h"


#define APP_A2DP_TEST_PCM_FILE  "./48.pcm"
#define APP_A2DP_TEST_WAV_FILE  "./a2dp.wav"
#define APP_HFP_TEST_16K_FILE  "./16K.pcm"
#define APP_HFP_TEST_8K_FILE  "./8K.pcm"
#define APP_HFP_OUT_PUT_FILE "./sco.wav"

enum APP_CODEC_MSG_TYPE{
    TX_DATA_MODE = 0x01,
    RX_DATA_MODE,
};

enum CODEC_MODE{
    SBC_MODE,
    AAC_MODE,
};

enum SCO_CODEC_MODE{
    CVSD_MODE,
    MSBC_MODE,
};

void* lbh_a2dp_sink_avrcp_key_test_thread( void *arg );
void* lbh_a2dp_pcm_data_tx_test_thread( void *arg );
void* lbh_a2dp_wav_data_tx_test_thread( void *arg );


int app_codec_client_msg_send(BT_HST_MSG* msg,enum APP_CODEC_MSG_TYPE codec_msg_type);
void app_codec_send_sco_pcm(uint8_t *data, uint16_t length);
void app_codec_send_a2dp_pcm(uint8_t *data, uint16_t length);
void* app_codec_thread( void *arg );
void app_codec_socket_init(void);

#endif//LBH_CODEC_H_
