/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
 *
 * All Rights Reserved
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include <rtthread.h>
#include <hal_timer.h>

#include "rtos_al.h"
#include <string.h>
//#include "porting.h"
#include "co_list.h"
#include "co_math.h"
#include <sys/time.h>
//#include <sys/param.h>
#include "aic_log.h"
#include "os_timer.h"

#define RTOS_AL_INFO_DUMP 0

#define AIC_TASK_NAME_MAX_LEN 64

#define QUEUE_COUNT_USE_SEMAPHORE   0
#define QUEUE_EXTRA_ELEMENT_COUNT   4
#define QUEUE_TRACE                 0

typedef struct {
	rt_mq_t mq;
	uint32_t elt_size;
    uint32_t nb_elt;
} OsQueue;

typedef struct {
    rt_timer_t handle;
    rtos_timer_fct func;
    void *args;
    uint32_t ms;
    uint8_t autoReload;
} OsTimer;

/*
 * FUNCTIONS
 ****************************************************************************************
 */
/**
 ****************************************************************************************
 * @brief Convert ms to ticks
 *
 * @param[in] timeout_ms Timeout value in ms (use -1 for no timeout).
 * @return number of ticks for the specified timeout value.
 *
 ****************************************************************************************
 */
#if 1
//__INLINE rtos_tick_type rtos_timeout_2_tickcount(int timeout_ms)
//{
//    //1Tick == 1ms
//    return timeout_ms;
//}

rt_tick_t rtos_ms_to_tick(unsigned long ms)
{
	rt_tick_t tick;

	tick = RT_TICK_PER_SECOND * (ms / 1000);
    tick += (RT_TICK_PER_SECOND * (ms % 1000) + 999) / 1000;
	return tick;
}

__INLINE unsigned long rtos_tick_to_ms(unsigned long tick)
{
	return tick	* (1000 / RT_TICK_PER_SECOND);
}

unsigned long rtos_now(bool isr)
{
    unsigned long tick = rt_tick_get();

    return rtos_tick_to_ms(tick);
}

void rtos_msleep(uint32 time_in_ms)
{
	rt_thread_delay(rtos_ms_to_tick(time_in_ms));
}

void rtos_udelay(unsigned int us)
{
	hal_udelay(us);
}

void *rtos_malloc(uint32_t size)
{
    return rt_malloc(size);
}

void *rtos_calloc(uint32_t nb_elt, uint32_t size)
{
    return rt_calloc(nb_elt, size);
}

void rtos_free(void *ptr)
{
    rt_free(ptr);
}

void rtos_memcpy(void *pdest, const void *psrc, uint32_t size)
{
    memcpy(pdest, psrc, size);
}

void rtos_memset(void *pdest, uint8_t byte, uint32_t size)
{
    memset(pdest, byte, size);
}

//void rtos_heap_info(int *total_size, int *free_size, int *min_free_size)
//{
//
//}

uint32_t rtos_entercritical()
{
    rt_enter_critical();

    return 0;
}

void rtos_exitcritical()
{
    rt_exit_critical();
}

rtos_task_handle rtos_get_current_task(void)
{
    return (rtos_task_handle) rt_thread_self();
}

int rtos_task_create(rtos_task_fct func,
                     const char * const name,
                     int task_id,
                     const uint16_t stack_depth,
                     void * const params,
                     rtos_prio prio,
                     rtos_task_handle * task_handle)
{
    rt_thread_t handle;

    if (task_handle == NULL) {
        return -1;
    }

    handle = rt_thread_create((char *)name,
                      func,
                      params,
					  stack_depth,
                      prio,
                      10);

#if RTOS_AL_INFO_DUMP
    AIC_LOG_PRINTF("%s '%s', handle = %p, stack_size = %#x\n", __func__, name, handle, stack_depth);
#endif

	if (handle == RT_NULL) {
		return -3;
	}

    *task_handle = (rtos_task_handle)handle;

    rt_thread_startup(handle);

    return 0;
}

void rtos_task_delete(rtos_task_handle task_handle)
{
    rt_thread_t handle = (rt_thread_t)task_handle;

#if RTOS_AL_INFO_DUMP
    AIC_LOG_PRINTF("%s, handle = %p\n", __func__, handle);
#endif
	if (task_handle == rt_thread_self() || task_handle == NULL)
        rt_thread_exit();
    else {
	    rt_thread_delete(handle);
	}

}

