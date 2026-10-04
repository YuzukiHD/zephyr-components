#ifndef _BT_TYPES_DEF_H_
#define _BT_TYPES_DEF_H_
#include <string.h>
#include "co_types_def.h"
#include "co_errors.h"


#ifndef bool
#define bool BOOL
#endif

/*---------------------------------------------------------------------------
 * Status_BTDef type
 */
typedef Co_Error Status_BTDef;

#define BTDEF_NO_ERROR           CO_ERROR_ERROR_NONE
#define BTDEF_ERROR              CO_ERROR_ERROR
#define BTDEF_PENDING            CO_ERROR_PENDING
#define BTDEF_DISCONNECT         CO_ERROR_DISCONNECT
#define BTDEF_NOCON              CO_ERROR_NOCON
#define BTDEF_IS_USEING          CO_ERROR_IS_USEING
#define BTDEF_PLAY_NOT_DONE      CO_ERROR_PLAY_NOT_DONE
#define BTDEF_PLAY_DONE          CO_ERROR_PLAY_DONE
#define BTDEF_NO_PROGRESS        CO_ERROR_NO_PROGRESS
#define BTDEF_IS_CON             CO_ERROR_IS_CON
#define BTDEF_SEND_OUT           CO_ERROR_SEND_OUT
#define BTDEF_NOT_DONE           CO_ERROR_NOT_DONE
#define BTDEF_NO_RESOURCES       CO_ERROR_NO_RESOURCES
#define BTDEF_NOT_FOUND          CO_ERROR_NOT_FOUND
#define BTDEF_DEVICE_NOT_FOUND   CO_ERROR_DEVICE_NOT_FOUND
#define BTDEF_CON_ERROR          CO_ERROR_CON_ERROR
#define BTDEF_TIMER_TIMEOUT      CO_ERROR_TIMER_TIMEOUT
#define BTDEF_NO_CONN            CO_ERROR_NO_CONN
#define BTDEF_INVALID_PARAM      CO_ERROR_INVALID_PARAM
#define BTDEF_IS_GOING_ON        CO_ERROR_IS_GOING_ON
#define BTDEF_IS_LIMITED         CO_ERROR_IS_LIMITED
#define BTDEF_INVALID_TYPE       CO_ERROR_INVALID_TYPE
#define BTDEF_HCI_NOT_OPEN       CO_ERROR_HCI_NOT_OPEN
#define BTDEF_NOT_SUPPORTED      CO_ERROR_NOT_SUPPORTED
#define BTDEF_CONTINUE           CO_ERROR_CONTINUE
#define BTDEF_CANCELLED          CO_ERROR_CANCELLED
#define BTDEF_UNDEFINED          CO_ERROR_UNDEFINED
/* End of Status_BTDef */


#define BD_ADDR_SIZE    6

typedef struct _BT_ADDR {
    uint8_t    addr[BD_ADDR_SIZE];
} BT_ADDR;

#ifndef LISTS_DEFINED
#define LISTS_DEFINED

typedef struct  _ListEntry
{
    struct _ListEntry *Flink;
    struct _ListEntry *Blink;

} ListEntry;
#endif

#endif /* _BT_TYPES_DEF_H_ */
