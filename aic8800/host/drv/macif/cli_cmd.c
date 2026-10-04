#include "cli_cmd.h"
#include <stdio.h>
#include <string.h>
#include "porting.h"
#include "fhost_config.h"
#include "rtos_al.h"
#include "rwnx_utils.h"
#include "rwnx_defs.h"
#include "wifi.h"
#include "wlan_if.h"
#include "fhost_cntrl.h"
#include "fhost.h"

//#include <errno.h>
#include "rtos_errno.h"
#include "queue_client.h"

#define CMD_MAXARGS 30
#define POWER_LEVEL_INVALID_VAL     (127)
extern struct rwnx_hw *g_rwnx_hw;

extern struct aic_ap_info g_ap_info;
extern int ap_net_id;

#define TRUE  1
#define FALSE 0

enum {
    SET_TX,
    SET_TXSTOP,
    SET_TXTONE,
    SET_RX,
    GET_RX_RESULT,
    SET_RXSTOP,
    SET_RX_METER,
    SET_POWER,
    SET_XTAL_CAP,
    SET_XTAL_CAP_FINE,
    GET_EFUSE_BLOCK,
    SET_FREQ_CAL,
    SET_FREQ_CAL_FINE,
    GET_FREQ_CAL,
    SET_MAC_ADDR,
    GET_MAC_ADDR,
    SET_BT_MAC_ADDR,
    GET_BT_MAC_ADDR,
    SET_VENDOR_INFO,
    GET_VENDOR_INFO,
    RDWR_PWRMM,
    RDWR_PWRIDX,
    RDWR_PWRLVL = RDWR_PWRIDX,
    RDWR_PWROFST,
    RDWR_DRVIBIT,
    RDWR_EFUSE_PWROFST,
    RDWR_EFUSE_DRVIBIT,
    SET_PAPR,
    SET_CAL_XTAL,
    GET_CAL_XTAL_RES,
    SET_COB_CAL,
    GET_COB_CAL_RES,
    RDWR_EFUSE_USRDATA,
    SET_NOTCH,
    RDWR_PWROFSTFINE,
    RDWR_EFUSE_PWROFSTFINE,
    RDWR_EFUSE_SDIOCFG,
    RDWR_EFUSE_USBVIDPID,
	SET_SRRC,
	SET_FSS,
	RDWR_EFUSE_HE_OFF,
	SET_USB_OFF,
	SET_PLL_TEST,
	SET_ANT_MODE,
	RDWR_BT_EFUSE_PWROFST,
    #ifdef CONFIG_USB_BT
    BT_CMD_BASE = 0x100,
    BT_RESET,
    BT_TXDH,
    BT_RXDH,
    BT_STOP,
    GET_BT_RX_RESULT,
    #endif
};

typedef struct
{
    u8_l chan;
    u8_l bw;
    u8_l mode;
    u8_l rate;
    u16_l length;
    u16_l tx_intv_us;
    s8_l max_pwr;
}cmd_rf_settx_t;

typedef struct
{
    u8_l val;
}cmd_rf_setfreq_t;

typedef struct
{
    u8_l chan;
    u8_l bw;
}cmd_rf_rx_t;

typedef struct
{
    u8_l block;
}cmd_rf_getefuse_t;

#if 0
typedef struct
{
    struct co_list_hdr hdr;
    char cmd_buf[0];
}cli_cmd_t;

static struct co_list cli_cmd_list;
static rtos_mutex cli_cmd_mutex = NULL;

int Cli_RunCmd(char *CmdBuffer)
{
    AIC_LOG_PRINTF("cli cmd: %s\n", CmdBuffer);

    uint32_t cli_cmd_len = sizeof(cli_cmd_t) + strlen(CmdBuffer) + 1;
    cli_cmd_t *cli_cmd = rtos_malloc(cli_cmd_len);
    if (cli_cmd == NULL) {
        AIC_LOG_ERROR("cli_cmd malloc fail\n");
        return -1;
    }
    memset(cli_cmd, 0, cli_cmd_len);
    strcpy(cli_cmd->cmd_buf, CmdBuffer);
    rtos_mutex_lock(cli_cmd_mutex, -1);
    co_list_push_back(&cli_cmd_list, &cli_cmd->hdr);
    rtos_mutex_unlock(cli_cmd_mutex);
    AIC_LOG_DEBUG("put cli cmd to list\n");

    rtos_semaphore_signal(g_rwnx_hw->cli_cmd_sema, false);

    return 0;
}

int aic_cli_run_cmd(char *CmdBuffer)
{
    return Cli_RunCmd(CmdBuffer);
}

void cli_cmd_task(void *argv)
{
    struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)argv;
    int ret = 0;

    AIC_LOG_PRINTF("cli_cmd_task\n");
    while (1) {
        int32_t ret = rtos_semaphore_wait(rwnx_hw->cli_cmd_sema, -1);
        AIC_LOG_DEBUG("cli cmd sema wait: ret=%d\n", ret);
        rtos_mutex_lock(cli_cmd_mutex, -1);
        cli_cmd_t *cli_cmd = (cli_cmd_t *)co_list_pop_front(&cli_cmd_list);
        rtos_mutex_unlock(cli_cmd_mutex);
        if (cli_cmd == NULL) {
            AIC_LOG_ERROR("cli cmd list empty\n");
            continue;
        }
        AIC_LOG_DEBUG("cli cmd check again: %s\n", cli_cmd->cmd_buf);
        ret = handle_private_cmd(rwnx_hw, cli_cmd->cmd_buf);
        rtos_free(cli_cmd);
        if (ret < 0) {
            AIC_LOG_ERROR("handle_private_cmd fail: ret=%d\n", ret);
        }
    }
}

bool aic_cli_cmd_init(struct rwnx_hw *rwnx_hw)
{
    int ret = 0;

    AIC_LOG_PRINTF("aic_cli_cmd_init\n");

    rtos_semaphore_create(&rwnx_hw->cli_cmd_sema, "rwnx_hw->cli_cmd_sema", 1024, 0);
    if (rwnx_hw->cli_cmd_sema == NULL) {
        AIC_LOG_ERROR("aic cli cmd sema create fail\n");
        return FALSE;
    }

    rtos_mutex_create(&cli_cmd_mutex, "cli_cmd_mutex");
    if (cli_cmd_mutex == NULL) {
        AIC_LOG_ERROR("cli_cmd_mutex create fail\n");
        return FALSE;
    }

    ret = rtos_task_create(cli_cmd_task, "cli_cmd_task", CLI_CMD_TASK,
                           cli_cmd_stack_size, (void*)rwnx_hw, cli_cmd_priority,
                           &rwnx_hw->cli_cmd_task_hdl);
    if (ret || (rwnx_hw->cli_cmd_task_hdl == NULL)) {
        AIC_LOG_ERROR("aic cli cmd task create fail\n");
        return FALSE;
    }

    co_list_init(&cli_cmd_list);

    return TRUE;
}

bool aic_cli_cmd_deinit(struct rwnx_hw *rwnx_hw)
{
    int ret = 0;

    AIC_LOG_PRINTF("aic_cli_cmd_deinit\n");

    rtos_task_delete(rwnx_hw->cli_cmd_task_hdl);
    rtos_semaphore_delete(rwnx_hw->cli_cmd_sema);
    rtos_mutex_delete(cli_cmd_mutex);
    while (co_list_cnt(&cli_cmd_list)) {
        cli_cmd_t *cli_cmd = (cli_cmd_t *)co_list_pop_front(&cli_cmd_list);
        if (cli_cmd) {
            rtos_free(cli_cmd);
        }
    }

    return TRUE;
}
#else
char *g_cli_cmd = NULL;

int Cli_RunCmd(char *CmdBuffer)
{
	AIC_LOG_PRINTF("AIC cmd: %s\n", CmdBuffer);

	g_cli_cmd = CmdBuffer;
	rtos_semaphore_signal(g_rwnx_hw->cli_cmd_sema, false);
}

int aic_cli_run_cmd(char *CmdBuffer)
{
    return Cli_RunCmd(CmdBuffer);
}

void cli_cmd_task(void *argv)
{
    struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)argv;
	int ret = 0;

    AIC_LOG_PRINTF("cli_cmd_task\n");
    while (1) {
		int32_t ret = rtos_semaphore_wait(rwnx_hw->cli_cmd_sema, -1);
		AIC_LOG_PRINTF("aft cli cmd sema\n");
		if (ret < 0) {
            AIC_LOG_PRINTF("cli cmd trigg fail: ret=%d\n", ret);
        }
		AIC_LOG_PRINTF("cli cmd check again: %s\n", g_cli_cmd);
		ret = handle_private_cmd(rwnx_hw, g_cli_cmd);
		if (ret < 0) {
			AIC_LOG_PRINTF("handle_private_cmd fail: ret=%d\n", ret);
		}
	}
}

bool aic_cli_cmd_init(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;

	AIC_LOG_PRINTF("aic_cli_cmd_init\n");

	rtos_semaphore_create(&rwnx_hw->cli_cmd_sema, "rwnx_hw->cli_cmd_sema", 1, 0);
    if (rwnx_hw->cli_cmd_sema == NULL) {
        AIC_LOG_PRINTF("aic cli cmd sema create fail\n");
        return FALSE;
    }
    ret = rtos_task_create(cli_cmd_task, "cli_cmd_task", CLI_CMD_TASK,
                           cli_cmd_stack_size, (void*)rwnx_hw, cli_cmd_priority,
                           &rwnx_hw->cli_cmd_task_hdl);
    if (ret || (rwnx_hw->cli_cmd_task_hdl == NULL)) {
        AIC_LOG_PRINTF("aic cli cmd task create fail\n");
        return FALSE;
    }

	return TRUE;
}

bool aic_cli_cmd_deinit(struct rwnx_hw *rwnx_hw)
{
    int ret = 0;

    AIC_LOG_PRINTF("aic_cli_cmd_deinit\n");

	rtos_task_delete(rwnx_hw->cli_cmd_task_hdl);
    rtos_semaphore_delete(rwnx_hw->cli_cmd_sema);
	g_cli_cmd = NULL;

    return TRUE;
}
#endif

static int parse_line (char *line, char *argv[])
{
    int nargs = 0;

    while (nargs < CMD_MAXARGS) {
        /* skip any white space */
        while ((*line == ' ') || (*line == '\t')) {
            ++line;
        }

        if (*line == '\0') {    /* end of line, no more args    */
            argv[nargs] = 0;
            return (nargs);
        }

        /* Argument include space should be bracketed by quotation mark */
        if (*line == '\"') {
            /* Skip quotation mark */
            line++;

            /* Begin of argument string */
            argv[nargs++] = line;

            /* Until end of argument */
            while(*line && (*line != '\"')) {
                ++line;
            }
        } else {
            argv[nargs++] = line;    /* begin of argument string    */

            /* find end of string */
            while(*line && (*line != ' ') && (*line != '\t')) {
                ++line;
            }
        }

        if (*line == '\0') {    /* end of line, no more args    */
            argv[nargs] = 0;
            return (nargs);
        }

        *line++ = '\0';         /* terminate current arg     */
    }

    printk("** Too many args (max. %d) **\n", CMD_MAXARGS);

    return (nargs);
}

unsigned int command_strtoul(const char *cp, char **endp, unsigned int base)
{
    unsigned int result = 0, value, is_neg=0;

    if (*cp == '0') {
        cp++;
        if ((*cp == 'x') && isxdigit(cp[1])) {
            base = 16;
            cp++;
        }
        if (!base) {
            base = 8;
        }
    }
    if (!base) {
        base = 10;
    }
    if (*cp == '-') {
        is_neg = 1;
        cp++;
    }
    while (isxdigit(*cp) && (value = isdigit(*cp) ? *cp - '0' : (islower(*cp) ? toupper(*cp) : *cp) - 'A' + 10) < base) {
        result = result * base + value;
        cp++;
    }
    if (is_neg)
        result = (unsigned int)((int)result * (-1));

    if (endp)
        *endp = (char *)cp;
    return result;
}

#if 1
#define TRUE  1
#define FALSE 0

#ifndef SERVER
#define SERVER 1
#endif
#ifndef CLIENT
#define CLIENT 2
#endif
#define CLIENT_DIRECT 3


#define ARG_IS(_str) (!strcmp(argv[command_counter], _str))
#define OS_STRCMP(str, _str) (!strcmp(str, _str))
int server_or_client = 0;

char s_baudrate[8];

char* s_hci_command[256];
uint8_t r_hci_command[256];
int r_hci_command_len;
char hci_event[2048];

char hci_reset[] = { 0x01, 0x03, 0x0c, 0x00 };
char set_filter[] = { 0x01, 0x05, 0x0c, 0x03, 0x02, 0x00, 0x02 };
char scan_on[] = { 0x01, 0x1a, 0x0c, 0x01, 0x03 };
char dut_en[] = { 0x01, 0x03, 0x18, 0x00 };

