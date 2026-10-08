/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Threads and clocks for the AirPlay library, see shim/ios_posix.h */

#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/printk.h>

/* the file is compiled without the shim, the library's view of these is in the header */
struct ios_thread {
	struct k_thread thread;
	k_thread_stack_t *stack;
	void *(*func)(void *);
	void *arg;
};

static void thread_entry(void *p1, void *p2, void *p3)
{
	struct ios_thread *t = p1;

	t->func(t->arg);
}

struct ios_thread *ios_thread_create(void *(*func)(void *), void *arg)
{
	struct ios_thread *t = calloc(1, sizeof(*t));

	if (t == NULL) {
		return NULL;
	}
	t->stack = k_thread_stack_alloc(CONFIG_IOS_MIRROR_THREAD_STACK, 0);
	if (t->stack == NULL) {
		printk("airplay: no memory for a thread stack\n");
		free(t);
		return NULL;
	}
	t->func = func;
	t->arg = arg;
	k_thread_create(&t->thread, t->stack, CONFIG_IOS_MIRROR_THREAD_STACK, thread_entry, t, NULL,
			NULL, CONFIG_IOS_MIRROR_THREAD_PRIORITY, 0, K_NO_WAIT);
	static int serial;
	char name[16];

	snprintk(name, sizeof(name), "airplay%d", serial++);
	k_thread_name_set(&t->thread, name);

	return t;
}

void ios_thread_join(struct ios_thread *t)
{
	if (t != NULL) {
		k_thread_join(&t->thread, K_FOREVER);
		k_thread_stack_free(t->stack);
		free(t);
	}
}

/* The time of day is the uptime: the library only compares times it took itself */
int ios_gettimeofday(struct timeval *tv, void *tz)
{
	int64_t us = k_ticks_to_us_floor64(k_uptime_ticks());

	tv->tv_sec = us / 1000000;
	tv->tv_usec = us % 1000000;

	return 0;
}

int ios_clock_gettime(int clock_id, struct timespec *ts)
{
	int64_t ns = k_ticks_to_ns_floor64(k_uptime_ticks());

	ts->tv_sec = ns / 1000000000;
	ts->tv_nsec = ns % 1000000000;

	return 0;
}

int ios_cond_timedwait(struct k_condvar *cond, struct k_mutex *mutex, const struct timespec *abs)
{
	int64_t now_us = k_ticks_to_us_floor64(k_uptime_ticks());
	int64_t at_us = (int64_t)abs->tv_sec * 1000000 + abs->tv_nsec / 1000;

	return k_condvar_wait(cond, mutex, at_us > now_us ? K_USEC(at_us - now_us) : K_NO_WAIT) ==
	       0 ? 0 : -1;
}

char *ios_strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = malloc(n);

	if (d != NULL) {
		memcpy(d, s, n);
	}

	return d;
}

/* %f, %u and literal characters: what the library parses */
int ios_sscanf(const char *str, const char *fmt, ...)
{
	va_list ap;
	int count = 0;

	va_start(ap, fmt);
	while (*fmt != '\0') {
		char *end;

		if (fmt[0] == '%' && fmt[1] == 'f') {
			float v = 0.0f, scale = 0.1f;
			bool neg = (*str == '-'), digits = false;

			end = (char *)str + (neg || *str == '+');
			for (; *end >= '0' && *end <= '9'; end++) {
				v = v * 10.0f + (*end - '0');
				digits = true;
			}
			if (*end == '.') {
				for (end++; *end >= '0' && *end <= '9'; end++) {
					v += (*end - '0') * scale;
					scale /= 10.0f;
					digits = true;
				}
			}
			if (!digits) {
				break;
			}
			*va_arg(ap, float *) = neg ? -v : v;
			str = end;
			fmt += 2;
			count++;
		} else if (fmt[0] == '%' && fmt[1] == 'u') {
			unsigned long v = strtoul(str, &end, 10);

			if (end == str) {
				break;
			}
			*va_arg(ap, unsigned int *) = v;
			str = end;
			fmt += 2;
			count++;
		} else if (*fmt == *str) {
			fmt++;
			str++;
		} else {
			break;
		}
	}
	va_end(ap);

	return count;
}

ssize_t ios_recv(int fd, void *buf, size_t len, int flags)
{
	ssize_t ret = zsock_recv(fd, buf, len, flags);

	if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
		return 0;
	}

	return ret;
}

int ios_setsockopt(int fd, int level, int opt, const void *val, socklen_t len)
{
	int ret = zsock_setsockopt(fd, level, opt, val, len);

	if (ret < 0 && errno == ENOPROTOOPT &&
	    ((level == SOL_SOCKET && opt == SO_KEEPALIVE) ||
	     (level == IPPROTO_TCP && opt >= TCP_KEEPIDLE && opt <= TCP_KEEPCNT))) {
		return 0;
	}

	return ret;
}

int ios_socket(int family, int type, int proto)
{
	int fd = zsock_socket(family, type, proto);

	if (fd < 0) {
		printk("airplay: socket(%d, %d, %d) failed: %d\n", family, type, proto, errno);
	}

	return fd;
}

int ios_bind(int fd, const struct sockaddr *addr, socklen_t len)
{
	int ret = zsock_bind(fd, addr, len);

	if (ret < 0) {
		printk("airplay: bind(%d, port %u) failed: %d\n", fd,
		       ntohs(((const struct sockaddr_in *)addr)->sin_port), errno);
	}

	return ret;
}
