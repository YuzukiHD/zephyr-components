#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <stdbool.h>
#include "queue_client.h"
#include <sys/stat.h>
#include "lbh_bt.h"
#include "lbh_ble.h"
#include "lbh_codec.h"
#include "rtos_al.h"

extern int lbh_server_main(int argc, char *argv[]);
extern int lbh_uart_open(void);
rtos_task_handle lbh_init_task_handle = NULL;

static void start_server(void){
#ifdef LBH_BLE_ENABLE
    app_queue_reg_callback(client_queue_msg_callback);
#endif
#ifdef LBH_BT_ENABLE
    app_queue_reg_bt_msg_callback(client_queue_bt_msg_callback);
#endif
    lbh_client_socket_and_msg_cb_init();
}

void lbh_init_task(void *arg)
{
    char *argvt[3]= {"lbh_server","-s","ble"};
	lbh_uart_open();
	lbh_server_main(3,argvt);
    start_server();
	printf("%s exit \n",__func__);
	rtos_task_delete(rtos_get_current_task());
}

int lbh_open(void){
	int ret ;
    ret = rtos_task_create(lbh_init_task, "lbh_init_task", BT_TASK_MIN,
                           6144, NULL, AIC_PRIORITY_NORMAL,
                           &lbh_init_task_handle);
    if (ret || (lbh_init_task_handle == NULL)) {
        printf("lbh_init_task  create fail,%d\n",ret);
        return ret;
    }
	return ret;
}

