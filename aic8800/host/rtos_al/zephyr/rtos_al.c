/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
 * All Rights Reserved
 *
 * RTOS abstraction layer for the AIC8800 WiFi driver, on Zephyr kernel objects.
 *
 * Timeouts are in milliseconds, -1 waits forever. The driver numbers task
 * priorities the way a tick kernel with "higher is more urgent" does; they are
 * mapped to preemptible Zephyr priorities keeping their order.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>

#include "rtos_al.h"
#include "co_list.h"
#include "co_math.h"
#include "aic_log.h"

struct os_task {
	struct k_thread thread;		/* first: the task handle is the thread */
	k_thread_stack_t *stack;
};

struct os_queue {
	struct k_msgq q;
	char *buf;
};

struct os_timer {
	struct k_work_delayable work;
	rtos_timer_fct func;
	void *args;
	uint32_t ms;
	uint8_t auto_reload;
	bool active;
};

#define TIMER_STACK_SIZE 4096
static K_THREAD_STACK_DEFINE(timer_stack, TIMER_STACK_SIZE);
static struct k_work_q timer_q;
static bool timer_q_started;

/* "higher is more urgent" driver priorities to Zephyr ones (lower is more urgent) */
static int prio_map(rtos_prio p)
{
	int z = 14 - (p - 10) / 2;

	return CLAMP(z, 0, 14);
}

static k_timeout_t to_timeout(int ms)
{
	return ms < 0 ? K_FOREVER : K_MSEC(ms);
}

unsigned long rtos_now(bool isr)
{
	return k_uptime_get_32();
}

void rtos_msleep(uint32_t time_in_ms)
{
	k_msleep(time_in_ms);
}

void rtos_udelay(unsigned int us)
{
	k_busy_wait(us);
}

void *rtos_malloc(uint32_t size)
{
	return malloc(size);
}

void *rtos_calloc(uint32_t nb_elt, uint32_t size)
{
	return calloc(nb_elt, size);
}

void rtos_free(void *ptr)
{
	free(ptr);
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
	if (total_size) {
		*total_size = 0;
	}
	if (free_size) {
		*free_size = 0;
	}
	if (min_free_size) {
		*min_free_size = 0;
	}
}

/* interrupts off, nestable */
static unsigned int crit_key;
static int crit_depth;

uint32_t rtos_entercritical(void)
{
	unsigned int key = irq_lock();

	if (crit_depth++ == 0) {
		crit_key = key;
	}

	return 0;
}

void rtos_exitcritical(void)
{
	if (--crit_depth == 0) {
		irq_unlock(crit_key);
	}
}

uint32_t rtos_protect(void)
{
	return rtos_entercritical() + 1;
}

void rtos_unprotect(uint32_t protect)
{
	rtos_exitcritical();
}

rtos_task_handle rtos_get_current_task(void)
{
	return (rtos_task_handle)k_current_get();
}

rtos_task_handle rtos_get_task_handle(void)
{
	return rtos_get_current_task();
}

int rtos_task_create(rtos_task_fct func, const char *const name, int task_id,
		     const uint16_t stack_depth, void *const params, rtos_prio prio,
		     rtos_task_handle *const task_handle)
{
	struct os_task *t;
	size_t size = ROUND_UP(MAX(stack_depth, 2048), 16);

	if (task_handle == NULL) {
		return -1;
	}
	t = calloc(1, sizeof(*t));
	if (t == NULL) {
		return -2;
	}
	t->stack = aligned_alloc(16, size);
	if (t->stack == NULL) {
		free(t);
		return -2;
	}
	k_thread_create(&t->thread, t->stack, size, (k_thread_entry_t)func, params, NULL, NULL,
			prio_map(prio), 0, K_NO_WAIT);
	k_thread_name_set(&t->thread, name);
	*task_handle = (rtos_task_handle)&t->thread;

	return 0;
}

void rtos_task_delete(rtos_task_handle task_handle)
{
	struct os_task *t = task_handle;

	if (t == NULL || (k_tid_t)t == k_current_get()) {
		/* a task that ends itself leaves its memory behind, it cannot free it */
		k_thread_abort(k_current_get());
		return;
	}
	k_thread_abort(&t->thread);
	free(t->stack);
	free(t);
}

