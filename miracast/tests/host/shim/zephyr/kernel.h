/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_KERNEL_H_
#define TEST_KERNEL_H_
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#define ARG_UNUSED(x) (void)(x)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define K_FOREVER (-1)
#define K_MUTEX_DEFINE(name) pthread_mutex_t name = PTHREAD_MUTEX_INITIALIZER
#define k_mutex_lock(m, t) pthread_mutex_lock(m)
#define k_mutex_unlock(m) pthread_mutex_unlock(m)

struct k_sem {
	pthread_mutex_t lock;
	pthread_cond_t ready;
	unsigned count;
};
#define K_SEM_DEFINE(name, initial, max) struct k_sem name = { \
	PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, initial }
static inline void k_sem_take(struct k_sem *sem, int timeout)
{
	(void)timeout;
	pthread_mutex_lock(&sem->lock);
	while (!sem->count) pthread_cond_wait(&sem->ready, &sem->lock);
	sem->count--;
	pthread_mutex_unlock(&sem->lock);
}
static inline void k_sem_give(struct k_sem *sem)
{
	pthread_mutex_lock(&sem->lock);
	sem->count = 1;
	pthread_cond_signal(&sem->ready);
	pthread_mutex_unlock(&sem->lock);
}
static inline void k_sem_reset(struct k_sem *sem)
{
	pthread_mutex_lock(&sem->lock);
	sem->count = 0;
	pthread_mutex_unlock(&sem->lock);
}
static inline int64_t k_uptime_get(void)
{
	struct timespec ts;
	assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static inline void k_msleep(int ms) { usleep(ms * 1000); }
#define K_THREAD_DEFINE(name, stack, entry, a, b, c, priority, options, delay) \
	static pthread_t name; \
	static void *name##_run(void *arg) { (void)arg; entry(a, b, c); return NULL; } \
	static void __attribute__((constructor)) name##_start(void) { \
		assert(pthread_create(&name, NULL, name##_run, NULL) == 0); }
#endif
