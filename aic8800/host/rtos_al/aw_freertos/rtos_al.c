/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
 * All Rights Reserved
 *
 * FreeRTOS RTOS Abstraction Layer for AIC8800 WiFi driver
 */

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "timers.h"

#include "rtos_al.h"
#include <string.h>
#include "co_list.h"
#include "co_math.h"
#include "aic_log.h"

#define RTOS_AL_INFO_DUMP 0

#define AIC_TASK_NAME_MAX_LEN 64

typedef struct {
    QueueHandle_t handle;
    uint32_t elt_size;
    uint32_t nb_elt;
} OsQueue;

typedef struct {
    TimerHandle_t handle;
    rtos_timer_fct func;
    void *args;
    uint32_t ms;
    uint8_t autoReload;
} OsTimer;

rtos_tick_type rtos_ms_to_tick(unsigned long ms)
{
    return (ms * configTICK_RATE_HZ + 999) / 1000;
}

unsigned long rtos_tick_to_ms(unsigned long tick)
{
    return tick * (1000 / configTICK_RATE_HZ);
}

unsigned long rtos_now(bool isr)
{
    unsigned long tick = xTaskGetTickCount();
    return rtos_tick_to_ms(tick);
}

void rtos_msleep(uint32 time_in_ms)
{
    vTaskDelay(rtos_ms_to_tick(time_in_ms));
}

void rtos_udelay(unsigned int us)
{
    volatile unsigned int count;
    while (us--) {
        count = 50;
        while (count--);
    }
}

void *rtos_malloc(uint32_t size)
{
    return pvPortMalloc(size);
}

void *rtos_calloc(uint32_t nb_elt, uint32_t size)
{
    void *ptr = pvPortMalloc(nb_elt * size);
    if (ptr)
        memset(ptr, 0, nb_elt * size);
    return ptr;
}

void rtos_free(void *ptr)
{
    vPortFree(ptr);
}

void rtos_memcpy(void *pdest, const void *psrc, uint32_t size)
{
    memcpy(pdest, psrc, size);
}

void rtos_memset(void *pdest, uint8_t byte, uint32_t size)
{
    memset(pdest, byte, size);
}

void rtos_heap_info(int *total_size, int *free_size, int *min_free_size)
{
    if (total_size) *total_size = configTOTAL_HEAP_SIZE;
    if (free_size) *free_size = xPortGetFreeHeapSize();
    if (min_free_size) *min_free_size = xPortGetMinimumEverFreeHeapSize();
}

uint32_t rtos_entercritical(void)
{
    taskENTER_CRITICAL();
    return 0;
}

void rtos_exitcritical(void)
{
    taskEXIT_CRITICAL();
}

rtos_task_handle rtos_get_current_task(void)
{
    return (rtos_task_handle)xTaskGetCurrentTaskHandle();
}

int rtos_task_create(rtos_task_fct func,
                     const char * const name,
                     int task_id,
                     const uint16_t stack_depth,
                     void * const params,
                     rtos_prio prio,
                     rtos_task_handle * task_handle)
{
    BaseType_t ret;

    if (task_handle == NULL)
        return -1;

    ret = xTaskCreate(func, name, stack_depth / sizeof(StackType_t),
                      params, prio, (TaskHandle_t *)task_handle);

    if (ret != pdPASS)
        return -3;

    return 0;
}

void rtos_task_delete(rtos_task_handle task_handle)
{
    vTaskDelete((TaskHandle_t)task_handle);
}

void rtos_task_suspend(int duration)
{
    if (duration == -1)
        vTaskSuspend(NULL);
    else
        vTaskDelay(rtos_ms_to_tick(duration));
}

void rtos_task_resume(rtos_task_handle task_handle)
{
    vTaskResume((TaskHandle_t)task_handle);
}

int rtos_task_init_notification(rtos_task_handle task)
{
    return 0;
}

uint32_t rtos_task_wait_notification(int timeout)
{
    return 0;
}

void rtos_task_notify(rtos_task_handle task_handle, uint32_t value, bool isr)
{
}

