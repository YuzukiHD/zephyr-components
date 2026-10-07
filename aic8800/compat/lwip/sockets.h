/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSD socket API for the driver and the supplicant. The types and constants are
 * declared here on their own; src/aic_sockets.c maps the calls to the Zephyr
 * socket layer.
 */

#ifndef AIC_SOCKETS_H_
#define AIC_SOCKETS_H_

#include <stddef.h>
#include <stdint.h>

#ifndef ssize_t
#include <sys/types.h>
#endif

typedef uint32_t socklen_t;
typedef uint8_t sa_family_t;
typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

#define AF_UNSPEC	0
#define AF_INET		2
#define PF_INET		AF_INET
#define PF_UNSPEC	AF_UNSPEC

#define SOCK_STREAM	1
#define SOCK_DGRAM	2
#define SOCK_RAW	3

#define IPPROTO_IP	0
#define IPPROTO_TCP	6
#define IPPROTO_UDP	17

#define SOL_SOCKET	0xfff
#define SO_REUSEADDR	0x0004
#define SO_BROADCAST	0x0020
#define SO_RCVTIMEO	0x1006
#define SO_SNDTIMEO	0x1005
#define SO_RCVBUF	0x1002
#define SO_ERROR	0x1007

#define MSG_PEEK	0x01
#define MSG_DONTWAIT	0x08

#define INADDR_ANY	((in_addr_t)0x00000000UL)
#define INADDR_LOOPBACK	((in_addr_t)0x7f000001UL)
#define INADDR_NONE	((in_addr_t)0xffffffffUL)

#define F_GETFL		3
#define F_SETFL		4
#define O_NONBLOCK	0x4000

struct in_addr {
	uint32_t s_addr;
};

struct sockaddr {
	uint8_t sa_len;
	sa_family_t sa_family;
	char sa_data[14];
};

struct sockaddr_in {
	uint8_t sin_len;
	sa_family_t sin_family;
	in_port_t sin_port;
	struct in_addr sin_addr;
	char sin_zero[8];
};

struct sockaddr_storage {
	uint8_t s2_len;
	sa_family_t ss_family;
	char s2_data1[2];
	uint32_t s2_data2[3];
};

struct iovec {
	void *iov_base;
	size_t iov_len;
};

struct msghdr {
	void *msg_name;
	socklen_t msg_namelen;
	struct iovec *msg_iov;
	int msg_iovlen;
	void *msg_control;
	socklen_t msg_controllen;
	int msg_flags;
};

struct timeval {
	long tv_sec;
	long tv_usec;
};

#define FD_SETSIZE 64
typedef struct {
	uint32_t bits[FD_SETSIZE / 32];
} fd_set;

#define FD_ZERO(s)	do { (s)->bits[0] = 0; (s)->bits[1] = 0; } while (0)
#define FD_SET(n, s)	((s)->bits[(n) / 32] |= (1UL << ((n) % 32)))
#define FD_CLR(n, s)	((s)->bits[(n) / 32] &= ~(1UL << ((n) % 32)))
#define FD_ISSET(n, s)	(((s)->bits[(n) / 32] >> ((n) % 32)) & 1U)

#ifndef htons
uint16_t htons(uint16_t v);
uint16_t ntohs(uint16_t v);
uint32_t htonl(uint32_t v);
uint32_t ntohl(uint32_t v);
#endif

int socket(int domain, int type, int protocol);
int bind(int fd, const struct sockaddr *addr, socklen_t len);
int connect(int fd, const struct sockaddr *addr, socklen_t len);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *addr, socklen_t *len);
int getsockname(int fd, struct sockaddr *addr, socklen_t *len);
int getsockopt(int fd, int level, int opt, void *val, socklen_t *len);
int setsockopt(int fd, int level, int opt, const void *val, socklen_t len);
ssize_t send(int fd, const void *buf, size_t len, int flags);
ssize_t recv(int fd, void *buf, size_t len, int flags);
ssize_t sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *to,
	       socklen_t tolen);
ssize_t recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *from,
		 socklen_t *fromlen);
ssize_t sendmsg(int fd, const struct msghdr *msg, int flags);
int select(int nfds, fd_set *rd, fd_set *wr, fd_set *ex, struct timeval *tv);
int fcntl(int fd, int cmd, ...);
int close(int fd);
int shutdown(int fd, int how);

#endif /* AIC_SOCKETS_H_ */
