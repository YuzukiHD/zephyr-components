/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * The BSD socket calls of the driver and the supplicant (declared in
 * compat/lwip/sockets.h with their own constants) on the Zephyr socket layer.
 *
 * This file must not see the declarations of that header, as they use the same
 * names for the types as the Zephyr headers do. The functions take pointers to
 * the structures of that header, which are mapped here by hand.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/byteorder.h>

/* constants and layouts of compat/lwip/sockets.h */
#define C_AF_INET	2
#define C_SOL_SOCKET	0xfff
#define C_SO_REUSEADDR	0x0004
#define C_SO_BROADCAST	0x0020
#define C_SO_RCVTIMEO	0x1006
#define C_SO_SNDTIMEO	0x1005
#define C_SO_RCVBUF	0x1002
#define C_SO_ERROR	0x1007
#define C_MSG_PEEK	0x01
#define C_MSG_DONTWAIT	0x08
#define C_FD_SETSIZE	64

struct c_in_addr {
	uint32_t s_addr;
};

struct c_sockaddr_in {
	uint8_t sin_len;
	uint8_t sin_family;
	uint16_t sin_port;
	struct c_in_addr sin_addr;
	char sin_zero[8];
};

struct c_iovec {
	void *iov_base;
	size_t iov_len;
};

struct c_msghdr {
	void *msg_name;
	uint32_t msg_namelen;
	struct c_iovec *msg_iov;
	int msg_iovlen;
	void *msg_control;
	uint32_t msg_controllen;
	int msg_flags;
};

struct c_timeval {
	long tv_sec;
	long tv_usec;
};

struct c_fd_set {
	uint32_t bits[C_FD_SETSIZE / 32];
};

static int addr_to_z(const void *addr, uint32_t len, struct sockaddr_in *z)
{
	const struct c_sockaddr_in *c = addr;

	if (c == NULL || len < sizeof(*c) || c->sin_family != C_AF_INET) {
		errno = EAFNOSUPPORT;
		return -1;
	}
	memset(z, 0, sizeof(*z));
	z->sin_family = AF_INET;
	z->sin_port = c->sin_port;
	z->sin_addr.s_addr = c->sin_addr.s_addr;

	return 0;
}

static void addr_from_z(const struct sockaddr_in *z, void *addr, uint32_t *len)
{
	struct c_sockaddr_in *c = addr;

	memset(c, 0, sizeof(*c));
	c->sin_len = sizeof(*c);
	c->sin_family = C_AF_INET;
	c->sin_port = z->sin_port;
	c->sin_addr.s_addr = z->sin_addr.s_addr;
	*len = sizeof(*c);
}

static int flags_to_z(int flags)
{
	return ((flags & C_MSG_PEEK) ? ZSOCK_MSG_PEEK : 0) |
	       ((flags & C_MSG_DONTWAIT) ? ZSOCK_MSG_DONTWAIT : 0);
}

int socket(int domain, int type, int protocol)
{
	if (domain != C_AF_INET) {
		errno = EAFNOSUPPORT;
		return -1;
	}

	return zsock_socket(AF_INET, type, protocol);
}

int bind(int fd, const void *addr, uint32_t len)
{
	struct sockaddr_in z;

	if (addr_to_z(addr, len, &z) != 0) {
		return -1;
	}

	return zsock_bind(fd, (struct sockaddr *)&z, sizeof(z));
}

int connect(int fd, const void *addr, uint32_t len)
{
	struct sockaddr_in z;

	if (addr_to_z(addr, len, &z) != 0) {
		return -1;
	}

	return zsock_connect(fd, (struct sockaddr *)&z, sizeof(z));
}

int listen(int fd, int backlog)
{
	return zsock_listen(fd, backlog);
}