enum{
	maxLth_DH1=27,
	maxLth_DH3=138,
	maxLth_DH5=339,
	maxLth_2DH1=54,
	maxLth_2DH3=367,
	maxLth_2DH5=679,
	maxLth_3DH1=83,
	maxLth_3DH3=552,
	maxLth_3DH5=1021,
};

enum{
	_dh1,
	_dh3,
	_dh5,
	_2dh1,
	_2dh3,
	_2dh5,
	_3dh1,
	_3dh3,
	_3dh5,
};

typedef enum{
	bt_start,
	bt_stop,
	ble_start,
	ble_stop,
	tx_tone,
	tx_tonestop,
	bt_reset,
	dut,
	dut_dis_scan,
	get_txpwr,
	set_txpwr,
	set_pwrofst,
} cmd_t;

typedef enum {
	tx,
	rx,
} trx_mode_t;

struct bt_test_args_t{
	trx_mode_t trx_mode;
	cmd_t cmd;
	int type;
	int pattern;
	int hopping;
	int channel;
	uint8_t is_edr;
	int whitening;
	int tx_pwr;
	int len;
	uint8_t addr[6];
	int le_phy;
	int mod_idx;
	int8_t tx_pwrofst;
}bt_test_args={
	.pattern = 0x00,
	.whitening = 0x01,
	.addr = { 0x0A, 0x1C, 0x6B, 0xC6, 0x96, 0x7E },
	.le_phy = 0x01,
	.tx_pwr = 0x7f,
	.tx_pwrofst = 0,
};

void helper(void) {
	printf(
		"\tUsage:\n"
		"\t\t-s <interface> <params>\n"
		"\t\t\tSelect the tool service:\n"

		"\t\t\t\tex. \"bt_test -s uart 1500000 /dev/ttyS0\" or \"bt_test -s usb\" or \"bt_test -s wlan wlan0\"\n"

		"\t\t-c <hci_cmd> Send an HCI command to the selected interface\n"
		"\t\t-v Display Release Date\n"
		"\n"
		"\tCommands:\n"
		"\t\tbt_start [<trx> <chan> <pkt> ...] = Start Bluetooth tx test\n"
		"\t\t\t<trx>      (man) = tx | rx\n"
		"\t\t\t<chan>     (man) = 0 ~ 78 (hopen=off) | 255 (hopen=on)\n"
		"\t\t\t<pkt>      (man) = dh1 | dh3 | dh5 | 2dh1 | 2dh3 | 2dh5 | 3dh1 | 3dh3 | 3dh5\n"
		"\t\t\t<pattern>  (opt) = pn9 (default) | pn15 | h00 | hff | hf0 | h0f | haa | h55\n"
		"\t\t\t<len>      (opt) = dec value (default: pkt's max length)\n"
		"\t\t\t<whiten>   (opt) = on | off (default)\n"
		"\t\t\t<addr>     (opt) = 0a 1c 6b c6 96 7e (default)\n"

		"\t\tbt_stop [<trx>] = Stop Bluetooth tx test\n"
		"\t\t\t<trx>      (man) = tx | rx\n"

 		"\t\tble_start [<trx> <chan> <pkt> ...] = Start BLE tx test\n"
		"\t\t\t<trx>      (man) = tx | rx\n"
		"\t\t\t<chan>     (man) = 0 ~ 39 (hopen=off) | 255 (hopen=on)\n"
		"\t\t\t<pkt>      (man) = 1m | 2m | s2 | s8\n"
		"\t\t\t<pattern>  (opt) = pn9 (default) | pn15 | h00 | hff | hf0 | h0f | haa | h55\n"
		"\t\t\t<len>      (opt) = dec value (default: 255)\n"

		"\t\tble_stop [<trx>] = Stop BLE tx test\n"
		"\t\t\t<trx>      (man) = tx | rx\n"

		"\t\ttx_tone [<chan> ...] = Start a Single-Carrier Bluetooth test\n"
		"\t\t\t<chan>     (man) = 0 ~ 78\n"
		"\t\t\t<pwr>      (opt) = 0 ~ 7 (default: 0x07)\n"
		"\t\ttx_tonestop = Stop Bluetooth Single-Carrier Test\n"

		"\t\tget_txpwr = Get  tx power\n"
		"\t\tset_txpwr [<pwr>] = Set tx power\n"
		"\t\t\t<pwr>	  (man) = 0 ~ 0x6f (default: 0x6f)\n"

		"\t\tbt_reset = reset BT\n"
		"\t\tbt_dut <1/0> = bt_dut /bt_dut 1 (dut en & scan on) | bt_dut 0 (scan off)\n"
		"\t\tset_pwrofst [<pwrofst>]= Set bt tx power offset\n"
		"\t\t\t<pwrofst>      (man) = -7 ~ 7\n"
	);
}

unsigned char atoh(char* s_data){
    unsigned char ret_value;
    char temp_value = 0;

    //h
    if(s_data[0] >= 48 && s_data[0] <= 57){
        temp_value = s_data[0] - 48;
    }else if(s_data[0] >= 65 && s_data[0] <= 70){
        temp_value = (s_data[0] - 65) + 10;
    }else if(s_data[0] >= 97 && s_data[0] <= 102){
        temp_value = (s_data[0] - 97) + 10;
    }

    ret_value = temp_value;
    ret_value = ret_value << 4;
    //printf("atoh1: ret_value:%x\r\n", ret_value);
    //l
    if(s_data[1] >= 48 && s_data[1] <= 57){
        temp_value = s_data[1] - 48;
    }else if(s_data[1] >= 65 && s_data[1] <= 70){
        temp_value = (s_data[1] - 65) + 10;
    }else if(s_data[1] >= 97 && s_data[1] <= 102){
        temp_value = (s_data[1] - 97) + 10;
    }

    ret_value |= temp_value;
    //printf("atoh2: ret_value:%x\r\n", ret_value);

    return ret_value;

}

void send_hci_command(void){


//    printf("\r\n");
}

void send_raw_hci_command(void){
}

static int hci_recv(char* data, int len){
    int i=0;
    unsigned char tmp_byte = 0x00;
    uint16_t hci_len;
    extern rtos_queue bt_driver_server_queue;
    uint32_t ret;

    memset(data, 0, len);
    ret = rtos_queue_read(bt_driver_server_queue, data, -1, 0);
    if (ret != TX_SUCCESS) {
        printf("read error \n");
        return FALSE;
    }
    switch (data[0]) {
        case 1:
            hci_len = (uint16_t)data[3]+4;
            break;
        case 2:
            hci_len = ((uint16_t)(data[3] & 0xFF) | (((uint16_t)(data[4] & 0xFF))<<8))+5;
            break;
        case 3:
            hci_len = (uint16_t)data[3]+4;
            break;
        case 4:
            hci_len = (uint16_t)data[2]+3;
            break;
        default:
            hci_len = 0;
            break;
    }
    printf("EVENT(%d): \r\n", hci_len);
    for (i=0;i<hci_len;i++){
        printf("%02x ", (unsigned char)data[i]);
    }
    printf("\r\n");
}

void* hci_recv_thread( void *arg ){
    printf("hci recv thread ready %p\r\n", arg);
    while(1){
        hci_recv(hci_event, sizeof(hci_event));
//        exit(0);
    }
}

static void start_server(void){

    printf("bt device test ok \r\n");
}

