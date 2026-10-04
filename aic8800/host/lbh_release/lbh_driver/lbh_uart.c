/*
* Copyright (c) 2019-2025 Allwinner Technology Co., Ltd. ALL rights reserved.
*
* Allwinner is a trademark of Allwinner Technology Co.,Ltd., registered in
* the the people's Republic of China and other countries.
* All Allwinner Technology Co.,Ltd. trademarks are used with permission.
*
* DISCLAIMER
* THIRD PARTY LICENCES MAY BE REQUIRED TO IMPLEMENT THE SOLUTION/PRODUCT.
* IF YOU NEED TO INTEGRATE THIRD PARTY'S TECHNOLOGY (SONY, DTS, DOLBY, AVS OR MPEGLA, ETC.)
* IN ALLWINNERS'SDK OR PRODUCTS, YOU SHALL BE SOLELY RESPONSIBLE TO OBTAIN
* ALL APPROPRIATELY REQUIRED THIRD PARTY LICENCES.
* ALLWINNER SHALL HAVE NO WARRANTY, INDEMNITY OR OTHER OBLIGATIONS WITH RESPECT TO MATTERS
* COVERED UNDER ANY REQUIRED THIRD PARTY LICENSE.
* YOU ARE SOLELY RESPONSIBLE FOR YOUR USAGE OF THIRD PARTY'S TECHNOLOGY.
*
*
* THIS SOFTWARE IS PROVIDED BY ALLWINNER"AS IS" AND TO THE MAXIMUM EXTENT
* PERMITTED BY LAW, ALLWINNER EXPRESSLY DISCLAIMS ALL WARRANTIES OF ANY KIND,
* WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING WITHOUT LIMITATION REGARDING
* THE TITLE, NON-INFRINGEMENT, ACCURACY, CONDITION, COMPLETENESS, PERFORMANCE
* OR MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
* IN NO EVENT SHALL ALLWINNER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
* NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS, OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
* OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <hal_log.h>
#include <hal_cmd.h>
#include <hal_timer.h>
#include <hal_uart.h>

#include "rtos_al.h"
#include "rtos_errno.h"

typedef enum{
	HCI_MSGTYPE_ST,
	HCI_LEN_ST,
	HCI_DATA_ST,
	HCI_DONE_ST,
}HCI_RXST_T;

#define HCI_TYPE_LEN        1
#define HCI_EVT_HEADER_LEN  2
#define HCI_ACL_HEADER_LEN  4
#define HCI_SCO_HEADER_LEN  3

#define HCI_CMD_PKT         0X01
#define HCI_ACL_PKT         0X02
#define HCI_SCO_PKT         0X03
#define HCI_EVT_PKT         0X04


rtos_queue bt_driver_server_queue = NULL;//fw to host
rtos_queue bt_server_driver_queue = NULL;//host to fw
static uint8_t *tmp_buf = NULL;
static uint8_t *tbuf = NULL;
static uint8_t *rbuf = NULL;
static uart_port_t port;
static rtos_task_handle lbh_uart_recv_task_handle = NULL;
static rtos_task_handle lbh_uart_send_task_handle = NULL;
static int lbh_uart_recv_task_running = 0;
static int lbh_uart_send_task_running = 0;

void lbh_uart_close(void);


static unsigned int lbh_get_print_en(void)
{
	return 0;
}

#define BUFFSIZE 1024

static int lbh_memcpy(unsigned char *dest,unsigned char *src,int len)
{
	if(!dest || !src){
        printf("%s error\n",__func__);
		return -1;
	}
	for(int i = 0; i < len; i++){
		dest[i] = src[i];
	}
	return 0;
}

static void print_data(char * data, int len)
{
	int i = 0;
	printf("len:%d \n",len);
	for(i=0;i<len;i++){
		printf("%02x ",data[i]);
	}
	printf("\n");
}

static int bt_hci_data_read_process(uint8_t *buffer,int len)
{
    int ret_len = -1, ret = 0;
    bool read_done = false;
    int len_header= 0;
    int len_type = 0;
    int len_data= 0;
    int offset = 0;

    HCI_RXST_T state = HCI_MSGTYPE_ST;
    uint8_t evt_len = 0;
    uint16_t acl_len = 0;
    uint8_t sco_len = 0;
    uint16_t check_offset = 0;
	if(buffer == NULL){
		printf("%s  buffer is NULL\r\n");
		return -1;
	}

    while(!read_done){
        switch(state){
            case HCI_MSGTYPE_ST:
                {
					printf("HCI_MSGTYPE_ST  0x%x, 0x%x, %d\r\n",buffer,tmp_buf);
                    len_type = hal_uart_receive(port, &tmp_buf[0], HCI_TYPE_LEN);
                    if (len_type != HCI_TYPE_LEN) {
                        printf("read  len_type error \n");
                        print_data(tmp_buf, len_type);
                        ret = -ESTRPIPE;
                        goto fail;
                    }
                    buffer[0] = tmp_buf[0];
					printf("type:  %x\r\n",buffer[0]);
                    state = HCI_LEN_ST;
                    if (lbh_get_print_en()){
					    print_data(tmp_buf, len_type);
                    }
                    offset = 0;
                }
                break;
            case HCI_LEN_ST:
                {
					printf("HCI_LEN_ST \r\n");
                    if(buffer[0] == HCI_EVT_PKT){
                        len_header = hal_uart_receive(port, (tmp_buf+offset), (HCI_EVT_HEADER_LEN-offset));
                        if (!len_header) {
                            printf("read  evt len_header error\r\n");
                            ret = -ESTRPIPE;
                            goto fail;
                        }
                        if ((len_header+offset) < HCI_EVT_HEADER_LEN) {
                            if (lbh_get_print_en()){
                                printf("read  evt len_header continue,%d \n",len_header);
                            }
                            offset += len_header;
                            break;
                        }
                        if (lbh_get_print_en()){
                            print_data(tmp_buf, len_header);
                        }
                        evt_len = tmp_buf[1];
						printf("evt_len = %d ,len_type=%d\n",evt_len,len_type);
                    }else if(buffer[0] == HCI_ACL_PKT){
                        len_header = hal_uart_receive(port, (tmp_buf+offset), (HCI_ACL_HEADER_LEN-offset));
                        if (!len_header) {
                            printf("read  acl len_header error\r\n");
                            ret = -ESTRPIPE;
                            goto fail;
                        }
                        if ((len_header+offset) < HCI_ACL_HEADER_LEN) {
                            if (lbh_get_print_en()){
                                printf("read  acl len_header continue %d \n",len_header);
                            }
                            offset += len_header;
                            break;
                        }
                        acl_len = (uint16_t)tmp_buf[2] |((uint16_t)tmp_buf[3]<<8);
                        if (lbh_get_print_en()){
						    printf("acl_len = %d \n",acl_len);
                        }
                    }else if(buffer[0] == HCI_SCO_PKT){
                        len_header = hal_uart_receive(port, (tmp_buf+offset), (HCI_SCO_HEADER_LEN-offset));
                        if (!len_header) {
                            printf("read  sco len_header error\r\n");
                            ret = -ESTRPIPE;
                            goto fail;
                        }
                        if ((len_header+offset) < HCI_SCO_HEADER_LEN) {
                            printf("read  sco len_header continue %d \n",len_header);
                            offset += len_header;
                            break;
                        }
                        sco_len = tmp_buf[2];
                        printf("sco_len = %d \n",sco_len);
                    }else{
                        printf("hci len st error \n");
                        ret = -ESTRPIPE;
                        goto fail;
                    }
                    len_header = len_header+offset;
					printf("len_type %d,len_header %d\n",len_type,len_header);
                    lbh_memcpy(buffer+len_type,tmp_buf,len_header);
                    state = HCI_DATA_ST;
                    offset = 0;
                }
                break;
            case HCI_DATA_ST:
                {
					printf("HCI_DATA_ST \r\n");
                    if(buffer[0] == HCI_EVT_PKT){
                        len_data = hal_uart_receive(port, tmp_buf, (evt_len-check_offset));
                        if (!len_data) {
                            printf("read  evt len_data error ,len_data =%d\n",len_data);
                            ret = -ESTRPIPE;
                            goto fail;
                        }
						if (len_data+check_offset < evt_len) {
                            printf("len_data =%d,continue\n",len_data);
							lbh_memcpy(buffer+len_type+len_header+check_offset,tmp_buf,len_data);
							check_offset += len_data;
                            break;
						}
						
						printf("len_type %d,len_header %d,check_offset %d\n",len_type,len_header,check_offset);
                        lbh_memcpy(buffer+len_type+len_header+check_offset,tmp_buf,len_data);
                        if (lbh_get_print_en()){
                            print_data(tmp_buf, len_data);
                        }
						len_data = evt_len;
                    }else if(buffer[0] == HCI_ACL_PKT){
                        len_data = hal_uart_receive(port, tmp_buf, (acl_len-check_offset));
                        if (!len_data) {
                            printf("read  acl len_data error ,len_data =%d\n",len_data);
                            ret = -ESTRPIPE;
                            goto fail;
                        }
						if (len_data+check_offset < acl_len) {
                            printf("len_data =%d,continue\n",len_data);
							lbh_memcpy(buffer+len_type+len_header+check_offset,tmp_buf,len_data);
							check_offset += len_data;
                            break;
						}
                        lbh_memcpy(buffer+len_type+len_header+check_offset,tmp_buf,len_data);
						len_data = acl_len;
                    }else if(buffer[0] == HCI_SCO_PKT){
                        len_data = hal_uart_receive(port, tmp_buf, (sco_len-check_offset));
                        if (!len_data) {
                            printf("read  sco len_data error ,len_data =%d\n",len_data);
                            ret = -ESTRPIPE;
                            goto fail;
                        }
						if (len_data+check_offset < sco_len) {
                            printf("len_data =%d,continue\n",len_data);
							lbh_memcpy(buffer+len_type+len_header+check_offset,tmp_buf,len_data);
							check_offset += len_data;
                            break;
						}
                        lbh_memcpy(buffer+len_type+len_header+check_offset,tmp_buf,len_data);
						len_data = sco_len;
                    }else{
                        printf("hci data st error \n");
                        ret = -ESTRPIPE;
                        goto fail;
                    }

                    state = HCI_DONE_ST;
                    check_offset = 0;
                }
                break;
            case HCI_DONE_ST:
				printf("HCI_DONE_ST \r\n");
                read_done = true;
                ret_len = len_type+len_header+len_data;
				printf("len_type %d,len_header %d,len_data %d, ret_len %d\n",len_type,len_header,len_data,ret_len);
                break;
            default:
                break;
        }
    }
done:
    return ret_len;

fail:
    return ret;
}

extern void bt_stack_th_rxdata_notify(uint32 value);
void lbh_uart_recv_task(void *arg)
{
	int ret;
	lbh_uart_recv_task_running = 1;
	while(lbh_uart_recv_task_running){
		if(bt_driver_server_queue){
			int recv_len;
			recv_len = bt_hci_data_read_process(rbuf,BUFFSIZE);
			if(recv_len == 0){
				printf("lbh_uart_recv_task uart read continue");
				continue;
			}else if(recv_len<0){
				printf("lbh_uart_recv_task uart read error");
				break;
			}
			//print_data(rbuf, recv_len);
			ret = rtos_queue_write(bt_driver_server_queue, rbuf, 0, 0);
			if(ret != 0){
				printf("bt_driver_server_queue write error \n");
			}
			extern int server_or_client;

			if (!server_or_client) {
            	bt_stack_th_rxdata_notify(1);
			}
		}else{
			printf("lbh_uart_recv_task error return\n");
			break;
		}
	}
	printf("lbh_uart_recv_task exit\n");
	rtos_task_delete(rtos_get_current_task());
}

void lbh_uart_send_task(void *arg)
{
	int ret;
	lbh_uart_send_task_running = 1;
	while(lbh_uart_send_task_running){
		if(bt_server_driver_queue){
			ret = rtos_queue_read(bt_server_driver_queue, tbuf, -1, 0);
			if(ret){
				printf("lbh_uart_send_task read error \n");
			}else{
				uint32_t tx_len = (uint32_t)tbuf[0] | (((uint32_t)tbuf[1])<<8) | (((uint32_t)tbuf[2])<<16)| (((uint32_t)tbuf[3])<<24);
				printf("lbh_uart_send_task tx_len = %d\n",tx_len);
				if(tx_len){
					hal_uart_send(port, &tbuf[4], tx_len);
				}else{
					lbh_uart_close();
				}
			}
		}else{
			printf("lbh_uart_send_task error return\n");
			break;
		}
	}
	printf("lbh_uart_send_task exit\n");
	rtos_task_delete(rtos_get_current_task());
}

int lbh_uart_open(void)
{
    tmp_buf = rtos_malloc(BUFFSIZE+64);
	tbuf = rtos_malloc(BUFFSIZE+64);
	rbuf = rtos_malloc(BUFFSIZE+64);
	uint32_t baudrate;
	_uart_config_t uart_config;
	int i;
	int flowctrl, loopback;

	hal_log_info("UART in lbh mode");

	port = 1;
	baudrate = 115200;//1500000;
	flowctrl = 1;
	loopback = 0;

	if(0 == port){
		printf("uart0 can't test, please use other port!");
		return -1;
	}
	if(tbuf == NULL || rbuf == NULL || tmp_buf == NULL){
		printf("tx rx buff alloc failed!");
		return -1;
	}
	rtos_memset(tbuf, 0, BUFFSIZE * sizeof(uint8_t));
	rtos_memset(rbuf, 0, BUFFSIZE * sizeof(uint8_t));
	rtos_memset(tmp_buf, 0, BUFFSIZE * sizeof(uint8_t));


	switch (baudrate) {
	case 4800:
		uart_config.baudrate = UART_BAUDRATE_4800;
		break;

	case 9600:
		uart_config.baudrate = UART_BAUDRATE_9600;
		break;

	case 115200:
		uart_config.baudrate = UART_BAUDRATE_115200;
		break;

	case 1500000:
		uart_config.baudrate = UART_BAUDRATE_1500000;
		break;

	default:
		printf("Using default baudrate: 115200");
		uart_config.baudrate = UART_BAUDRATE_115200;
		break;
	}

	uart_config.word_length = UART_WORD_LENGTH_8;
	uart_config.stop_bit = UART_STOP_BIT_1;
	uart_config.parity = UART_PARITY_NONE;

	hal_uart_init(port);
	hal_uart_control(port, 0, &uart_config);
	printf("flow:%d, loopback:%d \n", flowctrl, loopback);
	if (flowctrl)
		hal_uart_set_hardware_flowcontrol(port);
	else
		hal_uart_disable_flowcontrol(port);

	if (loopback)
		hal_uart_set_loopback(port, 1);
	else
		hal_uart_set_loopback(port, 0);

	/* send */
	printf("uart init done\n");

	if(rtos_queue_create(BUFFSIZE, 20, &bt_driver_server_queue, "bt_driver_server_queue")){
		printf("bt_driver_server_queue create failed\n");
	}
	if(rtos_queue_create(BUFFSIZE, 10, &bt_server_driver_queue, "bt_server_driver_queue")){
		printf("bt_server_driver_queue create failed\n");
	}

	if(rtos_task_create(lbh_uart_send_task,"lbh_uart_send_task",BT_UART_TX_TASK,4096,NULL, AIC_PRIORITY_NORMAL,&lbh_uart_send_task_handle)){
		printf("lbh_uart_send_task create failed\n");
	}
	if(rtos_task_create(lbh_uart_recv_task,"lbh_uart_recv_task",BT_UART_RX_TASK,6144,NULL, AIC_PRIORITY_NORMAL,&lbh_uart_recv_task_handle)){
		printf("lbh_uart_recv_task create failed\n");
	}

	return 0;
}

void lbh_uart_close(void)
{
	hal_uart_deinit(port);
	rtos_free(tbuf);
	rtos_free(rbuf);
    rtos_free(tmp_buf);
	lbh_uart_send_task_running = 0;
	lbh_uart_recv_task_running = 0;
	rtos_queue_delete(bt_driver_server_queue);
	rtos_queue_delete(bt_server_driver_queue);
	
}