void rtos_task_suspend(int duration)
{
    rt_thread_t handle = rt_thread_self();

    if (-1 == duration) {
        rt_thread_suspend(handle);
    } else {
		rt_thread_delay(rtos_ms_to_tick(duration));
    }
}

void rtos_task_resume(rtos_task_handle task_handle)
{
    rt_thread_t handle = (rt_thread_t)task_handle;

	rt_thread_resume(handle);
}


//int rtos_task_init_notification(rtos_task_handle task)
//{
//    return 0;
//}
//
//uint32_t rtos_task_wait_notification(int timeout)
//{
//    return 0;
//}
//
//void rtos_task_notify(rtos_task_handle task_handle, uint32_t value, bool isr)
//{
//
//}
//
//void rtos_task_notify_setbits(rtos_task_handle task_handle, uint32_t value, bool isr)
//{
//
//}

uint32_t rtos_task_get_priority(rtos_task_handle task_handle)
{
    rt_thread_t handle = (rt_thread_t)task_handle;

    return handle->current_priority;
}

void rtos_task_set_priority(rtos_task_handle task_handle, uint32_t priority)
{
    rt_thread_t handle = (rt_thread_t)task_handle;
    rt_uint8_t rt_priority = (rt_uint8_t)priority;

    rt_thread_control(handle, RT_THREAD_CTRL_CHANGE_PRIORITY, &rt_priority);
}

int rtos_queue_create(int elt_size, int nb_elt, rtos_queue *queue, const char * const name)
{
    OsQueue *pqueue = NULL;

    if (queue == NULL) {
        return -1;
    }

    pqueue = rtos_malloc(sizeof(OsQueue));
    if (pqueue == NULL) {
        return -2;
    }

    pqueue->elt_size = elt_size;
    pqueue->nb_elt = nb_elt;
    pqueue->mq = rt_mq_create(name, elt_size, nb_elt, RT_IPC_FLAG_FIFO);
    if (pqueue->mq == RT_NULL)
    {
        AIC_LOG_PRINTF("create queue %s error\n", name);
        rtos_free(pqueue);
        return -1;
    }

#if RTOS_AL_INFO_DUMP
    AIC_LOG_PRINTF("%s '%s', inner_handle = %p, handle = %p, elt_size = %#x, nb_elt = %#x, inner msg_size = %#x\n",
                    __func__, name, pqueue->mq, pqueue, elt_size, nb_elt, pqueue->mq->msg_size);
#endif

	*queue = (rtos_queue)pqueue;

    return 0;
}

void rtos_queue_delete(rtos_queue queue)
{
    #if QUEUE_TRACE
    aic_dbg("qdel:%p\n", queue);
    #endif
    OsQueue *pqueue = (OsQueue *)queue;
    if (pqueue) {
#if RTOS_AL_INFO_DUMP
		AIC_LOG_PRINTF("%s, inner_handle = %p, handle = %p\n", __func__, pqueue->mq, pqueue);
#endif
        rt_mq_delete(pqueue->mq);
		rtos_free(pqueue);
    }
}

bool rtos_queue_is_empty(rtos_queue queue)
{
    OsQueue *pqueue = (OsQueue *)queue;
    return (pqueue->mq->msg_queue_head == RT_NULL);
}

bool rtos_queue_is_full(rtos_queue queue)
{
    OsQueue *pqueue = (OsQueue *)queue;
    return (pqueue->mq->msg_queue_free == RT_NULL);
}

int rtos_queue_cnt(rtos_queue queue)
{
    OsQueue *pqueue = (OsQueue *)queue;
    return pqueue->mq->entry;
}

int rtos_queue_write(rtos_queue queue, void *msg, int timeout, bool isr)
{
    #if QUEUE_TRACE
    aic_dbg("wqin:%p/%d\n", queue, timeout);
    #endif
    rt_err_t status;
	int ret = 0;
    OsQueue *pqueue = (OsQueue *)queue;

    if (pqueue == NULL) {
       ret = -1;
	   goto exit;
    }
    status = rt_mq_send_wait(pqueue->mq, msg, pqueue->elt_size, rt_tick_from_millisecond(timeout));
	if (status != RT_EOK)
	{
		ret = -2;
	    goto exit;
	}

exit:
    #if QUEUE_TRACE
    aic_dbg("wqout:%p/%d\n", queue, ret);
    #endif
    return ret;
}

