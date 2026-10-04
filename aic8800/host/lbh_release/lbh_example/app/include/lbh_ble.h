#ifndef _LBH_BLE_H_
#define _LBH_BLE_H_

/**
 * @brief     Prerequisite of terminating LBH
 * 
 * @attention Must be invoked from thread other than LBH Client thread
 * 
*/
//void lbh_terminate();
void client_queue_msg_callback(uint8_t *data, uint16_t length);

#endif//_LBH_BLE_H_