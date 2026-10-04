#ifndef _LBH_BT_H_
#define _LBH_BT_H_

typedef enum {
    A2DP_IDLE,
    A2DP_CONNECTED,
    A2DP_PLAY,
}app_a2dp_state_t;


typedef uint16_t bt_avrcpPanelOp;

#define APP_AVRCP_PANEL_SELECT            0x0000
#define APP_AVRCP_PANEL_UP                0x0001
#define APP_AVRCP_PANEL_DOWN              0x0002
#define APP_AVRCP_PANEL_LEFT              0x0003
#define APP_AVRCP_PANEL_RIGHT             0x0004
#define APP_AVRCP_PANEL_RIGHT_UP          0x0005
#define APP_AVRCP_PANEL_RIGHT_DOWN        0x0006
#define APP_AVRCP_PANEL_LEFT_UP           0x0007
#define APP_AVRCP_PANEL_LEFT_DOWN         0x0008
#define APP_AVRCP_PANEL_ROOT_MENU         0x0009
#define APP_AVRCP_PANEL_SETUP_MENU        0x000A
#define APP_AVRCP_PANEL_CONTENTS_MENU     0x000B
#define APP_AVRCP_PANEL_FAVORITE_MENU     0x000C
#define APP_AVRCP_PANEL_EXIT              0x000D

#define APP_AVRCP_PANEL_0                 0x0020
#define APP_AVRCP_PANEL_1                 0x0021
#define APP_AVRCP_PANEL_2                 0x0022
#define APP_AVRCP_PANEL_3                 0x0023
#define APP_AVRCP_PANEL_4                 0x0024
#define APP_AVRCP_PANEL_5                 0x0025
#define APP_AVRCP_PANEL_6                 0x0026
#define APP_AVRCP_PANEL_7                 0x0027
#define APP_AVRCP_PANEL_8                 0x0028
#define APP_AVRCP_PANEL_9                 0x0029
#define APP_AVRCP_PANEL_DOT               0x002A
#define APP_AVRCP_PANEL_ENTER             0x002B
#define APP_AVRCP_PANEL_CLEAR             0x002C

#define APP_AVRCP_PANEL_CHANNEL_UP        0x0030
#define APP_AVRCP_PANEL_CHANNEL_DOWN      0x0031
#define APP_AVRCP_PANEL_PREVIOUS_CHANNEL  0x0032
#define APP_AVRCP_PANEL_SOUND_SELECT      0x0033
#define APP_AVRCP_PANEL_INPUT_SELECT      0x0034
#define APP_AVRCP_PANEL_DISPLAY_INFO      0x0035
#define APP_AVRCP_PANEL_HELP              0x0036
#define APP_AVRCP_PANEL_PAGE_UP           0x0037
#define APP_AVRCP_PANEL_PAGE_DOWN         0x0038

#define APP_AVRCP_PANEL_POWER             0x0040
#define APP_AVRCP_PANEL_VOLUME_UP         0x0041
#define APP_AVRCP_PANEL_VOLUME_DOWN       0x0042
#define APP_AVRCP_PANEL_MUTE              0x0043
#define APP_AVRCP_PANEL_PLAY              0x0044
#define APP_AVRCP_PANEL_STOP              0x0045
#define APP_AVRCP_PANEL_PAUSE             0x0046
#define APP_AVRCP_PANEL_RECORD            0x0047
#define APP_AVRCP_PANEL_REWIND            0x0048
#define APP_AVRCP_PANEL_FAST_FORWARD      0x0049
#define APP_AVRCP_PANEL_EJECT             0x004A
#define APP_AVRCP_PANEL_FORWARD           0x004B
#define APP_AVRCP_PANEL_BACKWARD          0x004C

#define APP_AVRCP_PANEL_ANGLE             0x0050
#define APP_AVRCP_PANEL_SUBPICTURE        0x0051

#define APP_AVRCP_PANEL_F1                0x0071
#define APP_AVRCP_PANEL_F2                0x0072
#define APP_AVRCP_PANEL_F3                0x0073
#define APP_AVRCP_PANEL_F4                0x0074
#define APP_AVRCP_PANEL_F5                0x0075

#define APP_AVRCP_PANEL_VENDOR_UNIQUE     0x007E

#define APP_AVRCP_PANEL_NEXT_GROUP        0x017E
#define APP_AVRCP_PANEL_PREV_GROUP        0x027E

#define APP_AVRCP_PANEL_RESERVED          0x007F

typedef uint8_t AvrcpMediaStatus;

#define AVRCP_MEDIA_STOPPED       0x00
#define AVRCP_MEDIA_PLAYING       0x01
#define AVRCP_MEDIA_PAUSED        0x02
#define AVRCP_MEDIA_FWD_SEEK      0x03
#define AVRCP_MEDIA_REV_SEEK      0x04
#define AVRCP_MEDIA_ERROR         0xFF

void client_queue_bt_msg_callback(uint8_t *data, uint16_t length);

#endif//_LBH_BT_H_

