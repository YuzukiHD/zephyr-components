/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
 * All Rights Reserved
 *
 * RTOS abstraction layer for the AIC8800 WiFi driver, on Zephyr kernel objects
 */

#ifndef RTOS_AL_H_
#define RTOS_AL_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "co_int.h"
#include "co_bool.h"
#include "compiler.h"

enum aic_task_id {
    CONTROL_TASK    = 1,
    SUPPLICANT_TASK = 2,
    SDIO_DATRX_TASK = 3,
    FHOST_TX_TASK   = 4,
    FHOST_RX_TASK   = 5,
    CLI_CMD_TASK    = 6,
    RWNX_TIMER_TASK = 7,
    USB_RX_TASK     = 8,
    USB_TX_TASK     = 9,
    RWNX_STA_MGMT_TASK = 10,
    BT_TASK_MIN     = 11,
    BT_TASK_1       = 12,
    BT_TASK_2       = 13,
    BT_UART_RX_TASK = 14,
    BT_UART_TX_TASK = 15,
    BT_TEST_TASK    = 16,
    BT_TASK_MAX     = 17,
    WPA_DRIVER_TASK = 18,
};

enum time_origin_t {
    SINCE_BOOT,
    SINCE_EPOCH,
};

typedef uint32_t rtos_tick_type;
typedef void * rtos_task_handle;
typedef int rtos_prio;
typedef void (*rtos_task_fct)(void *);
typedef void * rtos_queue;
typedef void * rtos_semaphore;
typedef void * rtos_mutex;
typedef int rtos_event_group;
typedef int rtos_sched_state;
typedef void * rtos_timer;
typedef void (*rtos_timer_fct)(void *);
typedef int rtos_timer_status;

#ifndef CONFIG_LWIP
typedef int err_t;
#endif

#define RTOS_TASK_FCT(name)
#define RTOS_TASK_PRIORITY(prio)
#define RTOS_TASK_NULL             NULL

#define AIC_PRIORITY_NORMAL 10
#define AIC_PRIORITY_LOW    10
#define AIC_PRIORITY_ABOVE  10

#define TX_SUCCESS                      ((uint32_t) 0x00)
#define TX_DELETED                      ((uint32_t) 0x01)
#define TX_POOL_ERROR                   ((uint32_t) 0x02)
#define TX_PTR_ERROR                    ((uint32_t) 0x03)
#define TX_WAIT_ERROR                   ((uint32_t) 0x04)
#define TX_SIZE_ERROR                   ((uint32_t) 0x05)
#define TX_GROUP_ERROR                  ((uint32_t) 0x06)
#define TX_NO_EVENTS                    ((uint32_t) 0x07)
#define TX_OPTION_ERROR                 ((uint32_t) 0x08)
#define TX_QUEUE_ERROR                  ((uint32_t) 0x09)
#define TX_QUEUE_EMPTY                  ((uint32_t) 0x0A)
#define TX_QUEUE_FULL                   ((uint32_t) 0x0B)
#define TX_SEMAPHORE_ERROR              ((uint32_t) 0x0C)
#define TX_NO_INSTANCE                  ((uint32_t) 0x0D)
#define TX_THREAD_ERROR                 ((uint32_t) 0x0E)
#define TX_PRIORITY_ERROR               ((uint32_t) 0x0F)
#define TX_NO_MEMORY                    ((uint32_t) 0x10)
#define TX_START_ERROR                  ((uint32_t) 0x10)
#define TX_DELETE_ERROR                 ((uint32_t) 0x11)
#define TX_RESUME_ERROR                 ((uint32_t) 0x12)
#define TX_CALLER_ERROR                 ((uint32_t) 0x13)
#define TX_SUSPEND_ERROR                ((uint32_t) 0x14)
#define TX_TIMER_ERROR                  ((uint32_t) 0x15)
#define TX_TICK_ERROR                   ((uint32_t) 0x16)
#define TX_ACTIVATE_ERROR               ((uint32_t) 0x17)
#define TX_THRESH_ERROR                 ((uint32_t) 0x18)
#define TX_SUSPEND_LIFTED               ((uint32_t) 0x19)
#define TX_WAIT_ABORTED                 ((uint32_t) 0x1A)
#define TX_WAIT_ABORT_ERROR             ((uint32_t) 0x1B)
#define TX_MUTEX_ERROR                  ((uint32_t) 0x1C)
#define TX_NOT_AVAILABLE                ((uint32_t) 0x1D)
#define TX_NOT_OWNED                    ((uint32_t) 0x1E)
#define TX_INHERIT_ERROR                ((uint32_t) 0x1F)
#define TX_NOT_DONE                     ((uint32_t) 0x20)
#define TX_CEILING_EXCEEDED             ((uint32_t) 0x21)
#define TX_INVALID_CEILING              ((uint32_t) 0x22)
#define TX_FEATURE_NOT_ENABLED          ((uint32_t) 0xFF)

unsigned long rtos_now(bool isr);
void rtos_msleep(uint32_t time_in_ms);
void rtos_udelay(unsigned int us);