void rtos_task_notify_setbits(rtos_task_handle task_handle, uint32_t value, bool isr)
{
}

uint32_t rtos_task_get_priority(rtos_task_handle task_handle)
{
    return uxTaskPriorityGet((TaskHandle_t)task_handle);
}

void rtos_task_set_priority(rtos_task_handle task_handle, uint32_t priority)
{
    vTaskPrioritySet((TaskHandle_t)task_handle, (UBaseType_t)priority);
}

int rtos_queue_create(int elt_size, int nb_elt, rtos_queue *queue, const char * const name)
{
    OsQueue *pqueue;

    if (queue == NULL)
        return -1;

    pqueue = rtos_malloc(sizeof(OsQueue));
    if (pqueue == NULL)
        return -2;

    pqueue->elt_size = elt_size;
    pqueue->nb_elt = nb_elt;
    pqueue->handle = xQueueCreate(nb_elt, elt_size);
    if (pqueue->handle == NULL) {
        AIC_LOG_PRINTF("create queue %s error\n", name);
        rtos_free(pqueue);
        return -1;
    }

    *queue = (rtos_queue)pqueue;
    return 0;
}

void rtos_queue_delete(rtos_queue queue)
{
    OsQueue *pqueue = (OsQueue *)queue;
    if (pqueue) {
        vQueueDelete(pqueue->handle);
        rtos_free(pqueue);
    }
}

bool rtos_queue_is_empty(rtos_queue queue)
{
    OsQueue *pqueue = (OsQueue *)queue;
    return (uxQueueMessagesWaiting(pqueue->handle) == 0);
}

bool rtos_queue_is_full(rtos_queue queue)
{
    OsQueue *pqueue = (OsQueue *)queue;
    return (uxQueueSpacesAvailable(pqueue->handle) == 0);
}

int rtos_queue_cnt(rtos_queue queue)
{
    OsQueue *pqueue = (OsQueue *)queue;
    return (int)uxQueueMessagesWaiting(pqueue->handle);
}

int rtos_queue_write(rtos_queue queue, void *msg, int timeout, bool isr)
{
    OsQueue *pqueue = (OsQueue *)queue;
    BaseType_t ret;

    if (pqueue == NULL)
        return -1;

    if (isr) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        ret = xQueueSendFromISR(pqueue->handle, msg, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken)
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    } else {
        TickType_t ticks = (timeout == -1) ? portMAX_DELAY : rtos_ms_to_tick(timeout);
        ret = xQueueSend(pqueue->handle, msg, ticks);
    }

    return (ret == pdPASS) ? 0 : -2;
}

int rtos_queue_read(rtos_queue queue, void *msg, int timeout, bool isr)
{
    OsQueue *pqueue = (OsQueue *)queue;
    BaseType_t ret;

    if (pqueue == NULL)
        return -1;

    if (isr) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        ret = xQueueReceiveFromISR(pqueue->handle, msg, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken)
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    } else {
        TickType_t ticks = (timeout == -1) ? portMAX_DELAY : rtos_ms_to_tick(timeout);
        ret = xQueueReceive(pqueue->handle, msg, ticks);
    }

    return (ret == pdPASS) ? 0 : -2;
}

int rtos_queue_peek(rtos_queue queue, void *msg, int timeout, bool isr)
{
    return -1;
}

int rtos_queue_reset(rtos_queue queue)
{
    OsQueue *pqueue = (OsQueue *)queue;
    if (pqueue == NULL)
        return -1;
    xQueueReset(pqueue->handle);
    return 0;
}

int rtos_semaphore_create(rtos_semaphore *semaphore, const char * const name, int max_count, int init_count)
{
    SemaphoreHandle_t sem;

    if (semaphore == NULL)
        return -1;

    if (max_count == 1)
        sem = xSemaphoreCreateBinary();
    else
        sem = xSemaphoreCreateCounting(max_count, init_count);

    if (sem == NULL)
        return -3;

    if (max_count == 1 && init_count == 0) {
        /* Binary semaphore created with xSemaphoreCreateBinary starts empty, which is correct */
    } else if (max_count == 1 && init_count == 1) {
        xSemaphoreGive(sem);
    }

    *semaphore = (rtos_semaphore)sem;
    return 0;
}