int rtos_queue_read(rtos_queue queue, void *msg, int timeout, bool isr)
{
    #if QUEUE_TRACE
    aic_dbg("rqin:%p/%d\n", queue, timeout);
    #endif
    rt_err_t status;
	int ret = 0;
    OsQueue *pqueue = (OsQueue *)queue;

    if (pqueue == NULL) {
        ret = -1;
        goto exit;
    }

	status = rt_mq_recv(pqueue->mq, msg, pqueue->elt_size, rt_tick_from_millisecond(timeout));
	if (status != RT_EOK)
	{
		ret = -2;
	    goto exit;
	}

exit:
    #if QUEUE_TRACE
    aic_dbg("rqout:%p/%d\n", queue, ret);
    #endif
    return ret;
}

//int rtos_queue_peek(rtos_queue queue, void *msg, int timeout, bool isr)
//{
//    return 0;
//}
//
//int rtos_queue_reset(rtos_queue queue)
//{
//    return 0;
//}
//
//int rtos_signal_send(rtos_task_handle dest_MsgQ, void *MsgItem, int msgLen, bool isr)
//{
//    return 0;
//}
//
//int rtos_signal_recv(rtos_task_handle MsgQ, void *MsgItem, int msgLen, bool isr)
//{
//    return 0;
//}

int rtos_semaphore_create(rtos_semaphore *semaphore, const char * const name, int max_count, int init_count)
{
    rt_sem_t psem;

    if (semaphore == NULL) {
        return -1;
    }

	psem = rt_sem_create(name, init_count, RT_IPC_FLAG_FIFO);

#if RTOS_AL_INFO_DUMP
	//AIC_LOG_PRINTF("%s sema = %p, name = %s\n", __func__, psem, name);
#endif

	if (psem == NULL) {
		return -3;
	}

    *semaphore = (rtos_semaphore)psem;

    return 0;
}

void rtos_semaphore_delete(rtos_semaphore semaphore)
{
    rt_sem_t psem = (rt_sem_t)semaphore;
    if (psem) {
        rt_sem_delete(psem);
    }
#if RTOS_AL_INFO_DUMP
	//AIC_LOG_PRINTF("%s sema = %p, name = %s\n", __func__, psem);
#endif
}

int rtos_semaphore_get_count(rtos_semaphore semaphore)
{
    rt_sem_t psem = (rt_sem_t)semaphore;
    uint16_t count = 0;

	rt_sem_control(psem, RT_IPC_CMD_GET_STATE, &count);

    return (int)count;
}

int rtos_semaphore_wait(rtos_semaphore semaphore, int timeout)
{
    rt_sem_t psem = (rt_sem_t)semaphore;
    rt_err_t status;

    if (psem == NULL) {
        return -1;
    }

	status = rt_sem_take(psem, rt_tick_from_millisecond(timeout));

    return status;
}

int rtos_semaphore_signal(rtos_semaphore semaphore, bool isr)
{
    rt_sem_t psem = (rt_sem_t)semaphore;

    if (psem == NULL) {
        return -1;
    }

    return rt_sem_release(psem);
}

uint32_t rtos_timer_create(const char * const name,
                           rtos_timer *timer,
                           const uint32_t ms,
                           const uint8_t autoReload,
                           void * const args,
                           rtos_timer_fct func )
{
    OsTimer *ptimer = NULL;
	rt_uint8_t flag;

    if (timer == NULL) {
        return -1;
    }

	flag = RT_TIMER_FLAG_SOFT_TIMER;
	if (autoReload) {
		flag |= RT_TIMER_FLAG_PERIODIC;
	} else {
		flag |= RT_TIMER_FLAG_ONE_SHOT;
	}

    ptimer = (OsTimer *)rtos_malloc(sizeof(OsTimer));
    if (ptimer == NULL) {
        return -2;
    }
    memset(ptimer, 0, sizeof(OsTimer));

    ptimer->ms = ms;
    ptimer->autoReload = autoReload;
    ptimer->func = func;
    ptimer->args = args;
    ptimer->handle = rt_timer_create(name,
									func,
									args,
									rt_tick_from_millisecond(ms),
									flag);
#if RTOS_AL_INFO_DUMP
	AIC_LOG_PRINTF("%s '%s', inner_handle = %p, handle = %p, period = %d, reload = %d\n",
					__func__, name, ptimer->handle, ptimer, ms, autoReload);
#endif

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

	rt_timer_start(ptimer->handle);

    return 0;
}