int parse_cmd_line(int argc, char **argv){
	int command_number = argc;
	int command_counter = 1;
	int hci_command_counter = 0;
	//int error_command = 0;
	int channel = 0;
	//int len = 0 ;

	while(command_counter < command_number){
		if(!strcmp(argv[command_counter], "-s")){
			if(!(command_number >= 2)){
				printf("error command \r\n");
				return ERR;
			}
			printf("bt_hci_tool server setting\r\n");
			server_or_client = SERVER;
            return OK;
		}else if(!strcmp(argv[command_counter], "-c")){
			server_or_client = CLIENT;
			command_counter++;
			hci_command_counter = 0;
			while(command_counter < command_number){
				s_hci_command[hci_command_counter++] = argv[command_counter];
				command_counter++;
			}
			return OK;
		}else if (ARG_IS("-m")){
			// reserved for a variety of chip models
		} else if (ARG_IS("bt_start")){
			server_or_client = CLIENT_DIRECT;
			bt_test_args.cmd = bt_start;
			if (command_number < 5) {
				printf("wrong args\r\n");
				return ERR;
			}
			//parse trx
			if (OS_STRCMP(argv[2], "tx")) {
				bt_test_args.trx_mode = tx;
			} else if (OS_STRCMP(argv[2], "rx")) {
				bt_test_args.trx_mode = rx;
			} else {
				printf("no find tx or rx\r\n");
				return ERR;
			}

			//parse chan and hopping
			channel = command_strtoul(argv[3], NULL, 10);
			if (channel < 0 || (channel > 78 && channel != 255)) {
				printf("wrong channel\r\n");
				return ERR;
			} else {
				bt_test_args.channel = channel;
				if (channel == 255)
					bt_test_args.hopping = TRUE;
				else
					bt_test_args.hopping = FALSE;
			}
			//parse type
			if (OS_STRCMP(argv[4], "dh1")){
				bt_test_args.type = _dh1;
				bt_test_args.len = maxLth_DH1;
				bt_test_args.is_edr = FALSE;
			} else if (OS_STRCMP(argv[4], "dh3")){
				bt_test_args.type = _dh3;
				bt_test_args.len = maxLth_DH3;
				bt_test_args.is_edr = FALSE;
			} else if (OS_STRCMP(argv[4], "dh5")){
				bt_test_args.type = _dh5;
				bt_test_args.len = maxLth_DH5;
				bt_test_args.is_edr = FALSE;
			} else if (OS_STRCMP(argv[4], "2dh1")){
				bt_test_args.type = _2dh1;
				bt_test_args.len = maxLth_2DH1;
				bt_test_args.is_edr = TRUE;
			} else if (OS_STRCMP(argv[4], "2dh3")){
				bt_test_args.type = _2dh3;
				bt_test_args.len = maxLth_2DH3;
				bt_test_args.is_edr = TRUE;
			} else if (OS_STRCMP(argv[4], "2dh5")){
				bt_test_args.type = _2dh5;
				bt_test_args.len = maxLth_2DH5;
				bt_test_args.is_edr = TRUE;
			} else if (OS_STRCMP(argv[4], "3dh1")){
				bt_test_args.type = _3dh1;
				bt_test_args.len = maxLth_3DH1;
				bt_test_args.is_edr = TRUE;
			} else if (OS_STRCMP(argv[4], "3dh3")){
				bt_test_args.type = _3dh3;
				bt_test_args.len = maxLth_3DH3;
				bt_test_args.is_edr = TRUE;
			} else if (OS_STRCMP(argv[4], "3dh5")){
				bt_test_args.type = _3dh5;
				bt_test_args.len = maxLth_3DH5;
				bt_test_args.is_edr = TRUE;
			} else {
				printf("wrong type\r\n");
				return ERR;
			}
			//parse pattern
			if (command_number > 5) {
				if (OS_STRCMP(argv[5], "pn9")){
					bt_test_args.pattern =  0x00;
				} else if (OS_STRCMP(argv[5], "hf0")){
					bt_test_args.pattern = 0x01;
				} else if (OS_STRCMP(argv[5], "haa")){
					bt_test_args.pattern = 0x02;
				} else if (OS_STRCMP(argv[5], "pn15")){
					bt_test_args.pattern = 0x03;
				} else if (OS_STRCMP(argv[5], "hff")){
					bt_test_args.pattern = 0x04;
				} else if (OS_STRCMP(argv[5], "h00")){
					bt_test_args.pattern = 0x05;
				} else if (OS_STRCMP(argv[5], "h0f")){
					bt_test_args.pattern = 0x06;
				} else if (OS_STRCMP(argv[5], "h55")){
					bt_test_args.pattern = 0x07;
				} else {
					printf("wrong pattern type\r\n");
					return ERR;
				}

			//parse pkt len
			if (command_number > 6) {
				bt_test_args.len = command_strtoul(argv[6], NULL, 10);
			}

			}
			//parse whitening
			if (command_number > 7) {
				if (OS_STRCMP(argv[7], "on")){
					bt_test_args.whitening =  0x00;
				} else if (OS_STRCMP(argv[7], "off")){
					bt_test_args.whitening = 0x01;
				} else {
					printf("wrong whitening args, use on/off\r\n");
					return ERR;
				}
			}
			//parse mac addr
			if (command_number > 8) {
				if (command_number < 14) {
					printf("wrong mac addr eg: 0a 1c 6b c6 96 7e\r\n");
					return ERR;
				}
			bt_test_args.addr[5] = command_strtoul(argv[8], NULL, 16);
			bt_test_args.addr[4] = command_strtoul(argv[9], NULL, 16);
			bt_test_args.addr[3] = command_strtoul(argv[10], NULL, 16);
			bt_test_args.addr[2] = command_strtoul(argv[11], NULL, 16);
			bt_test_args.addr[1] = command_strtoul(argv[12], NULL, 16);
			bt_test_args.addr[0] = command_strtoul(argv[13], NULL, 16);
			}
			return OK;
		} else if (ARG_IS("bt_stop")){
			server_or_client = CLIENT_DIRECT;
			bt_test_args.cmd = bt_stop;
			if (command_number < 3) {
				printf("wrong args\r\n");
				return ERR;
			}
			//parse trx
			if (OS_STRCMP(argv[2], "tx")) {
				bt_test_args.trx_mode = tx;
			} else if (OS_STRCMP(argv[2], "rx")) {
				bt_test_args.trx_mode = rx;
			} else {
				printf("no find tx or rx\r\n");
				return ERR;
			}
			return OK;
		} else if (ARG_IS("ble_start")){
			server_or_client = CLIENT_DIRECT;
			bt_test_args.cmd = ble_start;
			if (command_number < 5) {
				printf("wrong args\r\n");
				return ERR;
			}
			//parse trx
			if (OS_STRCMP(argv[2], "tx")) {
				bt_test_args.trx_mode = tx;
			} else if (OS_STRCMP(argv[2], "rx")) {
				bt_test_args.trx_mode = rx;
				bt_test_args.mod_idx = 0;//modulation index fix to 0
			} else {
				printf("no find tx or rx\r\n");
				return ERR;
			}

			//parse chan and hopping
			channel = command_strtoul(argv[3], NULL, 10);
			if (channel < 0 || (channel > 39 && channel != 255)) {
				printf("wrong channel\r\n");
				return ERR;
			} else {
				bt_test_args.channel = channel;
				if (channel == 255)
					bt_test_args.hopping = TRUE;
				else
					bt_test_args.hopping = FALSE;
			}
			//parse le phy
			if (OS_STRCMP(argv[4], "1m")){
				bt_test_args.le_phy = 0x01;
			} else if (OS_STRCMP(argv[4], "2m")){
				bt_test_args.le_phy = 0x02;
			} else if (OS_STRCMP(argv[4], "s8")){
				bt_test_args.le_phy = 0x03;
			} else if (OS_STRCMP(argv[4], "s2")){
				if (bt_test_args.trx_mode == tx){
						bt_test_args.le_phy = 0x04;
				} else {
						bt_test_args.le_phy = 0x03;
				}

			} else {
				printf("wrong type\r\n");
				return ERR;
			}
			//parse pattern
			if (command_number > 5) {
				if (OS_STRCMP(argv[5], "pn9")){
					bt_test_args.pattern =  0x00;
				} else if (OS_STRCMP(argv[5], "hf0")){
					bt_test_args.pattern = 0x01;
				} else if (OS_STRCMP(argv[5], "haa")){
					bt_test_args.pattern = 0x02;
				} else if (OS_STRCMP(argv[5], "pn15")){
					bt_test_args.pattern = 0x03;
				} else if (OS_STRCMP(argv[5], "hff")){
					bt_test_args.pattern = 0x04;
				} else if (OS_STRCMP(argv[5], "h00")){
					bt_test_args.pattern = 0x05;
				} else if (OS_STRCMP(argv[5], "h0f")){
					bt_test_args.pattern = 0x06;
				} else if (OS_STRCMP(argv[5], "h55")){
					bt_test_args.pattern = 0x07;
				} else {
					printf("wrong pattern type\r\n");
					return ERR;
				}
			}
			//parse pkt len
			bt_test_args.len = 255;
			if (command_number > 6) {
				bt_test_args.len = command_strtoul(argv[6], NULL, 10);
				if (bt_test_args.len < 1 || bt_test_args.len > 255) {
					printf("len limit 1~255 (default: 255)\r\n");
					}
			}

			return OK;
		} else if (ARG_IS("ble_stop")){
			server_or_client = CLIENT_DIRECT;
			bt_test_args.cmd = ble_stop;
			if (command_number < 3) {
				printf("wrong args\r\n");
				return ERR;
			}
			//parse trx
			if (OS_STRCMP(argv[2], "tx")) {
				bt_test_args.trx_mode = tx;
			} else if (OS_STRCMP(argv[2], "rx")) {
				bt_test_args.trx_mode = rx;
			} else {
				printf("no find tx or rx\r\n");
				return ERR;
			}
			return OK;
		} else if (ARG_IS("tx_tone")){
			server_or_client = CLIENT_DIRECT;
			bt_test_args.cmd = tx_tone;
			if (command_number < 3) {
				printf("wrong args\r\n");
				return ERR;
			}
			//parse chan
			channel = command_strtoul(argv[2], NULL, 10);
			if (channel < 0 || (channel > 78)) {
				printf("wrong channel\r\n");
				return ERR;
			} else {
				bt_test_args.channel = channel;
			}
			//parse tx pwr
			bt_test_args.tx_pwr = 7;
			if (command_number > 3) {
				bt_test_args.tx_pwr = command_strtoul(argv[3], NULL, 16);
				if (bt_test_args.tx_pwr < 0 || bt_test_args.tx_pwr > 7) {
					printf("pwr limit 0~7 (default: 07)\r\n");
					bt_test_args.tx_pwr = 7;
				}
			}
			return OK;
		} else if (ARG_IS("tx_tonestop")){
			server_or_client = CLIENT_DIRECT;
			bt_test_args.cmd = tx_tonestop;
			return OK;
		} else if (ARG_IS("bt_reset")){
			server_or_client = CLIENT_DIRECT;
			bt_test_args.cmd = bt_reset;
			return OK;
		} else if (ARG_IS("bt_dut")){
			server_or_client = CLIENT_DIRECT;
			if (command_number > 2 && !(command_strtoul(argv[2], NULL, 10)))
				bt_test_args.cmd = dut_dis_scan;
			else
				bt_test_args.cmd = dut;
			return OK;
		} else if (ARG_IS("get_txpwr")){
			server_or_client = CLIENT_DIRECT;
			bt_test_args.cmd = get_txpwr;
			return OK;
		} else if (ARG_IS("set_txpwr")){
			server_or_client = CLIENT_DIRECT;
			bt_test_args.cmd = set_txpwr;
			if (command_number < 2) {
				printf("wrong args\r\n");
				return ERR;
			}
			//parse tx pwr
			bt_test_args.tx_pwr = 0x6f;
			if (command_number > 2) {
				bt_test_args.tx_pwr = command_strtoul(argv[2], NULL, 16);
				if (bt_test_args.tx_pwr < 0 || bt_test_args.tx_pwr > 0x6f) {
					printf("pwr limit 0~0x6f (default: 0x6f)\r\n");
					bt_test_args.tx_pwr = 0x6f;
				}
			}
			return OK;
		} else if (ARG_IS("set_pwrofst")){
			server_or_client = CLIENT_DIRECT;
			bt_test_args.cmd = set_pwrofst;
			if (command_number < 2) {
				printf("wrong args\r\n");
				return ERR;
			}
			//parse tx pwrofst
			bt_test_args.tx_pwrofst = 0x0;
			if (command_number > 2) {
				bt_test_args.tx_pwrofst = command_strtoul(argv[2], NULL, 10);
				printf("txpwrofst %d\n", bt_test_args.tx_pwrofst);
				if (bt_test_args.tx_pwrofst < -7 || bt_test_args.tx_pwrofst > 7) {
					printf("pwrofst limit -7 ~ 7\r\n");
					if (bt_test_args.tx_pwrofst > 7) {
						bt_test_args.tx_pwrofst = 7;
					} else if (bt_test_args.tx_pwrofst < -7) {
						bt_test_args.tx_pwrofst = -7;
					}
				}
			}
			return OK;
		} else {
			if (strcmp(argv[1], "-h") != 0) {
				printf("unknow command %s \r\n", argv[command_counter]);
			}
			helper();
			return ERR;
		}

		command_counter++;
	}

	helper();
	return ERR;

}
#endif