int rtos_semaphore_get_count(rtos_semaphore semaphore)
{
    return (int)uxSemaphoreGetCount((SemaphoreHandle_t)semaphore);
}

void rtos_semaphore_delete(rtos_semaphore semaphore)
{
    SemaphoreHandle_t sem = (SemaphoreHandle_t)semaphore;
    if (sem)
        vSemaphoreDelete(sem);
}

int rtos_semaphore_wait(rtos_semaphore semaphore, int timeout)
{
    SemaphoreHandle_t sem = (SemaphoreHandle_t)semaphore;
    BaseType_t ret;

    if (sem == NULL)
        return -1;

    TickType_t ticks = (timeout == -1) ? portMAX_DELAY : rtos_ms_to_tick(timeout);
    ret = xSemaphoreTake(sem, ticks);

    return (ret == pdPASS) ? 0 : -1;
}

int rtos_semaphore_signal(rtos_semaphore semaphore, bool isr)
{
    SemaphoreHandle_t sem = (SemaphoreHandle_t)semaphore;
    BaseType_t ret;

    if (sem == NULL)
        return -1;

    if (isr) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        ret = xSemaphoreGiveFromISR(sem, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken)
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    } else {
        ret = xSemaphoreGive(sem);
    }

    return (ret == pdPASS) ? 0 : -1;
}

static void rtos_timer_callback(TimerHandle_t xTimer)
{
    OsTimer *ptimer = (OsTimer *)pvTimerGetTimerID(xTimer);
    if (ptimer && ptimer->func)
        ptimer->func(ptimer->args);
}

uint32_t rtos_timer_create(const char * const name,
                           rtos_timer *timer,
                           const uint32_t ms,
                           const uint8_t autoReload,
                           void * const args,
                           rtos_timer_fct func)
{
    OsTimer *ptimer;
    UBaseType_t flag;

    if (timer == NULL)
        return -1;

    ptimer = (OsTimer *)rtos_malloc(sizeof(OsTimer));
    if (ptimer == NULL)
        return -2;
    memset(ptimer, 0, sizeof(OsTimer));

    ptimer->ms = ms;
    ptimer->autoReload = autoReload;
    ptimer->func = func;
    ptimer->args = args;

    flag = autoReload ? pdTRUE : pdFALSE;
    ptimer->handle = xTimerCreate(name, rtos_ms_to_tick(ms), flag,
                                  (void *)ptimer, rtos_timer_callback);

    if (ptimer->handle == NULL) {
        rtos_free(ptimer);
        return -3;
    }

    *timer = (rtos_timer)ptimer;
    return 0;
}

int rtos_timer_start(rtos_timer timer, uint32_t ms, bool isr)
{
    OsTimer *ptimer = (OsTimer *)timer;
    BaseType_t ret;

    if (ptimer == NULL)
        return -1;

    if (isr) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        ret = xTimerStartFromISR(ptimer->handle, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken)
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    } else {
        ret = xTimerStart(ptimer->handle, portMAX_DELAY);
    }

    return (ret == pdPASS) ? 0 : -1;
}

int rtos_timer_stop(rtos_timer timer, uint32_t wait_ms)
{
    OsTimer *ptimer = (OsTimer *)timer;
    if (ptimer == NULL)
        return -1;
    return (xTimerStop(ptimer->handle, portMAX_DELAY) == pdPASS) ? 0 : -1;
}