int accept(int fd, void *addr, uint32_t *len)
{
	struct sockaddr_in z;
	socklen_t zlen = sizeof(z);
	int nfd = zsock_accept(fd, (struct sockaddr *)&z, &zlen);

	if (nfd >= 0 && addr != NULL && len != NULL) {
		addr_from_z(&z, addr, len);
	}

	return nfd;
}

int getsockname(int fd, void *addr, uint32_t *len)
{
	struct sockaddr_in z;
	socklen_t zlen = sizeof(z);
	int ret = zsock_getsockname(fd, (struct sockaddr *)&z, &zlen);

	if (ret == 0) {
		addr_from_z(&z, addr, len);
	}

	return ret;
}

int setsockopt(int fd, int level, int opt, const void *val, uint32_t len)
{
	if (level != C_SOL_SOCKET) {
		return zsock_setsockopt(fd, level, opt, val, len);
	}
	switch (opt) {
	case C_SO_REUSEADDR:
		return zsock_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, val, len);
	case C_SO_BROADCAST:
		return zsock_setsockopt(fd, SOL_SOCKET, SO_BROADCAST, val, len);
	case C_SO_RCVBUF:
		return zsock_setsockopt(fd, SOL_SOCKET, SO_RCVBUF, val, len);
	case C_SO_RCVTIMEO:
	case C_SO_SNDTIMEO: {
		const struct c_timeval *c = val;
		struct zsock_timeval z = {.tv_sec = c->tv_sec, .tv_usec = c->tv_usec};

		return zsock_setsockopt(fd, SOL_SOCKET, opt == C_SO_RCVTIMEO ? SO_RCVTIMEO : SO_SNDTIMEO,
					&z, sizeof(z));
	}
	default:
		/* the option does not exist here; the callers only tune the buffers */
		return 0;
	}
}

int getsockopt(int fd, int level, int opt, void *val, uint32_t *len)
{
	socklen_t zlen = *len;
	int ret;

	if (level == C_SOL_SOCKET && opt == C_SO_ERROR) {
		opt = SO_ERROR;
	}
	ret = zsock_getsockopt(fd, level == C_SOL_SOCKET ? SOL_SOCKET : level, opt, val, &zlen);
	*len = zlen;

	return ret;
}

ssize_t send(int fd, const void *buf, size_t len, int flags)
{
	return zsock_send(fd, buf, len, flags_to_z(flags));
}

ssize_t recv(int fd, void *buf, size_t len, int flags)
{
	return zsock_recv(fd, buf, len, flags_to_z(flags));
}

ssize_t sendto(int fd, const void *buf, size_t len, int flags, const void *to, uint32_t tolen)
{
	struct sockaddr_in z;

	if (to == NULL) {
		return zsock_send(fd, buf, len, flags_to_z(flags));
	}
	if (addr_to_z(to, tolen, &z) != 0) {
		return -1;
	}

	return zsock_sendto(fd, buf, len, flags_to_z(flags), (struct sockaddr *)&z, sizeof(z));
}

ssize_t recvfrom(int fd, void *buf, size_t len, int flags, void *from, uint32_t *fromlen)
{
	struct sockaddr_in z;
	socklen_t zlen = sizeof(z);
	ssize_t ret;

	if (from == NULL) {
		return zsock_recv(fd, buf, len, flags_to_z(flags));
	}
	ret = zsock_recvfrom(fd, buf, len, flags_to_z(flags), (struct sockaddr *)&z, &zlen);
	if (ret >= 0) {
		addr_from_z(&z, from, fromlen);
	}

	return ret;
}

