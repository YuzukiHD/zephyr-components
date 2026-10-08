/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <mirror_zephyr/mdns.h>

#define MDNS_PORT	5353
#define TYPE_A		1
#define TYPE_PTR	12
#define TYPE_SRV	33
#define CLASS_IN	1
#define QU_BIT		0x8000
#define PKT_MAX		1500
#define QUERY_PERIOD_MS	1000

/* Reads a (possibly compressed) name into dotted text; returns the offset after it in the record */
static int read_name(const uint8_t *p, size_t len, size_t off, char *out, size_t size)
{
	size_t o = off, w = 0;
	int next = -1, hops = 0;

	while (true) {
		uint8_t l;

		if (o >= len) {
			return -1;
		}
		l = p[o];
		if ((l & 0xc0) == 0xc0) {
			if (o + 1 >= len || ++hops > 16) {
				return -1;
			}
			if (next < 0) {
				next = o + 2;
			}
			o = ((l & 0x3f) << 8) | p[o + 1];
			continue;
		}
		o++;
		if (l == 0U) {
			break;
		}
		if (o + l > len || w + l + 2U > size) {
			return -1;
		}
		memcpy(out + w, p + o, l);
		w += l;
		out[w++] = '.';
		o += l;
	}
	if (w > 0U) {
		w--;
	}
	out[w] = '\0';

	return next >= 0 ? next : (int)o;
}

static bool name_equal(const char *a, const char *b)
{
	size_t l = strlen(a);

	return l == strlen(b) && strncasecmp(a, b, l) == 0;
}

static size_t put_name(uint8_t *p, const char *name)
{
	size_t w = 0;

	while (*name != '\0') {
		const char *dot = strchr(name, '.');
		size_t l = dot != NULL ? (size_t)(dot - name) : strlen(name);

		p[w++] = l;
		memcpy(p + w, name, l);
		w += l;
		name += l;
		if (*name == '.') {
			name++;
		}
	}
	p[w++] = 0;

	return w;
}

static size_t build_query(uint8_t *p, const char *type)
{
	size_t w = 12;

	memset(p, 0, 12);
	sys_put_be16(1, p + 4);	/* one question */
	w += put_name(p + w, type);
	sys_put_be16(TYPE_PTR, p + w);
	sys_put_be16(CLASS_IN | QU_BIT, p + w + 2);

	return w + 4;
}

struct found {
	char instance_full[128];	/* "<instance>.<type>" of the PTR we follow */
	char target[96];
	uint16_t port;
	bool have_ptr, have_srv, have_a;
	struct in_addr addr;
};

/* Takes the records of one packet; wanted_suffix is "." + type */
static void parse(const uint8_t *p, size_t len, const char *type, const char *instance,
		  const struct in_addr *src, struct found *f)
{
	int n_q = len >= 12 ? sys_get_be16(p + 4) : 0;
	int n_rr = len >= 12 ? sys_get_be16(p + 6) + sys_get_be16(p + 8) + sys_get_be16(p + 10) : 0;
	size_t off = 12;
	char name[128], rdname[128];

	for (int i = 0; i < n_q; i++) {
		int o = read_name(p, len, off, name, sizeof(name));

		if (o < 0) {
			return;
		}
		off = o + 4;
	}
	for (int i = 0; i < n_rr; i++) {
		uint16_t t, rdlen;
		int o = read_name(p, len, off, name, sizeof(name));

		if (o < 0 || (size_t)o + 10U > len) {
			return;
		}
		t = sys_get_be16(p + o);
		rdlen = sys_get_be16(p + o + 8);
		off = o + 10;
		if (off + rdlen > len) {
			return;
		}
		if (t == TYPE_PTR && name_equal(name, type) &&
		    read_name(p, len, off, rdname, sizeof(rdname)) >= 0) {
			size_t il = strlen(instance != NULL ? instance : "");

			if (instance == NULL ||
			    (strncmp(rdname, instance, il) == 0 && rdname[il] == '.')) {
				if (!f->have_ptr) {
					strncpy(f->instance_full, rdname, sizeof(f->instance_full) - 1);
					f->have_ptr = true;
				}
			}
		} else if (t == TYPE_SRV && f->have_ptr && rdlen >= 7 &&
			   strcmp(name, f->instance_full) == 0) {
			f->port = sys_get_be16(p + off + 4);
			if (read_name(p, len, off + 6, f->target, sizeof(f->target)) >= 0) {
				f->have_srv = true;
			}
		} else if (t == TYPE_A && rdlen == 4 && f->have_srv &&
			   name_equal(name, f->target)) {
			memcpy(&f->addr, p + off, 4);
			f->have_a = true;
		}
		off += rdlen;
	}
	/* the phone answers for itself: its own address will do without an A record */
	if (f->have_srv && !f->have_a && src != NULL) {
		f->addr = *src;
		f->have_a = true;
	}
}

int mdns_find(const char *type, const char *instance, struct mdns_service *out, int timeout_ms,
	      bool (*cancel)(void))
{
	struct sockaddr_in group = {.sin_family = AF_INET, .sin_port = htons(MDNS_PORT)};
	struct sockaddr_in local = {.sin_family = AF_INET, .sin_port = htons(MDNS_PORT)};
	struct ip_mreqn mreq = {0};
	struct found *f;
	uint8_t *pkt;
	int fd, one = 1, ret = -ETIMEDOUT;
	int64_t end = k_uptime_get() + timeout_ms, next_query = 0;

	pkt = malloc(PKT_MAX);
	f = calloc(1, sizeof(*f));
	if (pkt == NULL || f == NULL) {
		free(pkt);
		free(f);
		return -ENOMEM;
	}
	fd = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		free(pkt);
		free(f);
		return -errno;
	}
	zsock_inet_pton(AF_INET, "224.0.0.251", &group.sin_addr);
	zsock_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (zsock_bind(fd, (struct sockaddr *)&local, sizeof(local)) != 0) {
		/* without port 5353 the answers come back unicast to the port of the query */
		local.sin_port = 0;
		zsock_bind(fd, (struct sockaddr *)&local, sizeof(local));
	}
	mreq.imr_multiaddr = group.sin_addr;
	if (zsock_setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0 &&
	    errno != EALREADY) {
		printk("mdns: cannot join the group: %d\n", errno);
	}

	while (k_uptime_get() < end) {
		struct zsock_pollfd pfd = {.fd = fd, .events = ZSOCK_POLLIN};

		if (cancel != NULL && cancel()) {
			ret = -ECANCELED;
			break;
		}
		if (k_uptime_get() >= next_query) {
			size_t n = build_query(pkt, type);

			zsock_sendto(fd, pkt, n, 0, (struct sockaddr *)&group, sizeof(group));
			next_query = k_uptime_get() + QUERY_PERIOD_MS;
		}
		if (zsock_poll(&pfd, 1, 200) > 0) {
			struct sockaddr_in from;
			socklen_t fl = sizeof(from);
			ssize_t n = zsock_recvfrom(fd, pkt, PKT_MAX, 0, (struct sockaddr *)&from, &fl);

			if (n > 12) {
				parse(pkt, n, type, instance, &from.sin_addr, f);
				if (f->have_srv && f->have_a) {
					strncpy(out->instance, f->instance_full,
						sizeof(out->instance) - 1);
					out->instance[sizeof(out->instance) - 1] = '\0';
					out->port = f->port;
					out->addr = f->addr;
					ret = 0;
					break;
				}
			}
		}
	}
	zsock_close(fd);
	free(pkt);
	free(f);

	return ret;
}