void rtos_task_suspend(int duration)
{
	if (duration == -1) {
		k_thread_suspend(k_current_get());
	} else {
		k_msleep(duration);
	}
}

void rtos_task_resume(rtos_task_handle task_handle)
{
	k_thread_resume((k_tid_t)task_handle);
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
	return k_thread_priority_get((k_tid_t)task_handle);
}

void rtos_task_set_priority(rtos_task_handle task_handle, uint32_t priority)
{
	k_thread_priority_set((k_tid_t)task_handle, prio_map(priority));
}

void rtos_priority_set(rtos_task_handle handle, rtos_prio priority)
{
	rtos_task_set_priority(handle, priority);
}

int rtos_queue_create(int elt_size, int nb_elt, rtos_queue *queue, const char *const name)
{
	struct os_queue *q;

	if (queue == NULL) {
		return -1;
	}
	q = calloc(1, sizeof(*q));
	if (q == NULL) {
		return -2;
	}
	q->buf = malloc((size_t)elt_size * nb_elt);
	if (q->buf == NULL) {
		free(q);
		return -2;
	}
	k_msgq_init(&q->q, q->buf, elt_size, nb_elt);
	*queue = q;

	return 0;
}

void rtos_queue_delete(rtos_queue queue)
{
	struct os_queue *q = queue;

	if (q) {
		k_msgq_purge(&q->q);
		free(q->buf);
		free(q);
	}
}

bool rtos_queue_is_empty(rtos_queue queue)
{
	return k_msgq_num_used_get(&((struct os_queue *)queue)->q) == 0;
}

bool rtos_queue_is_full(rtos_queue queue)
{
	return k_msgq_num_free_get(&((struct os_queue *)queue)->q) == 0;
}

int rtos_queue_cnt(rtos_queue queue)
{
	return k_msgq_num_used_get(&((struct os_queue *)queue)->q);
}

int rtos_queue_write(rtos_queue queue, void *msg, int timeout, bool isr)
{
	struct os_queue *q = queue;

	if (q == NULL) {
		return -1;
	}

	return k_msgq_put(&q->q, msg, isr ? K_NO_WAIT : to_timeout(timeout)) == 0 ? 0 : -2;
}

int rtos_queue_read(rtos_queue queue, void *msg, int timeout, bool isr)
{
	struct os_queue *q = queue;

	if (q == NULL) {
		return -1;
	}

	return k_msgq_get(&q->q, msg, isr ? K_NO_WAIT : to_timeout(timeout)) == 0 ? 0 : -2;
}

int rtos_queue_peek(rtos_queue queue, void *msg, int timeout, bool isr)
{
	struct os_queue *q = queue;

	if (q == NULL) {
		return -1;
	}

	return k_msgq_peek(&q->q, msg) == 0 ? 0 : -2;
}

int rtos_queue_reset(rtos_queue queue)
{
	struct os_queue *q = queue;

	if (q == NULL) {
		return -1;
	}
	k_msgq_purge(&q->q);

	return 0;
}

int rtos_semaphore_create(rtos_semaphore *semaphore, const char *const name, int max_count,
			  int init_count)
{
	struct k_sem *sem;

	if (semaphore == NULL) {
		return -1;
	}
	sem = malloc(sizeof(*sem));
	if (sem == NULL) {
		return -3;
	}
	k_sem_init(sem, init_count, max_count > 0 ? max_count : K_SEM_MAX_LIMIT);
	*semaphore = sem;

	return 0;
}

int rtos_semaphore_get_count(rtos_semaphore semaphore)
{
	return k_sem_count_get(semaphore);
}

void rtos_semaphore_delete(rtos_semaphore semaphore)
{
	free(semaphore);
}

int rtos_semaphore_wait(rtos_semaphore semaphore, int timeout)
{
	if (semaphore == NULL) {
		return -1;
	}

	return k_sem_take(semaphore, to_timeout(timeout)) == 0 ? 0 : -1;
}

int rtos_semaphore_signal(rtos_semaphore semaphore, bool isr)
{
	if (semaphore == NULL) {
		return -1;
	}
	k_sem_give(semaphore);

	return 0;
}