void *rtos_malloc(uint32_t size);
void *rtos_calloc(uint32_t nb_elt, uint32_t size);
void rtos_free(void *ptr);
void rtos_memcpy(void *pdest, const void *psrc, uint32_t size);
void rtos_memset(void *pdest, uint8_t byte, uint32_t size);
void rtos_heap_info(int *total_size, int *free_size, int *min_free_size);

uint32_t rtos_entercritical(void);
void rtos_exitcritical(void);

rtos_task_handle rtos_get_current_task(void);

int rtos_task_create(rtos_task_fct func,
                     const char * const name,
                     int task_id,
                     const uint16_t stack_depth,
                     void * const params,
                     rtos_prio prio,
                     rtos_task_handle * const task_handle);
void rtos_task_delete(rtos_task_handle task_handle);
void rtos_task_suspend(int duration);
void rtos_task_resume(rtos_task_handle task_handle);

int rtos_task_init_notification(rtos_task_handle task);
uint32_t rtos_task_wait_notification(int timeout);
void rtos_task_notify(rtos_task_handle task_handle, uint32_t value, bool isr);
void rtos_task_notify_setbits(rtos_task_handle task_handle, uint32_t value, bool isr);
uint32_t rtos_task_get_priority(rtos_task_handle task_handle);
void rtos_task_set_priority(rtos_task_handle task_handle, uint32_t priority);

int rtos_queue_create(int elt_size, int nb_elt, rtos_queue *queue, const char * const name);
void rtos_queue_delete(rtos_queue queue);
bool rtos_queue_is_empty(rtos_queue queue);
bool rtos_queue_is_full(rtos_queue queue);
int rtos_queue_cnt(rtos_queue queue);
int rtos_queue_write(rtos_queue queue, void *msg, int timeout, bool isr);
int rtos_queue_read(rtos_queue queue, void *msg, int timeout, bool isr);
int rtos_queue_peek(rtos_queue queue, void *msg, int timeout, bool isr);
int rtos_queue_reset(rtos_queue queue);

int rtos_semaphore_create(rtos_semaphore *semaphore, const char * const name, int max_count, int init_count);
int rtos_semaphore_get_count(rtos_semaphore semaphore);
void rtos_semaphore_delete(rtos_semaphore semaphore);
int rtos_semaphore_wait(rtos_semaphore semaphore, int timeout);
int rtos_semaphore_signal(rtos_semaphore semaphore, bool isr);

uint32_t rtos_timer_create(const char * const name,
                           rtos_timer *timer,
                           const uint32_t ms,
                           const uint8_t autoReload,
                           void * const args,
                           rtos_timer_fct func);
int rtos_timer_start(rtos_timer timer, uint32_t ms, bool isr);
int rtos_timer_stop(rtos_timer timer, uint32_t wait_ms);
int rtos_timer_stop_isr(rtos_timer timer);
int rtos_timer_delete(rtos_timer timer, uint32_t wait_ms);
int rtos_timer_is_active(rtos_timer timer);

int rtos_mutex_recursive_create(rtos_mutex *mutex);
int rtos_mutex_recursive_lock(rtos_mutex mutex);
int rtos_mutex_recursive_unlock(rtos_mutex mutex);
int rtos_mutex_create(rtos_mutex *mutex, const char * const name);
void rtos_mutex_delete(rtos_mutex mutex);
int rtos_mutex_lock(rtos_mutex mutex, int timeout);
int rtos_mutex_unlock(rtos_mutex mutex);

int rtos_event_group_create(rtos_event_group *event_group);
void rtos_event_group_delete(rtos_event_group event_group);
uint32_t rtos_event_group_get_bits(rtos_event_group event_group, bool isr);
uint32_t rtos_event_group_wait_bits(rtos_event_group event_group, const uint32_t val,
                                    const bool clear_on_exit, const bool wait_all_bits, int timeout);
uint32_t rtos_event_group_clear_bits(rtos_event_group event_group, const uint32_t val, bool isr);
uint32_t rtos_event_group_set_bits(rtos_event_group event_group, const uint32_t val, bool isr);

uint32_t rtos_protect(void);
void rtos_unprotect(uint32_t protect);
void rtos_start_scheduler(void);
int rtos_init(void);
void rtos_priority_set(rtos_task_handle handle, rtos_prio priority);
rtos_task_handle rtos_get_task_handle(void);
rtos_sched_state rtos_get_scheduler_state(void);

int aic_time_get(enum time_origin_t origin, uint32_t *sec, uint32_t *usec);

#define RTOS_SEM_DEL_A(sem) \
{\
    if(sem != NULL)\
    {\
        rtos_semaphore_delete(sem);\
        sem = NULL;\
    }\
}

#define RTOS_MUTEX_DEL_A(mux) \
{\
    if(mux != NULL)\
    {\
        rtos_mutex_delete(mux);\
        mux = NULL;\
    }\
}

#define RTOS_TASK_DEL_A(task) \
{\
    if(task != NULL)\
    {\
        rtos_task_delete(task);\
        task = NULL;\
    }\
}

#define RTOS_Q_DEL_A(queue) \
{\
    if(queue != NULL)\
    {\
        rtos_queue_delete(queue);\
        queue = NULL;\
    }\
}

#endif // RTOS_AL_H_