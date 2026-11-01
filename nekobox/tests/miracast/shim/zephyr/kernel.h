/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_KERNEL_H_
#define TEST_KERNEL_H_

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ARG_UNUSED(x) (void)(x)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define K_FOREVER (-1)
#define K_NO_WAIT 0
#define K_MSEC(ms) (ms)
#define K_MUTEX_DEFINE(name) pthread_mutex_t name = PTHREAD_MUTEX_INITIALIZER
#define k_mutex_lock(mutex, timeout) pthread_mutex_lock(mutex)
#define k_mutex_unlock(mutex) pthread_mutex_unlock(mutex)

extern _Atomic(int64_t) test_clock_offset;

static inline int64_t k_uptime_get(void)
{
	struct timespec now;

	assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000 +
		atomic_load(&test_clock_offset);
}

struct k_sem {
	pthread_mutex_t lock;
	pthread_cond_t ready;
	unsigned count, limit;
};

#define K_SEM_DEFINE(name, initial, maximum) struct k_sem name = { \
	PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, initial, maximum }

static inline int k_sem_take(struct k_sem *sem, int timeout)
{
	struct timespec end;
	int ret = 0;

	assert(clock_gettime(CLOCK_REALTIME, &end) == 0);
	if (timeout > 0) {
		end.tv_sec += timeout / 1000;
		end.tv_nsec += (timeout % 1000) * 1000000;
		end.tv_sec += end.tv_nsec / 1000000000;
		end.tv_nsec %= 1000000000;
	}
	pthread_mutex_lock(&sem->lock);
	while (!sem->count && !ret) {
		if (timeout == K_NO_WAIT) {
			ret = EAGAIN;
		} else if (timeout == K_FOREVER) {
			ret = pthread_cond_wait(&sem->ready, &sem->lock);
		} else {
			ret = pthread_cond_timedwait(&sem->ready, &sem->lock, &end);
		}
	}
	if (!ret) sem->count--;
	pthread_mutex_unlock(&sem->lock);
	return ret ? -EAGAIN : 0;
}

static inline void k_sem_give(struct k_sem *sem)
{
	pthread_mutex_lock(&sem->lock);
	if (sem->count < sem->limit) sem->count++;
	pthread_cond_signal(&sem->ready);
	pthread_mutex_unlock(&sem->lock);
}

struct k_msgq {
	pthread_mutex_t lock;
	uint8_t *data;
	size_t size;
	unsigned limit, count, first;
};

#define K_MSGQ_DEFINE(name, bytes, maximum, align) \
	static uint8_t name##_data[(bytes) * (maximum)]; \
	struct k_msgq name = {PTHREAD_MUTEX_INITIALIZER, name##_data, bytes, maximum, 0, 0}

static inline int k_msgq_put(struct k_msgq *q, const void *data, int timeout)
{
	assert(timeout == K_NO_WAIT);
	pthread_mutex_lock(&q->lock);
	int ret = q->count == q->limit ? -ENOMSG : 0;

	if (!ret) {
		memcpy(q->data + ((q->first + q->count) % q->limit) * q->size, data, q->size);
		q->count++;
	}
	pthread_mutex_unlock(&q->lock);
	return ret;
}

static inline int k_msgq_get(struct k_msgq *q, void *data, int timeout)
{
	assert(timeout == K_NO_WAIT);
	pthread_mutex_lock(&q->lock);
	int ret = !q->count ? -ENOMSG : 0;

	if (!ret) {
		memcpy(data, q->data + q->first * q->size, q->size);
		q->first = (q->first + 1) % q->limit;
		q->count--;
	}
	pthread_mutex_unlock(&q->lock);
	return ret;
}

static inline void k_msgq_purge(struct k_msgq *q)
{
	pthread_mutex_lock(&q->lock);
	q->count = q->first = 0;
	pthread_mutex_unlock(&q->lock);
}

#define K_THREAD_DEFINE(name, stack, entry, a, b, c, priority, options, delay) \
	static pthread_t name; \
	static void *name##_run(void *arg) { (void)arg; entry(a, b, c); return NULL; } \
	static void __attribute__((constructor)) name##_start(void) { \
		assert(pthread_create(&name, NULL, name##_run, NULL) == 0); }

#endif