/* the pieces are put together and sent as one datagram (or one run of stream data) */
ssize_t sendmsg(int fd, const struct c_msghdr *msg, int flags)
{
	size_t total = 0, off = 0;
	uint8_t *buf;
	ssize_t ret;

	for (int i = 0; i < msg->msg_iovlen; i++) {
		total += msg->msg_iov[i].iov_len;
	}
	buf = k_malloc(total ? total : 1);
	if (buf == NULL) {
		errno = ENOMEM;
		return -1;
	}
	for (int i = 0; i < msg->msg_iovlen; i++) {
		memcpy(buf + off, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len);
		off += msg->msg_iov[i].iov_len;
	}
	if (msg->msg_name != NULL) {
		ret = sendto(fd, buf, total, flags, msg->msg_name, msg->msg_namelen);
	} else {
		ret = zsock_send(fd, buf, total, flags_to_z(flags));
	}
	k_free(buf);

	return ret;
}

int select(int nfds, struct c_fd_set *rd, struct c_fd_set *wr, struct c_fd_set *ex,
	   struct c_timeval *tv)
{
	zsock_fd_set zr, zw, ze;
	struct zsock_timeval ztv;
	struct c_fd_set *in[3] = {rd, wr, ex};
	zsock_fd_set *zs[3] = {&zr, &zw, &ze};
	int ret;

	for (int i = 0; i < 3; i++) {
		ZSOCK_FD_ZERO(zs[i]);
		for (int fd = 0; in[i] != NULL && fd < nfds && fd < C_FD_SETSIZE; fd++) {
			if ((in[i]->bits[fd / 32] >> (fd % 32)) & 1U) {
				ZSOCK_FD_SET(fd, zs[i]);
			}
		}
	}
	if (tv != NULL) {
		ztv.tv_sec = tv->tv_sec;
		ztv.tv_usec = tv->tv_usec;
	}
	ret = zsock_select(nfds, rd ? &zr : NULL, wr ? &zw : NULL, ex ? &ze : NULL, tv ? &ztv : NULL);
	if (ret < 0) {
		return ret;
	}
	for (int i = 0; i < 3; i++) {
		if (in[i] == NULL) {
			continue;
		}
		memset(in[i], 0, sizeof(*in[i]));
		for (int fd = 0; fd < nfds && fd < C_FD_SETSIZE; fd++) {
			if (ZSOCK_FD_ISSET(fd, zs[i])) {
				in[i]->bits[fd / 32] |= 1UL << (fd % 32);
			}
		}
	}

	return ret;
}

int fcntl(int fd, int cmd, ...)
{
	va_list ap;
	int arg;

	va_start(ap, cmd);
	arg = va_arg(ap, int);
	va_end(ap);

	return zsock_fcntl(fd, cmd, arg);
}

int close(int fd)
{
	return zsock_close(fd);
}

int shutdown(int fd, int how)
{
	return zsock_shutdown(fd, how);
}

/* the Zephyr headers have them as macros */
#undef htons
#undef ntohs
#undef htonl
#undef ntohl

uint16_t htons(uint16_t v)
{
	return sys_cpu_to_be16(v);
}

uint16_t ntohs(uint16_t v)
{
	return sys_be16_to_cpu(v);
}

uint32_t htonl(uint32_t v)
{
	return sys_cpu_to_be32(v);
}

uint32_t ntohl(uint32_t v)
{
	return sys_be32_to_cpu(v);
}

char *inet_ntoa(struct c_in_addr addr)
{
	static char buf[16];
	const uint8_t *b = (const uint8_t *)&addr.s_addr;

	snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);

	return buf;
}

int inet_aton(const char *cp, struct c_in_addr *addr)
{
	return zsock_inet_pton(AF_INET, cp, &addr->s_addr) == 1;
}

uint32_t inet_addr(const char *cp)
{
	struct c_in_addr a;

	return inet_aton(cp, &a) ? a.s_addr : 0xffffffffU;
}

const char *inet_ntop(int af, const void *src, char *dst, uint32_t size)
{
	return zsock_inet_ntop(af == C_AF_INET ? AF_INET : AF_INET6, src, dst, size);
}

int inet_pton(int af, const char *src, void *dst)
{
	return zsock_inet_pton(af == C_AF_INET ? AF_INET : AF_INET6, src, dst);
}