int rtos_timer_stop(rtos_timer timer, uint32_t wait_ms)
{
    OsTimer *ptimer = (OsTimer *)timer;

    return rt_timer_stop(ptimer->handle);
}

int rtos_timer_stop_isr(rtos_timer timer)
{
    OsTimer *ptimer = (OsTimer *)timer;

    rt_timer_stop(ptimer->handle);

    return 0;
}

int rtos_timer_delete(rtos_timer timer, uint32_t wait_ms)
{
    OsTimer *ptimer = (OsTimer *)timer;

    rt_timer_delete(ptimer->handle);
    rtos_free(ptimer);

    return 0;
}

int rtos_timer_is_active(rtos_timer timer)
{
    OsTimer *ptimer = (OsTimer *)timer;
    return OS_TimerIsActive(ptimer);
}
//int rtos_mutex_recursive_create(rtos_mutex *mutex)
//{
//    return -1;
//}
//
//int rtos_mutex_recursive_lock(rtos_mutex mutex)
//{
//    return -1;
//}
//
//int rtos_mutex_recursive_unlock(rtos_mutex mutex)
//{
//    return -1;
//}

int rtos_mutex_create(rtos_mutex *mutex, const char * const name)
{
	rt_mutex_t pmutex;

    if (mutex == NULL) {
        return -1;
    }

    pmutex = rt_mutex_create(name, RT_IPC_FLAG_PRIO);

#if RTOS_AL_INFO_DUMP
    AIC_LOG_PRINTF("%s mutex = %p, name = %s\n", __func__, pmutex, name);
#endif

	if (pmutex == NULL) {
		return -3;
	}

    *mutex = (rtos_mutex)pmutex;
    return 0;
}

void rtos_mutex_delete(rtos_mutex mutex)
{
    rt_mutex_t pmutex = (rt_mutex_t)mutex;
    rt_mutex_delete(pmutex);
}

int rtos_mutex_lock(rtos_mutex mutex, int timeout)
{
    rt_err_t status;
    rt_mutex_t pmutex = (rt_mutex_t)mutex;
    //aic_dbg("l: %p, locked:%d owner:%p\n", mutex, pmutex->locked, pmutex->owner);

    status = rt_mutex_take(pmutex, rt_tick_from_millisecond(timeout));
    if (status == RT_EOK) {
        return 0;
    }
    return -1;
}

int rtos_mutex_unlock(rtos_mutex mutex)
{
    rt_mutex_t pmutex = (rt_mutex_t)mutex;
    //aic_dbg("ul: %p\n",mutex);
    return rt_mutex_release(pmutex);
}

//int rtos_event_group_create(rtos_event_group *event_group)
//{
//    return -1;
//}
//
//void rtos_event_group_delete(rtos_event_group event_group)
//{
//}
//
//uint32_t rtos_event_group_get_bits(rtos_event_group event_group, bool isr)
//{
//    return 0;
//}
//
//uint32_t rtos_event_group_wait_bits(rtos_event_group event_group, const uint32_t val,
//                                    const bool clear_on_exit, const bool wait_all_bits, int timeout)
//{
//    return 0;
//}
//
//uint32_t rtos_event_group_clear_bits(rtos_event_group event_group, const uint32_t val, bool isr)
//{
//    return 0;
//}
//
//uint32_t rtos_event_group_set_bits(rtos_event_group event_group, const uint32_t val, bool isr)
//{
//    return 0;
//}
//
//uint32_t rtos_protect(void)
//{
//    return 1;
//}
//
//void rtos_unprotect(uint32_t protect)
//{
//    (void) protect;
//}
//
//void rtos_start_scheduler(void)
//{
//}
//
//int rtos_init(void)
//{
//    return 0;
//}
//
//rtos_task_handle rtos_get_task_handle(void)
//{
//    return rtos_get_current_task();
//}
//
//rtos_sched_state rtos_get_scheduler_state(void)
//{
//    return 0;
//}
//
//void rtos_trace_task(int id, void *task)
//{
//}
//
//void rtos_trace_mem(int id, void *ptr, int size, int free_size)
//{
//}
//
//void rtos_priority_set(rtos_task_handle handle, rtos_prio priority)
//{
//}

int aic_time_get(enum time_origin_t origin, uint32_t *sec, uint32_t *usec)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    *sec = tv.tv_sec;
    *usec = tv.tv_usec;

    return 0;
}
#endif