static void timer_handler(struct k_work *work)
{
	struct k_work_delayable *d = k_work_delayable_from_work(work);
	struct os_timer *t = CONTAINER_OF(d, struct os_timer, work);

	if (t->auto_reload) {
		k_work_schedule_for_queue(&timer_q, &t->work, K_MSEC(t->ms));
	} else {
		t->active = false;
	}
	if (t->func) {
		t->func(t->args);
	}
}

uint32_t rtos_timer_create(const char *const name, rtos_timer *timer, const uint32_t ms,
			   const uint8_t autoReload, void *const args, rtos_timer_fct func)
{
	struct os_timer *t;

	if (timer == NULL) {
		return -1;
	}
	if (!timer_q_started) {
		k_work_queue_init(&timer_q);
		k_work_queue_start(&timer_q, timer_stack, K_THREAD_STACK_SIZEOF(timer_stack),
				   prio_map(60), NULL);
		k_thread_name_set(&timer_q.thread, "aic_timer");
		timer_q_started = true;
	}
	t = calloc(1, sizeof(*t));
	if (t == NULL) {
		return -2;
	}
	t->ms = ms;
	t->auto_reload = autoReload;
	t->func = func;
	t->args = args;
	k_work_init_delayable(&t->work, timer_handler);
	*timer = t;

	return 0;
}

int rtos_timer_start(rtos_timer timer, uint32_t ms, bool isr)
{
	struct os_timer *t = timer;

	if (t == NULL) {
		return -1;
	}
	t->active = true;
	/* a start of a running timer begins the period again */
	k_work_reschedule_for_queue(&timer_q, &t->work, K_MSEC(t->ms));

	return 0;
}

int rtos_timer_stop(rtos_timer timer, uint32_t wait_ms)
{
	struct os_timer *t = timer;

	if (t == NULL) {
		return -1;
	}
	t->active = false;
	k_work_cancel_delayable(&t->work);

	return 0;
}

int rtos_timer_stop_isr(rtos_timer timer)
{
	return rtos_timer_stop(timer, 0);
}

int rtos_timer_delete(rtos_timer timer, uint32_t wait_ms)
{
	struct os_timer *t = timer;
	struct k_work_sync sync;

	if (t == NULL) {
		return -1;
	}
	k_work_cancel_delayable_sync(&t->work, &sync);
	free(t);

	return 0;
}

int rtos_timer_is_active(rtos_timer timer)
{
	struct os_timer *t = timer;

	return t != NULL && t->active;
}

int rtos_mutex_recursive_create(rtos_mutex *mutex)
{
	return rtos_mutex_create(mutex, NULL);
}

int rtos_mutex_recursive_lock(rtos_mutex mutex)
{
	return rtos_mutex_lock(mutex, -1);
}

int rtos_mutex_recursive_unlock(rtos_mutex mutex)
{
	return rtos_mutex_unlock(mutex);
}

/* Zephyr mutexes are recursive, so one object serves both kinds */
int rtos_mutex_create(rtos_mutex *mutex, const char *const name)
{
	struct k_mutex *m;

	if (mutex == NULL) {
		return -1;
	}
	m = malloc(sizeof(*m));
	if (m == NULL) {
		return -3;
	}
	k_mutex_init(m);
	*mutex = m;

	return 0;
}

void rtos_mutex_delete(rtos_mutex mutex)
{
	free(mutex);
}

int rtos_mutex_lock(rtos_mutex mutex, int timeout)
{
	if (mutex == NULL) {
		return -1;
	}

	return k_mutex_lock(mutex, to_timeout(timeout)) == 0 ? 0 : -1;
}

int rtos_mutex_unlock(rtos_mutex mutex)
{
	if (mutex == NULL) {
		return -1;
	}

	return k_mutex_unlock(mutex) == 0 ? 0 : -1;
}

/* not used by the driver */
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

void rtos_start_scheduler(void)
{
}

int rtos_init(void)
{
	return 0;
}

rtos_sched_state rtos_get_scheduler_state(void)
{
	return 0;
}

int aic_time_get(enum time_origin_t origin, uint32_t *sec, uint32_t *usec)
{
	int64_t ms = k_uptime_get();

	if (sec) {
		*sec = ms / 1000;
	}
	if (usec) {
		*usec = (ms % 1000) * 1000;
	}

	return 0;
}
