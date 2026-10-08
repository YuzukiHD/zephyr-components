/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * The POSIX surface the AirPlay library asks for (compat.h, threads.h, sockets.h, memalign.h of
 * the library), on the Zephyr kernel and its socket API. It is force-included into the library
 * sources; the guards of the original headers are set here so that they stay empty.
 */

#ifndef IOS_POSIX_H_
#define IOS_POSIX_H_

#define COMPAT_H
#define THREADS_H
#define SOCKETS_H
#define MEMALIGN_H

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/byteorder.h>

/* sockets */
int ios_socket(int family, int type, int proto);
int ios_bind(int fd, const struct sockaddr *addr, socklen_t len);
#define socket(...) ios_socket(__VA_ARGS__)
#define bind(...) ios_bind(__VA_ARGS__)
#define listen(...) zsock_listen(__VA_ARGS__)
#define accept(...) zsock_accept(__VA_ARGS__)
#define connect(...) zsock_connect(__VA_ARGS__)
/* a connection that is gone reads as end of stream, not as an error to retry */
ssize_t ios_recv(int fd, void *buf, size_t len, int flags);
#define recv(...) ios_recv(__VA_ARGS__)
#define send(...) zsock_send(__VA_ARGS__)
#define recvfrom(...) zsock_recvfrom(__VA_ARGS__)
#define sendto(...) zsock_sendto(__VA_ARGS__)
/* the keep-alive tuning of a connection is optional */
int ios_setsockopt(int fd, int level, int opt, const void *val, socklen_t len);
#define setsockopt(...) ios_setsockopt(__VA_ARGS__)
#define getsockname(...) zsock_getsockname(__VA_ARGS__)
#define shutdown(...) zsock_shutdown(__VA_ARGS__)
#define select(...) zsock_select(__VA_ARGS__)
#define getaddrinfo(...) zsock_getaddrinfo(__VA_ARGS__)
#define freeaddrinfo(...) zsock_freeaddrinfo(__VA_ARGS__)
#define closesocket(fd) zsock_close(fd)
#define close(fd) zsock_close(fd)
#define fd_set zsock_fd_set
#define FD_ZERO(set) ZSOCK_FD_ZERO(set)
#define FD_SET(fd, set) ZSOCK_FD_SET(fd, set)
#define FD_CLR(fd, set) ZSOCK_FD_CLR(fd, set)
#define FD_ISSET(fd, set) ZSOCK_FD_ISSET(fd, set)
#define addrinfo zsock_addrinfo
#define SHUT_RD ZSOCK_SHUT_RD
#define SHUT_WR ZSOCK_SHUT_WR
#define SHUT_RDWR ZSOCK_SHUT_RDWR
#define SOL_TCP IPPROTO_TCP
/* not available: the library only uses it to empty a socket that has a backlog */
#define FIONREAD 0
#define ioctl(fd, req, arg) ((void)(fd), (void)(arg), -1)
#define ioctlsocket ioctl

#define SOCKET_GET_ERROR() (errno)
#define SOCKET_SET_ERROR(value) (errno = (value))
#define SOCKET_ERRORNAME(name) name

/* time */
int ios_gettimeofday(struct timeval *tv, void *tz);
int ios_clock_gettime(int clock_id, struct timespec *ts);
#define gettimeofday(tv, tz) ios_gettimeofday(tv, tz)
#define clock_gettime(id, ts) ios_clock_gettime(id, ts)
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 1
/* the math functions of the MD5 tables in the FairPlay code, from libm */
double sin(double x);
double fabs(double x);

/* strings the minimal C library does not have */
char *ios_strdup(const char *s);
int ios_sscanf(const char *str, const char *fmt, ...);
#define strdup(s) ios_strdup(s)
#define sscanf(...) ios_sscanf(__VA_ARGS__)

/* files: only utils_read_file() of the library, which the receiver does not call */
#define fopen(path, mode) ((FILE *)NULL)
#define fseek(f, off, whence) (-1)
#define ftell(f) (-1L)
#define fclose(f) (-1)
#define fread(buf, size, n, f) ((size_t)0)

/* the library has members of this name, the kernel one is of no use to it */
#undef _current

#endif
#define usleep(us) k_usleep(us)
#define sleepms(x) k_msleep(x)

/* byte order (<endian.h>) */
#define htobe64(x) sys_cpu_to_be64(x)
#define be64toh(x) sys_be64_to_cpu(x)

/* aligned memory */
#define ALIGNED_MALLOC(memptr, alignment, size) (memptr) = aligned_alloc(alignment, size)
#define ALIGNED_FREE(memptr) free(memptr)

/* threads */
struct ios_thread;
typedef struct ios_thread *thread_handle_t;
typedef struct k_mutex mutex_handle_t;
typedef struct k_condvar cond_handle_t;

#define THREAD_RETVAL void *
thread_handle_t ios_thread_create(void *(*func)(void *), void *arg);
void ios_thread_join(thread_handle_t thread);
int ios_cond_timedwait(cond_handle_t *cond, mutex_handle_t *mutex, const struct timespec *abs);

#define THREAD_CREATE(handle, func, arg) (handle) = ios_thread_create(func, arg)
#define THREAD_JOIN(handle) ios_thread_join(handle)
#define MUTEX_CREATE(handle) k_mutex_init(&(handle))
#define MUTEX_LOCK(handle) k_mutex_lock(&(handle), K_FOREVER)
#define MUTEX_UNLOCK(handle) k_mutex_unlock(&(handle))
#define MUTEX_DESTROY(handle) ((void)0)
#define COND_CREATE(handle) k_condvar_init(&(handle))
#define COND_SIGNAL(handle) k_condvar_signal(&(handle))
#define COND_DESTROY(handle) ((void)0)
#define pthread_cond_timedwait(cond, mutex, abs) ios_cond_timedwait(cond, mutex, abs)

/* the math functions of the MD5 tables in the FairPlay code, from libm */
double sin(double x);
double fabs(double x);

/* strings the minimal C library does not have */
char *ios_strdup(const char *s);
int ios_sscanf(const char *str, const char *fmt, ...);
#define strdup(s) ios_strdup(s)
#define sscanf(...) ios_sscanf(__VA_ARGS__)

/* files: only utils_read_file() of the library, which the receiver does not call */
#define fopen(path, mode) ((FILE *)NULL)
#define fseek(f, off, whence) (-1)
#define ftell(f) (-1L)
#define fclose(f) (-1)
#define fread(buf, size, n, f) ((size_t)0)

/* the library has members of this name, the kernel one is of no use to it */
#undef _current

#endif