int rtos_timer_stop_isr(rtos_timer timer)
{
    OsTimer *ptimer = (OsTimer *)timer;
    if (ptimer == NULL)
        return -1;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t ret = xTimerStopFromISR(ptimer->handle, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    return (ret == pdPASS) ? 0 : -1;
}

int rtos_timer_delete(rtos_timer timer, uint32_t wait_ms)
{
    OsTimer *ptimer = (OsTimer *)timer;
    if (ptimer == NULL)
        return -1;
    xTimerDelete(ptimer->handle, portMAX_DELAY);
    rtos_free(ptimer);
    return 0;
}

int rtos_timer_is_active(rtos_timer timer)
{
    OsTimer *ptimer = (OsTimer *)timer;
    if (ptimer == NULL)
        return 0;
    return (xTimerIsTimerActive(ptimer->handle) != pdFALSE) ? 1 : 0;
}

int rtos_mutex_recursive_create(rtos_mutex *mutex)
{
    SemaphoreHandle_t sem;

    if (mutex == NULL)
        return -1;

    sem = xSemaphoreCreateRecursiveMutex();
    if (sem == NULL)
        return -3;

    *mutex = (rtos_mutex)sem;
    return 0;
}

int rtos_mutex_recursive_lock(rtos_mutex mutex)
{
    SemaphoreHandle_t sem = (SemaphoreHandle_t)mutex;
    return (xSemaphoreTakeRecursive(sem, portMAX_DELAY) == pdPASS) ? 0 : -1;
}

int rtos_mutex_recursive_unlock(rtos_mutex mutex)
{
    SemaphoreHandle_t sem = (SemaphoreHandle_t)mutex;
    return (xSemaphoreGiveRecursive(sem) == pdPASS) ? 0 : -1;
}

int rtos_mutex_create(rtos_mutex *mutex, const char * const name)
{
    SemaphoreHandle_t sem;

    if (mutex == NULL)
        return -1;

    sem = xSemaphoreCreateMutex();
    if (sem == NULL)
        return -3;

    *mutex = (rtos_mutex)sem;
    return 0;
}

void rtos_mutex_delete(rtos_mutex mutex)
{
    SemaphoreHandle_t sem = (SemaphoreHandle_t)mutex;
    if (sem)
        vSemaphoreDelete(sem);
}

int rtos_mutex_lock(rtos_mutex mutex, int timeout)
{
    SemaphoreHandle_t sem = (SemaphoreHandle_t)mutex;
    TickType_t ticks = (timeout == -1) ? portMAX_DELAY : rtos_ms_to_tick(timeout);
    return (xSemaphoreTake(sem, ticks) == pdPASS) ? 0 : -1;
}

int rtos_mutex_unlock(rtos_mutex mutex)
{
    SemaphoreHandle_t sem = (SemaphoreHandle_t)mutex;
    return (xSemaphoreGive(sem) == pdPASS) ? 0 : -1;
}

int rtos_event_group_create(rtos_event_group *event_group)
{
    return -1;
}

void rtos_event_group_delete(rtos_event_group event_group)
{
}

uint32_t rtos_event_group_get_bits(rtos_event_group event_group, bool isr)
{
    return 0;
}

uint32_t rtos_event_group_wait_bits(rtos_event_group event_group, const uint32_t val,
                                    const bool clear_on_exit, const bool wait_all_bits, int timeout)
{
    return 0;
}

uint32_t rtos_event_group_clear_bits(rtos_event_group event_group, const uint32_t val, bool isr)
{
    return 0;
}

uint32_t rtos_event_group_set_bits(rtos_event_group event_group, const uint32_t val, bool isr)
{
    return 0;
}

uint32_t rtos_protect(void)
{
    taskENTER_CRITICAL();
    return 1;
}

void rtos_unprotect(uint32_t protect)
{
    (void)protect;
    taskEXIT_CRITICAL();
}

void rtos_start_scheduler(void)
{
    vTaskStartScheduler();
}

int rtos_init(void)
{
    return 0;
}

void rtos_priority_set(rtos_task_handle handle, rtos_prio priority)
{
    vTaskPrioritySet((TaskHandle_t)handle, (UBaseType_t)priority);
}

rtos_task_handle rtos_get_task_handle(void)
{
    return rtos_get_current_task();
}

rtos_sched_state rtos_get_scheduler_state(void)
{
    return 0;
}

int aic_time_get(enum time_origin_t origin, uint32_t *sec, uint32_t *usec)
{
    if (sec) *sec = xTaskGetTickCount() / configTICK_RATE_HZ;
    if (usec) *usec = 0;
    return 0;
}
