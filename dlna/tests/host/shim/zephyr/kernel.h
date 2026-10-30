/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#define ARG_UNUSED(x) (void)(x)
#define K_THREAD_STACK_DEFINE(n, s) static char n[s]
#define K_THREAD_STACK_SIZEOF(n) sizeof(n)
#define K_NO_WAIT 0
#define K_SECONDS(s) ((s) * 1000)
struct k_thread { pthread_t thread; void (*entry)(void *, void *, void *); };
static inline void *host_entry(void *p)
{
	struct k_thread *t = p;
	t->entry(NULL, NULL, NULL);
	return NULL;
}
static inline void k_thread_create(struct k_thread *t, void *stack, size_t size,
	void (*entry)(void *, void *, void *), void *a, void *b, void *c,
	int priority, int flags, int delay)
{
	(void)stack; (void)size; (void)a; (void)b; (void)c;
	(void)priority; (void)flags; (void)delay;
	t->entry = entry;
	pthread_create(&t->thread, NULL, host_entry, t);
}
static inline void k_thread_name_set(struct k_thread *t, const char *name)
{
	(void)t; (void)name;
}
static inline int k_thread_join(struct k_thread *t, int timeout)
{
	(void)timeout;
	return pthread_join(t->thread, NULL);
}
static inline int64_t k_uptime_get(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