int handle_private_cmd(struct rwnx_hw *rwnx_hw, char *command)
{
    int bytes_written = 0;
    char *argv[CMD_MAXARGS + 1];
    int argc;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    if ((argc = parse_line(command, argv)) == 0) {
        return -1;
    }

#if defined(CONFIG_WIFI_MODE_RFTEST)
    if (rwnx_hw->mode == WIFI_MODE_RFTEST) {
        struct dbg_rftest_cmd_cfm cfm = {{0,}};
        u8_l mac_addr[6];
        cmd_rf_settx_t settx_param;
        cmd_rf_rx_t setrx_param;
        int freq;
        cmd_rf_getefuse_t getefuse_param;
        cmd_rf_setfreq_t cmd_setfreq;
        u8_l ana_pwr;
        u8_l dig_pwr;
        u8_l pwr;
        u8_l xtal_cap;
        u8_l xtal_cap_fine;
        u8_l vendor_info;
        #ifdef CONFIG_USB_BT
        int bt_index;
        u8_l dh_cmd_reset[4];
        u8_l dh_cmd_txdh[18];
        u8_l dh_cmd_rxdh[17];
        u8_l dh_cmd_stop[5];
        #endif
        u8_l buf[2];
        s8_l freq_ = 0;
        u8_l func = 0;
        int ret;
        do {
            //#ifdef AICWF_SDIO_SUPPORT
            //struct rwnx_hw *p_rwnx_hw = g_rwnx_plat->sdiodev->rwnx_hw;
            //#endif
            //#ifdef AICWF_USB_SUPPORT

            //struct rwnx_hw *p_rwnx_hw = cntrl_rwnx_hw;
            //#endif
            if (strcasecmp(argv[0], "GET_RX_RESULT") ==0) {
                printk("get_rx_result\n");
                rwnx_send_rftest_req(rwnx_hw, GET_RX_RESULT, 0, NULL, &cfm);
                //memcpy(command, &cfm.rftest_result[0], 8);
                bytes_written = 8;
                printk("done: getrx fcsok=%d, total=%d\n", (unsigned int)cfm.rftest_result[0], (unsigned int)cfm.rftest_result[1]);
            }else if (strcasecmp(argv[0], "SET_COUNTRYCODE") == 0) {
                if (argc > 1) {
                    memcpy(current_country_code, argv[1], 2);
                    printk("country code :%s\r\n", argv[1]);
                } else {
                    printk("wrong param\n");
                    break;
                }
            } else if (strcasecmp(argv[0], "SET_TX") == 0) {
                printk("set_tx\n");
                if (argc < 6) {
                    printk("wrong param\n");
                    break;
                }
            	u8_l set_p = 0;
                u8_l lvl_band, lvl_mod = 0, lvl_idx, lvl_pwr = 0;
                u8_l buf[10];
                #ifdef CONFIG_POWER_LIMIT
                int8_t max_pwr;
                uint8_t r_idx;
                txpwr_loss_conf_t txpwr_loss_tmp;
                txpwr_loss_conf_t *txpwr_loss;
                txpwr_loss = &txpwr_loss_tmp;
                #endif

                settx_param.chan = command_strtoul(argv[1], NULL, 10);
                settx_param.bw = command_strtoul(argv[2], NULL, 10);
                settx_param.mode = command_strtoul(argv[3], NULL, 10);
                settx_param.rate = command_strtoul(argv[4], NULL, 10);
                settx_param.length = command_strtoul(argv[5], NULL, 10);
                if (argc > 6) {
					settx_param.tx_intv_us = command_strtoul(argv[6], NULL, 10);
				} else {
					settx_param.tx_intv_us = 10000;
				}
            	if (argc > 7) {
                    // if (dev->chip_id == PRODUCT_ID_AIC8801){
                    //     aic_dbg("unsupported cmd\n");
                    //     return -1;
                    // }
                    lvl_pwr = command_strtoul(argv[7], NULL, 10);
                    aic_dbg("lvl_pwr: %d\n", lvl_pwr);

                    if (settx_param.chan >= 36)
                        lvl_band = 2;
                    else
                        lvl_band = 1;
                    if (settx_param.mode == 0)
                        lvl_mod = 0;
                    else if (settx_param.mode == 2 || settx_param.mode == 4)
                        lvl_mod = 1;
                    else if (settx_param.mode == 5)
                        lvl_mod = 2;
                    if (settx_param.mode >= 4)
                        lvl_idx = settx_param.rate & 0xF;
                    else if (settx_param.mode >= 2)
                        lvl_idx = settx_param.rate & 0x7;
                    else
                        lvl_idx = settx_param.rate;

                    buf[0] = lvl_band;
                    buf[1] = lvl_mod;
                    buf[2] = lvl_idx;
                    buf[3] = lvl_pwr;

                    set_p = 1;
                }
				settx_param.max_pwr = POWER_LEVEL_INVALID_VAL;
                printk("txparam:%d,%d,%d,%d,%d\n", settx_param.chan, settx_param.bw,
                        settx_param.mode, settx_param.rate, settx_param.length);

                #ifdef CONFIG_POWER_LIMIT
                extern uint8_t get_ccode_region(char * ccode);
                extern void get_userconfig_txpwr_loss(txpwr_loss_conf_t *txpwr_loss);
                extern int8_t get_powerlimit_by_chnum(uint8_t chnum, uint8_t r_idx, uint8_t bw);
                r_idx = get_ccode_region(current_country_code);
                txpwr_loss = &txpwr_loss_tmp;
                get_userconfig_txpwr_loss(txpwr_loss);
                if (txpwr_loss->loss_enable_2g4 == 1)
                    aic_dbg("%s:loss_value_2g4: %d\r\n", __func__,
                                txpwr_loss->loss_value_2g4);
                if (txpwr_loss->loss_enable_5g == 1)
                    aic_dbg("%s:loss_value_5g: %d\r\n", __func__,
                            txpwr_loss->loss_value_5g);
                max_pwr = get_powerlimit_by_chnum(settx_param.chan, r_idx, settx_param.bw);
                if (settx_param.chan >= 36) {
                    if (txpwr_loss->loss_enable_5g == 1)
                        max_pwr -= txpwr_loss->loss_value_5g;
                } else {
                    if (txpwr_loss->loss_enable_2g4 == 1)
                        max_pwr -= txpwr_loss->loss_value_2g4;
                }

                if (!set_p || (lvl_pwr == 255)) {
                    settx_param.max_pwr = max_pwr;
                    aic_dbg("max_pwr:%d\n", settx_param.max_pwr);
                } else
                    aic_dbg("the specified power[%d] exceed the power limit[%d]\n", lvl_pwr, max_pwr);
                #endif /* CONFIG_POWER_LIMIT */

                if (set_p && (lvl_pwr != 255))
                    rwnx_send_rftest_req(rwnx_hw, RDWR_PWRLVL, 4, buf, &cfm);
                rwnx_send_rftest_req(rwnx_hw, SET_TX, sizeof(cmd_rf_settx_t), (u8_l *)&settx_param, NULL);
            } else if (strcasecmp(argv[0], "SET_TXSTOP") == 0) {
                printk("settx_stop\n");
                rwnx_send_rftest_req(rwnx_hw, SET_TXSTOP, 0, NULL, NULL);
            } else if (strcasecmp(argv[0], "SET_TXTONE") == 0) {
                printk("set_tx_tone,argc:%d\n",argc);
                if ((argc == 2) || (argc == 3)) {
                    printk("argv 1:%s\n",argv[1]);
                    //u8_l func = (u8_l)command_strtoul(argv[1], NULL, 16);
                    func = (u8_l)command_strtoul(argv[1], NULL, 16);
                    //s8_l freq;
                    if (argc == 3) {
                        printk("argv 2:%s\n",argv[2]);
                        freq_ = (u8_l)command_strtoul(argv[2], NULL, 10);
                    } else {
                        freq_ = 0;
                    };
                    //u8_l buf[2] = {func, (u8_l)freq};
                    buf[0] = func;
                    buf[1] = (u8_l)freq_;
                    rwnx_send_rftest_req(rwnx_hw, SET_TXTONE, argc - 1, buf, NULL);
                } else {
                    printk("wrong args\n");
                }
            } else if (strcasecmp(argv[0], "SET_RX") == 0) {
                printk("set_rx\n");
                if (argc < 3) {
                    printk("wrong param\n");
                    break;
                }
                setrx_param.chan = command_strtoul(argv[1], NULL, 10);
                setrx_param.bw = command_strtoul(argv[2], NULL, 10);
                rwnx_send_rftest_req(rwnx_hw, SET_RX, sizeof(cmd_rf_rx_t), (u8_l *)&setrx_param, NULL);
            } else if (strcasecmp(argv[0], "SET_RXSTOP") == 0) {
                printk("set_rxstop\n");
                rwnx_send_rftest_req(rwnx_hw, SET_RXSTOP, 0, NULL, NULL);
            } else if (strcasecmp(argv[0], "SET_RX_METER") == 0) {
                printk("set_rx_meter\n");
                freq = (int)command_strtoul(argv[1], NULL, 10);
                rwnx_send_rftest_req(rwnx_hw, SET_RX_METER, sizeof(freq), (u8_l *)&freq, NULL);
            } else if (strcasecmp(argv[0], "SET_FREQ_CAL") == 0) {
                printk("set_freq_cal\n");
                if (argc < 2) {
                    printk("wrong param\n");
                    break;
                }
                cmd_setfreq.val = command_strtoul(argv[1], NULL, 16);
                printk("param:%x\r\n", cmd_setfreq.val);
                rwnx_send_rftest_req(rwnx_hw, SET_FREQ_CAL, sizeof(cmd_rf_setfreq_t), (u8_l *)&cmd_setfreq, &cfm);
                //memcpy(command, &cfm.rftest_result[0], 4);
                bytes_written = 4;
                printk("done: freq_cal: 0x%8x\n", (unsigned int)cfm.rftest_result[0]);
            } else if (strcasecmp(argv[0], "SET_FREQ_CAL_FINE") == 0) {
                printk("set_freq_cal_fine\n");
                if (argc < 2) {
                    printk("wrong param\n");
                    break;
                }
                cmd_setfreq.val = command_strtoul(argv[1], NULL, 16);
                printk("param:%x\r\n", cmd_setfreq.val);
                rwnx_send_rftest_req(rwnx_hw, SET_FREQ_CAL_FINE, sizeof(cmd_rf_setfreq_t), (u8_l *)&cmd_setfreq, &cfm);
                //memcpy(command, &cfm.rftest_result[0], 4);
                bytes_written = 4;
                printk("done: freq_cal_fine: 0x%8x\n", (unsigned int)cfm.rftest_result[0]);
            } else if (strcasecmp(argv[0], "GET_EFUSE_BLOCK") == 0) {
                printk("get_efuse_block\n");
                if (argc < 2) {
                    printk("wrong param\n");
                    break;
                }
                getefuse_param.block = command_strtoul(argv[1], NULL, 10);
                rwnx_send_rftest_req(rwnx_hw, GET_EFUSE_BLOCK, sizeof(cmd_rf_getefuse_t), (u8_l *)&getefuse_param, &cfm);
                //memcpy(command, &cfm.rftest_result[0], 4);
                bytes_written = 4;
                printk("done:efuse: 0x%8x\n", (unsigned int)cfm.rftest_result[0]);
            } else if (strcasecmp(argv[0], "SET_POWER") == 0) {
                printk("set_power\n");
                ana_pwr = command_strtoul(argv[1], NULL, 16);
                dig_pwr = command_strtoul(argv[2], NULL, 16);
                pwr = (ana_pwr << 4 | dig_pwr);
                if (ana_pwr > 0xf || dig_pwr > 0xf) {
                    printk("invalid param\r\n");
                    break;
                }
                printk("pwr =%x\r\n", pwr);
                rwnx_send_rftest_req(rwnx_hw, SET_POWER, sizeof(pwr), (u8_l *)&pwr, NULL);
            } else if (strcasecmp(argv[0], "SET_XTAL_CAP")==0) {
                printk("set_xtal_cap\n");
                if (argc < 2) {
                    printk("wrong param\n");
                    break;
                }
                xtal_cap = command_strtoul(argv[1], NULL, 10);
                printk("xtal_cap =%x\r\n", xtal_cap);
                rwnx_send_rftest_req(rwnx_hw, SET_XTAL_CAP, sizeof(xtal_cap), (u8_l *)&xtal_cap, &cfm);
                //memcpy(command, &cfm.rftest_result[0], 4);
                bytes_written = 4;
                printk("done:xtal cap: 0x%x\n", (unsigned int)cfm.rftest_result[0]);
            } else if (strcasecmp(argv[0], "SET_XTAL_CAP_FINE")==0) {
                printk("set_xtal_cap_fine\n");
                if (argc < 2) {
                    printk("wrong param\n");
                    break;
                }
                xtal_cap_fine = command_strtoul(argv[1], NULL, 10);
                printk("xtal_cap_fine =%x\r\n", xtal_cap_fine);
                rwnx_send_rftest_req(rwnx_hw, SET_XTAL_CAP_FINE, sizeof(xtal_cap_fine), (u8_l *)&xtal_cap_fine, &cfm);
                //memcpy(command, &cfm.rftest_result[0], 4);
                bytes_written = 4;
                printk("done:xtal cap_fine: 0x%x\n", (unsigned int)cfm.rftest_result[0]);
            } else if (strcasecmp(argv[0], "SET_MAC_ADDR")==0) {
                printk("set_mac_addr\n");
                if (argc < 7) {
                    printk("wrong param\n");
                    break;
                }
                mac_addr[5] = command_strtoul(argv[1], NULL, 16);
                mac_addr[4] = command_strtoul(argv[2], NULL, 16);
                mac_addr[3] = command_strtoul(argv[3], NULL, 16);
                mac_addr[2] = command_strtoul(argv[4], NULL, 16);
                mac_addr[1] = command_strtoul(argv[5], NULL, 16);
                mac_addr[0] = command_strtoul(argv[6], NULL, 16);
                printk("set macaddr:%x,%x,%x,%x,%x,%x\n", mac_addr[5], mac_addr[4], mac_addr[3], mac_addr[2], mac_addr[1], mac_addr[0]);
                rwnx_send_rftest_req(rwnx_hw, SET_MAC_ADDR, sizeof(mac_addr), (u8_l *)&mac_addr, NULL);
            } else if (strcasecmp(argv[0], "GET_MAC_ADDR")==0) {
                printk("get mac addr\n");
                rwnx_send_rftest_req(rwnx_hw, GET_MAC_ADDR, 0, NULL, &cfm);
                //memcpy(command, &cfm.rftest_result[0], 8);
                bytes_written = 8;
                printk("done: get macaddr: 0x%x,0x%x\n", cfm.rftest_result[0], cfm.rftest_result[1]);
            } else if (strcasecmp(argv[0], "SET_BT_MAC_ADDR") == 0) {
                printk("set_bt_mac_addr\n");
                if (argc < 7) {
                    printk("wrong param\n");
                    break;
                }
                mac_addr[5] = command_strtoul(argv[1], NULL, 16);
                mac_addr[4] = command_strtoul(argv[2], NULL, 16);
                mac_addr[3] = command_strtoul(argv[3], NULL, 16);
                mac_addr[2] = command_strtoul(argv[4], NULL, 16);
                mac_addr[1] = command_strtoul(argv[5], NULL, 16);
                mac_addr[0] = command_strtoul(argv[6], NULL, 16);
                printk("set bt macaddr:%x,%x,%x,%x,%x,%x\n", mac_addr[5], mac_addr[4], mac_addr[3], mac_addr[2], mac_addr[1], mac_addr[0]);
                rwnx_send_rftest_req(rwnx_hw, SET_BT_MAC_ADDR, sizeof(mac_addr), (u8_l *)&mac_addr, NULL);
            } else if (strcasecmp(argv[0], "GET_BT_MAC_ADDR")==0) {
                printk("get bt mac addr\n");
                rwnx_send_rftest_req(rwnx_hw, GET_BT_MAC_ADDR, 0, NULL, &cfm);
                //memcpy(command, &cfm.rftest_result[0], 8);
                bytes_written = 8;
                printk("done: get bt macaddr: 0x%x,0x%x\n", cfm.rftest_result[0], cfm.rftest_result[1]);
            } else if (strcasecmp(argv[0], "SET_VENDOR_INFO")==0) {
                vendor_info = command_strtoul(argv[1], NULL, 16);
                printk("set vendor info:%x\n", vendor_info);
                rwnx_send_rftest_req(rwnx_hw, SET_VENDOR_INFO, 1, &vendor_info, &cfm);
                //memcpy(command, &cfm.rftest_result[0], 1);
                bytes_written = 1;
                printk("done: get_vendor_info = 0x%x\n", (unsigned int)cfm.rftest_result[0]);
            } else if (strcasecmp(argv[0], "GET_VENDOR_INFO")==0) {
                printk("get vendor info\n");
                rwnx_send_rftest_req(rwnx_hw, GET_VENDOR_INFO, 0, NULL, &cfm);
                //memcpy(command, &cfm.rftest_result[0], 1);
                bytes_written = 1;
                printk("done: get_vendor_info = 0x%x\n", (unsigned int)cfm.rftest_result[0]);
            } else if (strcasecmp(argv[0], "GET_FREQ_CAL") == 0) {
                printk("get freq cal\n");
                rwnx_send_rftest_req(rwnx_hw, GET_FREQ_CAL, 0, NULL, &cfm);
                //memcpy(command, &cfm.rftest_result[0], 4);
                bytes_written = 4;
                printk("done: get_freq_cal: xtal_cap=0x%x, xtal_cap_fine=0x%x\n", cfm.rftest_result[0] & 0x0000ffff, (cfm.rftest_result[0] >> 16) & 0x0000ffff);
            } else if (strcasecmp(argv[0], "RDWR_PWRMM") == 0) {
                printk("read/write txpwr manul mode\n");
                if (argc <= 1) { // read cur
                    rwnx_send_rftest_req(rwnx_hw, RDWR_PWRMM, 0, NULL, &cfm);
                } else { // write
                    u8_l pwrmm = (u8_l)command_strtoul(argv[1], NULL, 16);
                    pwrmm = (pwrmm) ? 1 : 0;
                    printk("set pwrmm = %x\r\n", pwrmm);
                    rwnx_send_rftest_req(rwnx_hw, RDWR_PWRMM, sizeof(pwrmm), (u8_l *)&pwrmm, &cfm);
                }
                //memcpy(command, &cfm.rftest_result[0], 4);
                bytes_written = 4;
                printk("done: txpwr manual mode = %x\n", (unsigned int)cfm.rftest_result[0]);
            } else if (strcasecmp(argv[0], "RDWR_PWRIDX") == 0) {
                u8_l func = 0;
                printk("read/write txpwr index\n");
                if (argc > 1) {
                    func = (u8_l)command_strtoul(argv[1], NULL, 16);
                }
                if (func == 0) { // read cur
                    rwnx_send_rftest_req(rwnx_hw, RDWR_PWRIDX, 0, NULL, &cfm);
                } else if (func <= 2) { // write 2.4g/5g pwr idx
                    if (argc > 3) {
                        u8_l type = (u8_l)command_strtoul(argv[2], NULL, 16);
                        u8_l pwridx = (u8_l)command_strtoul(argv[3], NULL, 10);
                        u8_l buf[3] = {func, type, pwridx};
                        printk("set pwridx:[%x][%x]=%x\r\n", func, type, pwridx);
                        rwnx_send_rftest_req(rwnx_hw, RDWR_PWRIDX, sizeof(buf), buf, &cfm);
                    } else {
                        printk("wrong args\n");
                    }
                } else {
                    printk("wrong func: %x\n", func);
                }
                //memcpy(command, &cfm.rftest_result[0], 9);
                bytes_written = 9;
                char *buff = &cfm.rftest_result[0];
                printk("done:\n"
                       "txpwr index 2.4g:\n"
                       "  [0]=%d(ofdmlowrate)\n"
                       "  [1]=%d(ofdm64qam)\n"
                       "  [2]=%d(ofdm256qam)\n"
                       "  [3]=%d(ofdm1024qam)\n"
                       "  [4]=%d(dsss)\n", buff[0], buff[1], buff[2], buff[3], buff[4]);
                printk("txpwr index 5g:\n"
                       "  [0]=%d(ofdmlowrate)\n"
                       "  [1]=%d(ofdm64qam)\n"
                       "  [2]=%d(ofdm256qam)\n"
                       "  [3]=%d(ofdm1024qam)\n", buff[5], buff[6], buff[7], buff[8]);
            } else if (strcasecmp(argv[0], "RDWR_PWRLVL") == 0) {
                u8_l func = 0;
                printk("read/write txpwr level\n");
                if (argc > 1) {
                    func = (u8_l)command_strtoul(argv[1], NULL, 16);
                }
                if (func == 0) { // read cur
                    rwnx_send_rftest_req(rwnx_hw, RDWR_PWRLVL, 0, NULL, &cfm);
                } else if (func <= 2) { // write 2.4g/5g pwr lvl
                    if (argc > 4) {
                        u8_l grp = (u8_l)command_strtoul(argv[2], NULL, 16);
                        u8_l idx, size;
                        u8_l buf[14] = {func, grp,};
                        if (argc > 12) { // set all grp
                            printk("set pwrlvl %s:\n"
                            "  [%x] =", (func == 1) ? "2.4g" : "5g", grp);
                            if (grp == 1) { // TXPWR_LVL_GRP_11N_11AC
                                size = 10;
                            } else {
                                size = 12;
                            }
                            for (idx = 0; idx < size; idx++) {
                                s8_l pwrlvl = (s8_l)command_strtoul(argv[3 + idx], NULL, 10);
                                buf[2 + idx] = (u8_l)pwrlvl;
                                if (idx && !(idx & 0x3)) {
                                    printk(" ");
                                }
                                printk(" %2d", pwrlvl);
                            }
                            printk("\n");
                            size += 2;
                        } else { // set grp[idx]
                            u8_l idx = (u8_l)command_strtoul(argv[3], NULL, 10);
                            s8_l pwrlvl = (s8_l)command_strtoul(argv[4], NULL, 10);
                            buf[2] = idx;
                            buf[3] = (u8_l)pwrlvl;
                            size = 4;
                            printk("set pwrlvl %s:\n"
                            "  [%x][%d] = %d\n", (func == 1) ? "2.4g" : "5g", grp, idx, pwrlvl);
                        }
                        rwnx_send_rftest_req(rwnx_hw, RDWR_PWRLVL, size, buf, &cfm);
                    } else {
                        printk("wrong args\n");
                        bytes_written = -EINVAL;
                        break;
                    }
                } else {
                    printk("wrong func: %x\n", func);
                    bytes_written = -EINVAL;
                    break;
                }
                //memcpy(command, &cfm.rftest_result[0], 3 * 12);
                bytes_written = 3 * 12;
                char *buff = &cfm.rftest_result[0];
                int grp, idx;
                printk("done:\n"
                       "txpwr index 2.4g: [0]:11b+11a/g, [1]:11n/11ac, [2]:11ax\n");
                for (grp = 0; grp < 3; grp++) {
                    int cnt = 12;
                    if (grp == 1) {
                        cnt = 10;
                    }
                    printk("  [%x] =", grp);
                    for (idx = 0; idx < cnt; idx++) {
                        if (idx && !(idx & 0x3)) {
                            printk(" ");
                        }
                        printk(" %2d", buff[12 * grp + idx]);
                    }
                    printk("\r\n");
                }
            } else if (strcasecmp(argv[0], "RDWR_PWROFST") == 0) {
                u8_l func = 0;
                int res_len = 0;
                printk("read/write txpwr offset\n");
                if (argc > 1) {
                    func = (u8_l)command_strtoul(argv[1], NULL, 16);
                }
                if (func == 0) { // read cur
                    rwnx_send_rftest_req(rwnx_hw, RDWR_PWROFST, 0, NULL, &cfm);
                } else if (func <= 2) { // write 2.4g/5g pwr ofst
    				if ((argc > 4) && (rwnx_hw->chipid == PRODUCT_ID_AIC8800D80)) {
                        u8_l type = (u8_l)command_strtoul(argv[2], NULL, 16);
                        u8_l chgrp = (u8_l)command_strtoul(argv[3], NULL, 16);
                        s8_l pwrofst = (u8_l)command_strtoul(argv[4], NULL, 10);
                        u8_l buf[4] = {func, type, chgrp, (u8_l)pwrofst};
                        printk("set pwrofst_%s:[%x][%x]=%d\r\n", (func == 1) ? "2.4g" : "5g", type, chgrp, pwrofst);
                        rwnx_send_rftest_req(rwnx_hw, RDWR_PWROFST, sizeof(buf), buf, &cfm);
                    } else if ((argc > 3) && (rwnx_hw->chipid != PRODUCT_ID_AIC8800D80)) {
    					u8_l chgrp = (u8_l)command_strtoul(argv[2], NULL, 16);
    					s8_l pwrofst = (u8_l)command_strtoul(argv[3], NULL, 10);
    					u8_l buf[3] = {func, chgrp, (u8_l)pwrofst};
    					printk("set pwrofst_%s:[%x]=%d\r\n", (func == 1) ? "2.4g" : "5g", chgrp, pwrofst);
    					rwnx_send_rftest_req(rwnx_hw, RDWR_PWROFST, sizeof(buf), buf, &cfm);
    				} else {
    					printk("wrong args\n");
    				}
			    } else {
                    printk("wrong func: %x\n", func);
                }

                if ((rwnx_hw->chipid == PRODUCT_ID_AIC8800DC) ||
                    (rwnx_hw->chipid == PRODUCT_ID_AIC8800DW)) { // 3 = 3 (2.4g)
                    res_len = 3;
                } else if (rwnx_hw->chipid == PRODUCT_ID_AIC8800D80) { // 3 * 2 (2.4g) + 3 * 6 (5g)
                    res_len = 3 * 3 + 3 * 6;
                } else {
                    res_len = 3 + 4;
                }

                signed char *buff = &cfm.rftest_result[0];
                if (rwnx_hw->chipid != PRODUCT_ID_AIC8800D80) {
                    printk("done:\n"
                           "txpwr offset 2.4g:\n"
                           "  [0]=%d(ch1~4)\n"
                           "  [1]=%d(ch5~9)\n"
                           "  [2]=%d(ch10~13)\n", buff[0], buff[1], buff[2]);
                    printk("txpwr offset 5g:\n"
                           "  [0]=%d(ch36~64)\n"
                           "  [1]=%d(ch100~120)\n"
                           "  [2]=%d(ch122~140)\n"
                           "  [3]=%d(ch142~165)\n", buff[3], buff[4], buff[5], buff[6]);
                } else {
                    int type, ch_grp;
                    printf("done:\n"
                        "pwrofst2x 2.4g: [0]:11b, [1]:ofdm_highrate, [2]:ofdm_lowrate\n"
                        "  chan=" "\t1-4" "\t5-9" "\t10-13");
                    for (type = 0; type < 3; type++) {
                        printf("\n  [%d] =", type);
                        for (ch_grp = 0; ch_grp < 3; ch_grp++) {
                            printf("\t%d", buff[3 * type + ch_grp]);
                        }
                    }
                    printf("\npwrofst2x 5g: [0]:ofdm_lowrate, [1]:ofdm_highrate, [2]:ofdm_midrate\n"
                        "  chan=" "\t36-50" "\t51-64" "\t98-114" "\t115-130" "\t131-146" "\t147-166");
                    buff = (signed char *)&buff[3 * 3];
                    for (type = 0; type < 3; type++) {
                        printf("\n  [%d] =", type);
                        for (ch_grp = 0; ch_grp < 6; ch_grp++) {
                            printf("\t%d", buff[6 * type + ch_grp]);
                        }
                    }
                    printf("\n");
                }
            } else if (strcasecmp(argv[0], "RDWR_DRVIBIT") == 0) {
                u8_l func = 0;
                printk("read/write pa drv_ibit\n");
                if (argc > 1) {
                    func = (u8_l)command_strtoul(argv[1], NULL, 16);
                }
                if (func == 0) { // read cur
                    rwnx_send_rftest_req(rwnx_hw, RDWR_DRVIBIT, 0, NULL, &cfm);
                } else if (func == 1) { // write 2.4g pa drv_ibit
                    if (argc > 2) {
                        u8_l ibit = (u8_l)command_strtoul(argv[2], NULL, 16);
                        u8_l buf[2] = {func, ibit};
                        printk("set drvibit:[%x]=%x\r\n", func, ibit);
                        rwnx_send_rftest_req(rwnx_hw, RDWR_DRVIBIT, sizeof(buf), buf, &cfm);
                    } else {
                        printk("wrong args\n");
                    }
                } else {
                    printk("wrong func: %x\n", func);
                }
                //memcpy(command, &cfm.rftest_result[0], 16);
                bytes_written = 16;
                char *buff = &cfm.rftest_result[0];
                printk("done: 2.4g txgain tbl pa drv_ibit:\n");
                int idx;
                for (idx = 0; idx < 16; idx++) {
                    printf(" %x", buff[idx]);
                    if (!((idx + 1) & 0x03)) {
                        printf(" [%x~%x]\n", idx - 3, idx);
                    }
                }
            } else if (strcasecmp(argv[0], "RDWR_EFUSE_PWROFST") == 0) {
                u8_l func = 0;
                printk("read/write txpwr offset into efuse\n");
                if (argc > 1) {
                    func = (u8_l)command_strtoul(argv[1], NULL, 16);
                }
                if (func == 0) { // read cur
                    rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_PWROFST, 0, NULL, &cfm);
                } else if (func <= 2) { // write 2.4g/5g pwr ofst
                    if ((argc > 4) && (rwnx_hw->chipid == PRODUCT_ID_AIC8800D80)) {
                        u8_l type = (u8_l)command_strtoul(argv[2], NULL, 16);
                        u8_l chgrp = (u8_l)command_strtoul(argv[3], NULL, 16);
                        s8_l pwrofst = (u8_l)command_strtoul(argv[4], NULL, 10);
                        u8_l buf[4] = {func, type, chgrp, (u8_l)pwrofst};
                        printk("set efuse pwrofst_%s:[%x][%x]=%d\r\n", (func == 1) ? "2.4g" : "5g", type, chgrp, pwrofst);
                        rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_PWROFST, sizeof(buf), buf, &cfm);
                    } else if ((argc > 3) && (rwnx_hw->chipid != PRODUCT_ID_AIC8800D80)) {
    					u8_l chgrp = (u8_l)command_strtoul(argv[2], NULL, 16);
    					s8_l pwrofst = (u8_l)command_strtoul(argv[3], NULL, 10);
    					u8_l buf[3] = {func, chgrp, (u8_l)pwrofst};
    					printk("set efuse pwrofst_%s:[%x]=%d\r\n", (func == 1) ? "2.4g" : "5g", chgrp, pwrofst);
    					rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_PWROFST, sizeof(buf), buf, &cfm);
    				} else {
    					printk("wrong args\n");
    					bytes_written = -EINVAL;
    					break;
    				}
                } else {
                    printk("wrong func: %x\n", func);
                }
                //memcpy(command, &cfm.rftest_result[0], 7);
                bytes_written = 7;
                signed char *buff = &cfm.rftest_result[0];
                if (rwnx_hw->chipid == PRODUCT_ID_AIC8801) {
#if defined(CONFIG_AIC8801)
                printf("done:\n"
                       "efuse txpwr offset 2.4g:\n"
                       "  [0]=%d(ch1~4)\n"
                       "  [1]=%d(ch5~9)\n"
                       "  [2]=%d(ch10~13)\n", (int8_t)buff[0], (int8_t)buff[1], (int8_t)buff[2]);
                printf("efuse txpwr offset 5g:\n"
                       "  [0]=%d(ch36~64)\n"
                       "  [1]=%d(ch100~120)\n"
                       "  [2]=%d(ch122~140)\n"
                       "  [3]=%d(ch142~165)\n", (int8_t)buff[3], (int8_t)buff[4], (int8_t)buff[5], (int8_t)buff[6]);
#endif /* CONFIG_AIC8801 */
                } else if (rwnx_hw->chipid == PRODUCT_ID_AIC8800D80){
                    int type, ch_grp;
                    printf("done:\n"
                        "pwrofst2x 2.4g: [0]:11b, [1]:ofdm_highrate, [2]:ofdm_lowrate\n"
                        "  chan=" "\t1-4" "\t5-9" "\t10-13");
                    for (type = 0; type < 3; type++) {
                        printf("\n  [%d] =", type);
                        for (ch_grp = 0; ch_grp < 3; ch_grp++) {
                            printf("\t%d", buff[3 * type + ch_grp]);
                        }
                    }
                    printf("\npwrofst2x 5g: [0]:ofdm_lowrate, [1]:ofdm_highrate, [2]:ofdm_midrate\n"
                        "  chan=" "\t36-50" "\t51-64" "\t98-114" "\t115-130" "\t131-146" "\t147-166");
                    buff = (signed char *)&buff[3 * 3];
                    for (type = 0; type < 3; type++) {
                        printf("\n  [%d] =", type);
                        for (ch_grp = 0; ch_grp < 6; ch_grp++) {
                            printf("\t%d", buff[6 * type + ch_grp]);
                        }
                    }
                    printf("\n");
                } else if (rwnx_hw->chipid == PRODUCT_ID_AIC8800DC || rwnx_hw->chipid == PRODUCT_ID_AIC8800DW){
                    printf("done:\n"
                           "efuse txpwr offset 2.4g:\n"
                           "  [0]=%d(remain:%x, ch1~4)\n"
                           "  [1]=%d(remain:%x, ch5~9)\n"
                           "  [2]=%d(remain:%x, ch10~13)\n",
                           (int8_t)buff[0], (int8_t)buff[3],
                           (int8_t)buff[1], (int8_t)buff[4],
                           (int8_t)buff[2], (int8_t)buff[5]);
                    // if (ret > 6) { // 5g_en
                    //     printf("efuse txpwr offset 5g:\n"
                    //            "  [0]=%d(remain:%x, ch36~64)\n"
                    //            "  [1]=%d(remain:%x, ch100~120)\n"
                    //            "  [2]=%d(remain:%x, ch122~140)\n"
                    //            "  [3]=%d(remain:%x, ch142~165)\n",
                    //            (int8_t)buff[6], (int8_t)buff[10],
                    //            (int8_t)buff[7], (int8_t)buff[11],
                    //            (int8_t)buff[8], (int8_t)buff[12],
                    //            (int8_t)buff[9], (int8_t)buff[13]);
                    // }
                }
            } else if (strcasecmp(argv[0], "RDWR_EFUSE_DRVIBIT") == 0) {
                u8_l func = 0;
                printk("read/write pa drv_ibit into efuse\n");
                if (argc > 1) {
                    func = (u8_l)command_strtoul(argv[1], NULL, 16);
                }
                if (func == 0) { // read cur
                    rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_DRVIBIT, 0, NULL, &cfm);
                } else if (func == 1) { // write 2.4g pa drv_ibit
                    if (argc > 2) {
                    u8_l ibit = (u8_l)command_strtoul(argv[2], NULL, 16);
                    u8_l buf[2] = {func, ibit};
                    printk("set efuse drvibit:[%x]=%x\r\n", func, ibit);
                    rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_DRVIBIT, sizeof(buf), buf, &cfm);
                    } else {
                        printk("wrong args\n");
                    }
                } else {
                    printk("wrong func: %x\n", func);
                }
                //memcpy(command, &cfm.rftest_result[0], 4);
                bytes_written = 4;
                if (rwnx_hw->chipid == PRODUCT_ID_AIC8801 || rwnx_hw->chipid == PRODUCT_ID_AIC8800D80) {
                    printf("done: efsue 2.4g txgain tbl pa drv_ibit: %x\n", cfm.rftest_result[0]);
                } else if (rwnx_hw->chipid == PRODUCT_ID_AIC8800DC || rwnx_hw->chipid == PRODUCT_ID_AIC8800DW) {
                    int val = *(int *)&cfm.rftest_result[0];
                    if (val < 0) {
                        printf("failed to rd/wr efuse drv_ibit, ret=%d\n", val);
                    } else {
                        printf("done: efsue 2.4g txgain tbl pa drv_ibit: %x (remain: %x)\n", cfm.rftest_result[0], cfm.rftest_result[1]);
                    }
                }
            } else if (strcasecmp(argv[0], "SET_PAPR") == 0) {
    			printk("set papr\n");
    			if (argc > 1) {
    				u8_l func = (u8_l) command_strtoul(argv[1], NULL, 10);
    				printk("papr %d\r\n", func);
    				rwnx_send_rftest_req(rwnx_hw, SET_PAPR, sizeof(func), &func, NULL);
    			} else {
    				printk("wrong args\n");
    				break;
    			}
    		} else if (strcasecmp(argv[0], "SET_NOTCH") == 0) {
    			if (argc > 1) {
    				u8_l func = (u8_l) command_strtoul(argv[1], NULL, 10);
    				printk("set notch %d\r\n", func);
    				rwnx_send_rftest_req(rwnx_hw, SET_NOTCH, sizeof(func), &func, NULL);
    			} else {
    				printk("wrong args\n");
    				break;
    			}
		    } else if (strcasecmp(argv[0], "RDWR_PWROFSTFINE") == 0) {
                u8_l func = 0;
                printk("read/write txpwr offset fine\n");
                if (argc > 1) {
                    func = (u8_l)command_strtoul(argv[1], NULL, 16);
                }
                if (func == 0) { // read cur
                    rwnx_send_rftest_req(rwnx_hw, RDWR_PWROFSTFINE, 0, NULL, &cfm);
                } else if (func <= 2) { // write 2.4g/5g pwr ofst
                    if (argc > 3) {
                        u8_l chgrp = (u8_l)command_strtoul(argv[2], NULL, 16);
                        s8_l pwrofst = (u8_l)command_strtoul(argv[3], NULL, 10);
                        u8_l buf[3] = {func, chgrp, (u8_l)pwrofst};
                        printk("set pwrofstfine:[%x][%x]=%d\r\n", func, chgrp, pwrofst);
                        rwnx_send_rftest_req(rwnx_hw, RDWR_PWROFSTFINE, sizeof(buf), buf, &cfm);
                    } else {
                        printk("wrong args\n");
                        //bytes_written = -EINVAL;
                        break;
                    }
                } else {
                    printk("wrong func: %x\n", func);
                    //bytes_written = -EINVAL;
                    break;
                }
                //memcpy(command, &cfm.rftest_result[0], 7);
                bytes_written = 7;
                signed char *buff = (signed char *)&cfm.rftest_result[0];
                printf("done:\n"
                       "txpwr offset fine 2.4g: \n"
                       "  [0]=%d(ch1~4)\n"
                       "  [1]=%d(ch5~9)\n"
                       "  [2]=%d(ch10~13)\n", (int8_t)buff[0], (int8_t)buff[1], (int8_t)buff[2]);
                printf("txpwr offset fine 5g:\n"
                       "  [0]=%d(ch36~64)\n"
                       "  [1]=%d(ch100~120)\n"
                       "  [2]=%d(ch122~140)\n"
                       "  [3]=%d(ch142~165)\n", (int8_t)buff[3], (int8_t)buff[4], (int8_t)buff[5], (int8_t)buff[6]);
    		} else if (strcasecmp(argv[0], "RDWR_EFUSE_PWROFSTFINE") == 0) {
                u8_l func = 0;
                printk("read/write txpwr offset fine into efuse\n");
                if (argc > 1) {
                    func = (u8_l)command_strtoul(argv[1], NULL, 16);
                }
                if (func == 0) { // read cur
                    rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_PWROFSTFINE, 0, NULL, &cfm);
                } else if (func <= 2) { // write 2.4g/5g pwr ofst
                    if (argc > 3) {
                        u8_l chgrp = (u8_l)command_strtoul(argv[2], NULL, 16);
                        s8_l pwrofst = (u8_l)command_strtoul(argv[3], NULL, 10);
                        u8_l buf[3] = {func, chgrp, (u8_l)pwrofst};
                        printk("set efuse pwrofstfine:[%x][%x]=%d\r\n", func, chgrp, pwrofst);
                        rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_PWROFSTFINE, sizeof(buf), buf, &cfm);
                    } else {
                        printk("wrong args\n");
                        //bytes_written = -EINVAL;
                        break;
                    }
                } else {
                    printk("wrong func: %x\n", func);
                    //bytes_written = -EINVAL;
                    break;
                }
                signed char *buff = (signed char *)&cfm.rftest_result[0];
                if ((rwnx_hw->chipid == PRODUCT_ID_AIC8800DC) ||
                    (rwnx_hw->chipid == PRODUCT_ID_AIC8800DW)) { // 6 = 3 (2.4g) * 2
                    //memcpy(command, &cfm.rftest_result[0], 6);
                    bytes_written = 6;
                    printf("done:\n"
                           "efuse txpwr offset fine 2.4g:\n"
                           "  [0]=%d(remain:%x, ch1~4)\n"
                           "  [1]=%d(remain:%x, ch5~9)\n"
                           "  [2]=%d(remain:%x, ch10~13)\n",
                           (int8_t)buff[0], (int8_t)buff[3],
                           (int8_t)buff[1], (int8_t)buff[4],
                           (int8_t)buff[2], (int8_t)buff[5]);
                } else { // 7 = 3(2.4g) + 4(5g)
                    //memcpy(command, &cfm.rftest_result[0], 7);
                    bytes_written = 7;
                    printf("done:\n"
                           "efuse txpwr offset fine 2.4g:\n"
                           "  [0]=%d(ch1~4)\n"
                           "  [1]=%d(ch5~9)\n"
                           "  [2]=%d(ch10~13)\n", (int8_t)buff[0], (int8_t)buff[1], (int8_t)buff[2]);
                    printf("efuse txpwr offset fine 5g:\n"
                           "  [0]=%d(ch36~64)\n"
                           "  [1]=%d(ch100~120)\n"
                           "  [2]=%d(ch122~140)\n"
                           "  [3]=%d(ch142~165)\n", (int8_t)buff[3], (int8_t)buff[4], (int8_t)buff[5], (int8_t)buff[6]);
                }
    		} else if (strcasecmp(argv[0], "RDWR_EFUSE_SDIOCFG") == 0) {
                u8_l func = 0;
                printk("read/write sdiocfg_bit into efuse\n");
                if (argc > 1) {
                    func = (u8_l)command_strtoul(argv[1], NULL, 16);
                }
                if (func == 0) { // read cur
                    rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_SDIOCFG, 0, NULL, &cfm);
                } else if (func == 1) { // write sdiocfg
                    if (argc > 2) {
                    u8_l ibit = (u8_l)command_strtoul(argv[2], NULL, 16);
                    u8_l buf[2] = {func, ibit};
                    printk("set efuse sdiocfg:[%x]=%x\r\n", func, ibit);
                    rwnx_send_rftest_req(rwnx_hw, RDWR_EFUSE_SDIOCFG, sizeof(buf), buf, &cfm);
                    } else {
                        printk("wrong args\n");
                        //bytes_written = -EINVAL;
                        break;
                    }
                } else {
                    printk("wrong func: %x\n", func);
                    //bytes_written = -EINVAL;
                    break;
                }
                //memcpy(command, &cfm.rftest_result[0], 4);
                bytes_written = 4;
                printf("done: efsue sdio cfg: %x\n", cfm.rftest_result[0]);
    		} else if (strcasecmp(argv[0], "SET_SRRC") == 0) {
    			if (argc > 1) {
    				u8_l func = (u8_l) command_strtoul(argv[1], NULL, 10);
    				printk("set srrc %d\r\n", func);
    				rwnx_send_rftest_req(rwnx_hw, SET_SRRC, sizeof(func), &func, NULL);
    			} else {
    				printk("wrong args\n");
    				break;
    			}
			} else if (strcasecmp(argv[0], "SET_FSS") == 0) {
    			if (argc > 1) {
    				u8_l func = (u8_l) command_strtoul(argv[1], NULL, 10);
    				printk("set fss %d\r\n", func);
    				rwnx_send_rftest_req(rwnx_hw, SET_FSS, sizeof(func), &func, NULL);
    			} else {
    				printk("wrong args\n");
    				break;
    			}
            } else if (strcasecmp(argv[0], "bt_test") == 0) {
                int ret = parse_cmd_line(argc, argv);

                if(!ret){
                    if(server_or_client == SERVER){
                        start_server();
                    } else if (server_or_client == CLIENT) {
                        send_hci_command();
                    } else if (server_or_client == CLIENT_DIRECT) {
                        if (bt_test_args.cmd == ble_start) {
                           // send_to_server((unsigned char*)hci_reset, sizeof(hci_reset));
                        } else if (bt_test_args.cmd == dut) {
                          //  send_to_server((unsigned char*)hci_reset, sizeof(hci_reset));
                         //   send_to_server((unsigned char*)set_filter, sizeof(set_filter));
                         //   send_to_server((unsigned char*)scan_on, sizeof(scan_on));
                         //   send_to_server((unsigned char*)dut_en, sizeof(dut_en));
                            return 0;
                        }
                       // send_raw_hci_command();
                    }
                }
    		} else {
                printk("%s: wrong cmd(%s) in mode:%x\n", __func__, argv[0], rwnx_hw->mode);
            }
        } while(0);
    } else
    #endif /* CONFIG_WIFI_MODE_RFTEST */
    {
        u8_l sta_idx;
        u8_l bw;
        u8_l format_idx;
        u16_l rate_idx;
        u8_l pre_type;
        do
        {
            if (strcasecmp(argv[0], "TEST_CMD") ==0) {
                printk("this is test cmd\n");
            } else if (strcasecmp(argv[0], "SCAN_OPEN") == 0) {
                printk("scan open\n");

                wlan_if_scan_open();
            } else if (strcasecmp(argv[0], "SCAN") == 0) {
                printk("scan\n");

                wlan_if_scan();
            } else if (strcasecmp(argv[0], "GET_SCAN") == 0) {
                printk("get scan\n");
                wifi_ap_list_t ap_list;

                wlan_if_getscan(&ap_list);
            } else if (strcasecmp(argv[0], "SCAN_CLOSE") == 0) {
                printk("scan close\n");

                wlan_if_scan_close();
            }  else if (strcasecmp(argv[0], "STOP_STA") == 0) {
                printk("stop sta\n");
                wlan_disconnect_sta(0);
            } else if (strcasecmp(argv[0], "START_STA") == 0) {
                printk("start station\n");
		        rwnx_hw->net_id = wlan_start_sta(NULL, NULL, -1);
		#if 0
                if (argc == 2) {
                    printk("connect unencrypted ap\n");
                    rwnx_hw->net_id = wlan_start_sta(argv[1], NULL, -1);
                } else if (argc == 3) {
                    printk("connect encrypted ap\n");
                    rwnx_hw->net_id = wlan_start_sta(argv[1], argv[2], -1);
                } else {
                    printk("wrong param\n");
                    break;
                }
		#endif
            } else if (strcasecmp(argv[0], "CONN") == 0) {
                printk("station connect\n");
                if (argc == 2) {
                    printk("connect unencrypted ap\n");
                    wlan_sta_connect(argv[1], NULL, -1);
                } else if (argc == 3) {
                    printk("connect encrypted ap\n");
                    wlan_sta_connect(argv[1], argv[2], -1);
                } else {
                    printk("wrong param\n");
                    break;
                }
            } else if (strcasecmp(argv[0], "START_AP") == 0) {
                printk("start ap\n");
                if (argc <  3) {
                    printk("wrong param, need ssid/pw\n");
                    break;
                }
                u8_l band    = PHY_BAND_2G4;
                u8_l channel = 6;
                u8_l type = PHY_CHNL_BW_40;
                u8_l he_enable = 1;
                u8_l sercurity_type = KEY_WPA2;
                u8_l enable_acs = 0;
                u8_l acs_scan_num = 3;

                if (argc >  2)
                    band    = (u8_l) command_strtoul(argv[3], NULL, 10);
                if (argc >  3)
                    channel = (u8_l) command_strtoul(argv[4], NULL, 10);
                if (argc >  4)
                    type = (u8_l) command_strtoul(argv[5], NULL, 10);
                if (argc >  5)
                    he_enable = (u8_l) command_strtoul(argv[6], NULL, 10);
                if (argc >  6)
                    sercurity_type = (u8_l) command_strtoul(argv[7], NULL, 10);
                if (argc >  7)
                    enable_acs = (u8_l) command_strtoul(argv[8], NULL, 10);
                // if (argc >  9)
                //     acs_scan_num = (u8_l) command_strtoul(argv[9], NULL, 10);

                // #define AP_SSID_STRING  "AIC-AP"
                // #define AP_PASS_STRING  "00000000"
                struct aic_ap_cfg cfg;
                memset(&cfg, 0, sizeof(cfg));
                cfg.band = band;//PHY_BAND_2G4;
                cfg.channel = channel;//6;
                cfg.type = type;//PHY_CHNL_BW_40;
                cfg.max_inactivity = 60;
                cfg.enable_he = he_enable;//1;
            	cfg.bcn_interval = 100;
                cfg.sercurity_type = sercurity_type;//KEY_WPA2;
            	cfg.sta_num = 32;
            	cfg.enable_acs = enable_acs;
                cfg.acs_scan_num = acs_scan_num;
                memcpy(cfg.aic_ap_ssid.array, argv[1], strlen(argv[1]));
                cfg.aic_ap_ssid.length = strlen(cfg.aic_ap_ssid.array);
                if (sercurity_type < 2 ) {
                    cfg.aic_ap_passwd.length = 0;
                } else {
                    memcpy(cfg.aic_ap_passwd.array, argv[2], strlen(argv[2]));
                    cfg.aic_ap_passwd.length = strlen(cfg.aic_ap_passwd.array);
                }
                extern void aic_set_pref_chan_list(unsigned int chan_list[], unsigned int num);
                //unsigned int p_chan_list[] = {149, 153, 157, 161};
                unsigned int p_chan_list[] = {36, 40, 44, 48};
                aic_set_pref_chan_list(p_chan_list, 4);
                // #undef AP_SSID_STRING
                // #undef AP_PASS_STRING
                aic_wifi_init(WIFI_MODE_AP, 0, &cfg);
            } else if (strcasecmp(argv[0], "STOP_AP") == 0) {
                printk("stop ap\n");
                aic_wifi_deinit(WIFI_MODE_AP);
            } else if (strcasecmp(argv[0], "START_P2P") == 0) {
                printk("start p2p\n");
                if (argc <  3) {
                    printk("wrong param, need ssid/pw\n");
                    break;
                }
                uint8_t ssi_str[65] = {0};
                u8_l band    = PHY_BAND_2G4;
                u8_l channel = 6;
                u8_l type = PHY_CHNL_BW_40;
                u8_l he_enable = 1;
                u8_l sercurity_type = KEY_WPA2;
                u8_l acs_enable = 0;

                if (argc >  3)
                    band    = (u8_l) command_strtoul(argv[3], NULL, 10);
                if (argc >  4)
                    channel = (u8_l) command_strtoul(argv[4], NULL, 10);
                if (argc >  5)
                    type = (u8_l) command_strtoul(argv[5], NULL, 10);
                if (argc >  6)
                    he_enable = (u8_l) command_strtoul(argv[6], NULL, 10);
                if (argc >  7)
                    sercurity_type = (u8_l) command_strtoul(argv[7], NULL, 10);
                if (argc >  8)
                    acs_enable = (u8_l) command_strtoul(argv[8], NULL, 10);

                snprintf(ssi_str, 65, "%s", argv[1]);
                struct aic_p2p_cfg cfg;
                memset(&cfg, 0, sizeof(cfg));
                cfg.band = band;//PHY_BAND_5G;
                cfg.channel = channel;//36;
                cfg.type = type;//PHY_CHNL_BW_40;
                cfg.enable_he = he_enable;//1;
                cfg.enable_acs = acs_enable;
                memcpy(cfg.aic_p2p_ssid.array, ssi_str, strlen(ssi_str));
                memcpy(cfg.aic_ap_passwd.array, argv[2], strlen(argv[2]));
                cfg.aic_p2p_ssid.length = strlen(cfg.aic_p2p_ssid.array);
                cfg.aic_ap_passwd.length = strlen(cfg.aic_ap_passwd.array);

                // extern void aic_set_pref_chan_list(unsigned int chan_list[], unsigned int num);
                // unsigned int p_chan_list[] = {149, 153, 157, 161};
                // aic_set_pref_chan_list(p_chan_list, 4);
                user_p2p_start(&cfg);
                // #undef P2P_SSID_STRING
                // #undef P2P_PASS_STRING
            } else if (strcasecmp(argv[0], "WPS_BUTTON") == 0) {
                user_wps_button_pushed();
            } else if (strcasecmp(argv[0], "SWITCH_CH") == 0) {
                wlan_ap_switch_channel(149);
            } else if (strcasecmp(argv[0], "MONITOR") == 0) {
                extern int do_monitor(int argc, char * const argv[]);
                do_monitor(argc, argv);
            } else if (strcasecmp(argv[0], "MONITOR_TX") == 0) {
                extern int do_monitor_tx(int argc, char * const argv[]);
                do_monitor_tx(argc, argv);
            } else if (strcasecmp(argv[0], "MONITOR_FILTER") == 0) {
                extern int do_monitor_set_filter(int argc, char * const argv[]);
                do_monitor_set_filter(argc, argv);
            } else if (strcasecmp(argv[0], "MONITOR_SET_CHAN") == 0) {
                extern int do_monitor_set_channel(int argc, char * const argv[]);
                do_monitor_set_channel(argc, argv);
            }
              #ifdef CONFIG_VENDOR_IE
              else if (strcasecmp(argv[0], "ADD_VENDOR_IE") == 0) {
                aic_add_custom_ie(argv[1]);
            } else if (strcasecmp(argv[0], "UPDATE_VENDOR_IE") == 0) {
                aic_update_custom_ie(argv[1]);
            } else if (strcasecmp(argv[0], "DELETE_VENDOR_IE") == 0) {
                aic_del_custom_ie();
            }
              #endif
              else if (strcasecmp(argv[0], "ADD_BLACK") == 0) {
                printk("Add Blacklist\n");
                struct mac_addr macaddr;
                uint8_t addr[] = {0xFE, 0x37, 0xB4, 0x23, 0x56, 0x4c};
                memcpy(&macaddr, addr, 6);
                wlan_ap_add_blacklist(&macaddr);
                wlan_ap_set_mac_acl_mode(1);
            } else if (strcasecmp(argv[0], "DEL_BLACK") == 0) {
                printk("Del Blacklist\n");
                struct mac_addr macaddr;
                uint8_t addr[] = {0xFE, 0x37, 0xB4, 0x23, 0x56, 0x4c};
                memcpy(&macaddr, addr, 6);
                wlan_ap_delete_blacklist(&macaddr);
            }  else if (strcasecmp(argv[0], "GET_ACL") == 0) {
                printk("Get ACL list\n");
                uint8_t index, cnt = wlan_ap_get_mac_acl_list_cnt();
                void *list = wlan_ap_get_mac_acl_list();

                index = 0;
                struct co_list_hdr *list_hdr = co_list_pick(list);
                while (list_hdr != NULL)
                {
                    struct wifi_mac_node *marked_sta = (struct sta_info_tag *)list_hdr;
                    printk("ACL list[%d] = %02x:%02x:%02x:%02x:%02x:%02x\n", index, marked_sta->mac[0], marked_sta->mac[1], marked_sta->mac[2],
                    marked_sta->mac[3], marked_sta->mac[4], marked_sta->mac[5]);
                    list_hdr = co_list_next(list_hdr);
                    index ++;
                }
            } else if (strcasecmp(argv[0], "ADD_WHITE") == 0) {
                printk("Add Whitelist\n");
                struct mac_addr macaddr;
                uint8_t addr[] = {0xFE, 0x37, 0xB4, 0x23, 0x56, 0x4c};
                memcpy(&macaddr, addr, 6);
                wlan_ap_add_whitelist(&macaddr);
                wlan_ap_set_mac_acl_mode(2);
            } else if (strcasecmp(argv[0], "DEL_WHITE") == 0) {
                printk("Del Whitelist\n");
                struct mac_addr macaddr;
                uint8_t addr[] = {0xFE, 0x37, 0xB4, 0x23, 0x56, 0x4c};
                memcpy(&macaddr, addr, 6);
                wlan_ap_delete_whitelist(&macaddr);
            } else if (strcasecmp(argv[0], "ACL_DIS") == 0) {
                printk("ACL disable\n");
                wlan_ap_set_mac_acl_mode(0);
            } else if (strcasecmp(argv[0], "GET_STAS") == 0) {
                printk("Get stas\n");
                uint8_t index, cnt = wlan_ap_get_associated_sta_cnt();
                void *sta_list = wlan_ap_get_associated_sta_list();

                index = 0;
                struct co_list_hdr *list_hdr = co_list_pick(sta_list);
                while (list_hdr != NULL)
                {
                    struct sta_info_tag *sta = (struct sta_info_tag *)list_hdr;
                    printk("STA[%d] = %x:%x:%x\n", index, sta->mac_addr.array[0], sta->mac_addr.array[1], sta->mac_addr.array[2]);
                    list_hdr = co_list_next(list_hdr);
                    index ++;
                }
            } else if (strcasecmp(argv[0], "GET_RSSI") == 0) {
                    uint8_t addr[] = {0xFE, 0x37, 0xB4, 0x23, 0x56, 0x4c};
                    printk("RSSI[ %02x:%02x:%02x:%02x:%02x:%02x] = %d\n", addr[0], addr[1],
                    addr[2], addr[3], addr[4], addr[5], wlan_ap_get_associated_sta_rssi(addr));

            }else if (strcasecmp(argv[0], "SET_RATE") == 0) {
                uint8_t dcm = 0;
                if (argc >= 5) {
                    printk("set rate\n");
                    struct fhost_vif_tag *fhost_vif = &fhost_env.vif[0];
                    struct vif_info_tag *mac_vif = fhost_vif->mac_vif;
                    if (mac_vif->type == VIF_STA)
                        sta_idx = mac_vif->u.sta.ap_id;
                    else if (mac_vif->type == VIF_AP) {
                        struct vif_info_tag *mac_vif = fhost_to_mac_vif(0);

                        // for softAP test, get first sta
                        struct sta_info_tag *sta = (struct sta_info_tag *)co_list_pick(&mac_vif->sta_list);
                        if(!sta->valid) {
                            break;
                        }
                        sta_idx = vif_mgmt_get_staid(mac_vif, &sta->mac_addr);;
                    }
                    bw = command_strtoul(argv[1], NULL, 10);
                    format_idx = command_strtoul(argv[2], NULL, 10);
                    rate_idx = command_strtoul(argv[3], NULL, 10);
                    pre_type = command_strtoul(argv[4], NULL, 10);
                    if (argc > 5)
                        dcm = command_strtoul(argv[5], NULL, 10);
                    printk("sta_idx:%d, bw:%d, format_idx:%d, rate_idx:%d, pre_type:%d, dcm %d\r\n", sta_idx, bw, format_idx, rate_idx, pre_type, dcm);
                } else {
                    printk("wrong param\n");
                    break;
                }
                fhost_cntrl_cfgrwnx_set_fixed_rate(sta_idx, bw, format_idx, rate_idx, pre_type, dcm);
            } else if (strcasecmp(argv[0], "REBOOT") == 0) {
                    extern void aic_wifi_reboot(void);
                    aic_wifi_reboot();
            } else if (strcasecmp(argv[0], "bleinit") == 0) {
                    server_or_client = 0;

            }else if (strcasecmp(argv[0], "bledeinit") == 0) {

            }else if (strcasecmp(argv[0], "ble_setgapname") == 0) {

            } else if (strcasecmp(argv[0], "ble_advdataset") == 0) {

            } else if (strcasecmp(argv[0], "ble_advdataupdate") == 0) {

            }else if (strcasecmp(argv[0], "ble_scanresponseset") == 0) {

            } else if (strcasecmp(argv[0], "ble_scanresponseupdate") == 0) {

            } else if (strcasecmp(argv[0], "ble_advparamset") == 0) {

            } else if (strcasecmp(argv[0], "ble_advparamupdate") == 0) {

            } else if (strcasecmp(argv[0], "ble_advstop") == 0) {

            } else if (strcasecmp(argv[0], "ble_advstart") == 0) {

            } else if (strcasecmp(argv[0], "ble_disconnect") == 0) {

            } else if (strcasecmp(argv[0], "ble_conparamupdate") == 0) {

            }else if (strcasecmp(argv[0], "ble_deletebond") == 0) {

            }else if (strcasecmp(argv[0], "ble_scanparamset") == 0) {

            }else if (strcasecmp(argv[0], "ble_scanstart") == 0) {

            }else if (strcasecmp(argv[0], "ble_scanstop") == 0) {

            }else if (strcasecmp(argv[0], "ble_smartconfig_sendntf") == 0) {

            }else if (strcasecmp(argv[0], "ble_smartconfig_sendind") == 0) {

            }else if (strcasecmp(argv[0], "ble_smartconfig_updatereadvalue") == 0) {

            }else if (strcasecmp(argv[0], "ble_setprint") == 0) {

            }else if (strcasecmp(argv[0], "ble_udfc_connect") == 0) {

            }else if (strcasecmp(argv[0], "ble_udfc_con_cancel") == 0) {

            }else if (strcasecmp(argv[0], "ble_udfc_write") == 0) {

            }else if (strcasecmp(argv[0], "ble_udfc_write_nores") == 0) {

            }else if (strcasecmp(argv[0], "ble_udfc_read") == 0) {

            }else if (strcasecmp(argv[0], "ble_udfc_ntf_en") == 0) {

            }else if (strcasecmp(argv[0], "ble_udfc_ind_en") == 0) {

            }else if (strcasecmp(argv[0], "ble_setaddr") == 0) {

            }else if (strcasecmp(argv[0], "ble_getaddr") == 0) {

            }else if (strcasecmp(argv[0], "ble_setdatalen") == 0) {

            }else if (strcasecmp(argv[0], "ble_setdefaultmtu") == 0) {

            }else if (strcasecmp(argv[0], "ble_getrssi") == 0) {

            }else if (strcasecmp(argv[0], "ble_setsecmode") == 0) {

            }else if (strcasecmp(argv[0], "ble_hogprh_connect") == 0) {

            }else if (strcasecmp(argv[0], "ble_hogprh_con_cancel") == 0) {

        	} else if(strcasecmp(argv[0], "GET_CS_INFO") == 0) {
                struct aicwf_cs_info cs_info = {0};
                uint8_t sta_addr[6] = {0};
                if (argc > 1) {
                    for (int i = 0; i < 6; i++)
                        sta_addr[i] = (u8_l) command_strtoul(argv[i+1], NULL, 16);
                }

                extern int get_cs_info(u8 *mac_addr, u8 *val);
                get_cs_info(sta_addr, &cs_info);
                printk("phymode=%d(0:B 1:G 2:A 3:N 4:AC 5:AX)\n", cs_info.phymode);
                printk("bandwidth=%d(0:20 1:40 2:80)\n", cs_info.bandwidth);
                printk("freq=%d\n", cs_info.freq);
                printk("rssi=%d\n", cs_info.rssi);
                printk("snr=%d\n", cs_info.snr);
                printk("noise=%d\n", cs_info.noise);
                printk("txpwr=%d\n", cs_info.txpwr);
                //chanutil
                printk("chan rxbusy times=%d/%d(ms), txbusy times=%d/%d(ms)\n", cs_info.chan_time_busy_ms, cs_info.chan_time_ms, cs_info.chan_tx_time_busy_ms, cs_info.chan_time_ms);
                printk("coutry code =%s\n", cs_info.countrycode);
                printk("rx nss=%d, mcs=%x\n", cs_info.rxnss, cs_info.rxmcs);
                printk("tx nss=%d, mcs=%x\n", cs_info.txnss, cs_info.txmcs);
                printk("tx_phyrate=%d(Kbps)\n", cs_info.tx_phyrate);
                printk("rx_phyrate=%d(Kbps)\n", cs_info.rx_phyrate);
                printk("tx_ack_succ_stat=%d\n", cs_info.tx_ack_succ_stat);
                printk("tx_ack_fail_stat=%d\n", cs_info.tx_ack_fail_stat);
                printk("chan=%d\n", get_ap_channel_num());
            } else if(strcasecmp(argv[0], "AUTO_CCA_EN_SET") == 0) {
                struct mm_set_auto_cca_en_req req;

                req.hwconfig_id     = AUTO_CCA_EN_REQ;
                req.enable          = 1;
                req.default_cca_set = 1;
                req.max_cca_thresh  = -40;
                req.default_cca_thresh = -62;
                rwnx_send_vendor_hwconfig_req(g_rwnx_hw, &req);
            } else if(strcasecmp(argv[0], "CCA_THRESHOLD_SET") == 0) {
                struct mm_set_cca_threshold_req req;

                memset(&req, 0, sizeof(req));
                req.hwconfig_id     = CCA_THRESHOLD_REQ;
                req.auto_cca_en     = 0x10;

                rwnx_send_vendor_hwconfig_req(g_rwnx_hw, &req);
            } else if(strcasecmp(argv[0], "MAX_AGG_TX_CNT_SET") == 0) {
                struct mm_set_max_tx_agg_cnt_req req;

				uint8_t mcs_thresh = 0, max_agg_cnt0 = 8, max_agg_cnt1 = 8, max_agg_cnt2 = 8, max_agg_cnt3 = 8;
                if(argc > 1)
                    mcs_thresh = command_strtoul(argv[1], NULL, 10);
                if(argc > 2)
                    max_agg_cnt0 = command_strtoul(argv[2], NULL, 10);
                if(argc > 3)
                    max_agg_cnt1 = command_strtoul(argv[3], NULL, 10);
                if(argc > 4)
                    max_agg_cnt2 = command_strtoul(argv[4], NULL, 10);
                if(argc > 5)
                    max_agg_cnt3 = command_strtoul(argv[5], NULL, 10);
                memset(&req, 0, sizeof(req));
                req.hwconfig_id     = MAX_AGG_TX_CNT_REQ;
                req.enable           = 1;
                req.mcs_thresh      = mcs_thresh;
                req.max_agg_cnt[0]  = max_agg_cnt0;
                req.max_agg_cnt[1]  = max_agg_cnt1;
                req.max_agg_cnt[2]  = max_agg_cnt2;
                req.max_agg_cnt[3]  = max_agg_cnt3;

                rwnx_send_vendor_hwconfig_req(g_rwnx_hw, &req);
            } else if(strcasecmp(argv[0], "DCM_FORCE_EN_SET") == 0) {
                struct mm_set_dcm_force_en_req req;

                memset(&req, 0, sizeof(req));
                req.hwconfig_id     = DCM_FORCE_EN_REQ;
                req.enable           = 1;

                rwnx_send_vendor_hwconfig_req(g_rwnx_hw, &req);
            } else if(strcasecmp(argv[0], "ON_AIR_DUTY_CYCLE_SET") == 0) {
                struct mm_set_on_air_duty_cycle_req req;

                uint8_t percent = command_strtoul(argv[1], NULL, 10);

                memset(&req, 0, sizeof(req));
                req.hwconfig_id     = ON_AIR_DUTY_CYCLE_REQ;
                req.enable          = 1;
                req.percent         = percent;

                rwnx_send_vendor_hwconfig_req(g_rwnx_hw, &req);
            }else if (strcasecmp(argv[0], "SET_COUNTRYCODE") == 0) {
                if (argc > 1) {
                    fhost_country_code_set(argv[1]);
                    printk("country code :%s\r\n", argv[1]);
                } else {
                    printk("wrong param\n");
                }
            } else {
                printk("%s: wrong cmd(%s) in mode:%x\n", __func__, argv[0], rwnx_hw->mode);
            }
        } while(0);
    }
    return bytes_written;
}


