#ifndef _XRADIO_SYS_LIST_H_
#define _XRADIO_SYS_LIST_H_

/*
 * On FreeRTOS/Tina RT, struct list_head and list functions are provided
 * by the SDMMC HAL's sys/list.h (which is already in the include path
 * via -Idrivers/rtos-hal/include/hal/sdmmc/sys/). This header serves
 * as a bridge to include that implementation.
 */
#include <sys/list.h>

#endif /* _XRADIO_SYS_LIST_H_ */