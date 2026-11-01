/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>

#include "internal.h"

LOG_MODULE_REGISTER(miracast, LOG_LEVEL_INF);

static K_MUTEX_DEFINE(lock);
static K_SEM_DEFINE(started, 0, 1);
static K_SEM_DEFINE(stopped, 0, 1);
static atomic_t running, stopping, idr_requested;
static struct miracast_receiver_config cfg;
static struct sockaddr_in peer;
static int control = -1;

struct reception {
	uint32_t ssrc, own_ssrc, highest, base, received;
	uint32_t previous_expected, previous_received;
	uint32_t jitter, last_sr;
	int64_t transit, sr_time;
	bool valid;
	struct sockaddr_in rtcp_peer;
};

static uint16_t get16(const uint8_t *p)
{
	return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t get32(const uint8_t *p)
{
	return ((uint32_t)get16(p) << 16) | get16(p + 2);
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

static int send_rtsp(void *ctx, const void *data, size_t len)
{
	const uint8_t *p = data;
	int64_t deadline = k_uptime_get() + 2000;

	ARG_UNUSED(ctx);
	while (len && !atomic_get(&stopping)) {
		ssize_t n = zsock_send(control, p, len, ZSOCK_MSG_DONTWAIT);

		if (n > 0) {
			p += n;
			len -= n;
		} else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
			if (k_uptime_get() >= deadline) {
				return -ETIMEDOUT;
			}
			struct zsock_pollfd fd = {.fd = control, .events = ZSOCK_POLLOUT};

			zsock_poll(&fd, 1, 50);
		} else {
			return n < 0 ? -errno : -EPIPE;
		}
	}
	return len ? -ECANCELED : 0;
}

static int media(void *ctx, enum miracast_codec codec, const void *data, size_t len,
		 int64_t pts, bool discontinuity)
{
	ARG_UNUSED(ctx);
	return cfg.ops.media(cfg.ops.ctx, codec, data, len, pts, discontinuity);
}

static void state(void *ctx, enum miracast_state value)
{
	ARG_UNUSED(ctx);
	LOG_INF("state %d", value);
	if (cfg.ops.state) {
		cfg.ops.state(cfg.ops.ctx, value);
	}
}

static int udp_bind(uint16_t port)
{
	struct sockaddr_in local = {.sin_family = AF_INET, .sin_port = htons(port)};
	int fd = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (fd < 0) {
		return -errno;
	}
	if (zsock_bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
		int ret = -errno;

		zsock_close(fd);
		return ret;
	}
	return fd;
}

static int connect_source(void)
{
	int ret, error;
	socklen_t error_len = sizeof(error);
	int flags = zsock_fcntl(control, ZVFS_F_GETFL, 0);

	if (flags < 0 || zsock_fcntl(control, ZVFS_F_SETFL, flags | ZVFS_O_NONBLOCK) < 0) {
		return -errno;
	}
	ret = zsock_connect(control, (struct sockaddr *)&peer, sizeof(peer));
	if (!ret) {
		return 0;
	}
	if (errno != EINPROGRESS) {
		return -errno;
	}
	int64_t deadline = k_uptime_get() + 10000;

	while (!atomic_get(&stopping) && k_uptime_get() < deadline) {
		struct zsock_pollfd fd = {.fd = control, .events = ZSOCK_POLLOUT};

		ret = zsock_poll(&fd, 1, 100);
		if (ret < 0 && errno != EINTR) {
			return -errno;
		}
		if (ret > 0) {
			if (zsock_getsockopt(control, SOL_SOCKET, SO_ERROR, &error, &error_len) < 0) {
				return -errno;
			}
			return error ? -error : 0;
		}
	}
	return atomic_get(&stopping) ? -ECANCELED : -ETIMEDOUT;
}

static void reception_rtp(struct reception *r, const uint8_t *p, size_t len,
			  const struct sockaddr_in *from, uint16_t rtcp_port)
{
	if (len < 12 || (p[0] >> 6) != 2 || (p[1] & 0x7f) != 33) {
		return;
	}
	uint32_t ssrc = get32(p + 8);
	uint16_t seq = get16(p + 2);
	int64_t transit = k_uptime_get() * 90 - get32(p + 4);

	if (!r->valid || r->ssrc != ssrc) {
		r->valid = true;
		r->ssrc = ssrc;
		r->highest = r->base = seq;
		r->received = r->previous_expected = r->previous_received = 0;
		r->jitter = r->last_sr = 0;
		r->transit = transit;
		/* SETUP may omit server_port; the RTP sender's next port is the fallback. */
		r->rtcp_peer = *from;
		r->rtcp_peer.sin_port = htons(rtcp_port ? rtcp_port : ntohs(from->sin_port) + 1);
	} else {
		int16_t delta = seq - (uint16_t)r->highest;

		if (delta <= 0) {
			return;
		}
		r->highest += delta;
	}
	r->received++;
	int64_t d = transit - r->transit;

	if (d < 0) {
		d = -d;
	}
	/* Ignore timestamp wrap when estimating interarrival jitter. */
	if (d < INT32_MAX) {
		r->jitter += (int32_t)d - (int32_t)((r->jitter + 8) >> 4);
	}
	r->transit = transit;
}

static bool reception_rtcp(struct reception *r, const uint8_t *p, size_t len,
			   const struct sockaddr_in *from)
{
	while (len >= 4 && (p[0] >> 6) == 2) {
		size_t size = ((size_t)get16(p + 2) + 1) * 4;
		size_t count = p[0] & 31;

		if (size < 4 || size > len) {
			return false;
		}
		size_t payload = size;

		if (p[0] & 0x20) {
			if (size != len || !p[size - 1] || p[size - 1] > size - 4) {
				return false;
			}
			payload -= p[size - 1];
		}
		if (p[1] == 200 && payload >= 28 + count * 24 &&
		    r->valid && get32(p + 4) == r->ssrc) {
			r->last_sr = (get32(p + 8) << 16) | (get32(p + 12) >> 16);
			r->sr_time = k_uptime_get();
			r->rtcp_peer = *from;
		} else if (p[1] == 203 && payload >= 4 + count * 4 && r->valid) {
			for (size_t i = 0; i < count; i++) {
				if (get32(p + 4 + i * 4) == r->ssrc) {
					return true;
				}
			}
		}
		p += size;
		len -= size;
	}
	return false;
}

static void report(int fd, struct reception *r)
{
	/* A compound receiver report and SDES CNAME, as required by RTP/AVP. */
	uint8_t p[52] = {0x81, 201, 0, 7};
	uint32_t expected = r->highest - r->base + 1;
	uint32_t interval = expected - r->previous_expected;
	int64_t lost = (int64_t)expected - r->received;
	int64_t interval_lost = (int64_t)interval - (r->received - r->previous_received);

	put32(p + 4, r->own_ssrc);
	put32(p + 8, r->ssrc);
	uint32_t fraction = interval && interval_lost > 0 ?
		(uint64_t)interval_lost * 256 / interval : 0;

	p[12] = fraction > 255 ? 255 : fraction;
	if (lost > 0x7fffff) {
		lost = 0x7fffff;
	}
	p[13] = lost >> 16;
	p[14] = lost >> 8;
	p[15] = lost;
	put32(p + 16, r->highest);
	put32(p + 20, r->jitter >> 4);
	put32(p + 24, r->last_sr);
	put32(p + 28, r->last_sr ? (k_uptime_get() - r->sr_time) * 65536 / 1000 : 0);
	p[32] = 0x81;
	p[33] = 202;
	p[35] = 4;
	put32(p + 36, r->own_ssrc);
	p[40] = 1;
	p[41] = 8;
	memcpy(p + 42, "miracast", 8);
	zsock_sendto(fd, p, sizeof(p), ZSOCK_MSG_DONTWAIT,
		     (struct sockaddr *)&r->rtcp_peer, sizeof(r->rtcp_peer));
	r->previous_expected = expected;
	r->previous_received = r->received;
}

static int receive(struct miracast_session *s, int rtp, int rtcp, uint8_t *buf)
{
	struct zsock_pollfd fds[] = {
		{.fd = control, .events = ZSOCK_POLLIN},
		{.fd = rtp, .events = ZSOCK_POLLIN},
		{.fd = rtcp, .events = ZSOCK_POLLIN},
	};
	struct reception reception = {.own_ssrc = sys_rand32_get()};
	int64_t last_control = k_uptime_get(), last_report = last_control, last_idr = 0;
	int64_t pending_since = 0;
	uint32_t pending_seq = 0;
	int64_t last_keepalive = last_control;

	while (!atomic_get(&stopping) && s->state != MIRACAST_CLOSED) {
		int ret = zsock_poll(fds, ARRAY_SIZE(fds), 100);

		if (ret < 0 && errno != EINTR) {
			return -errno;
		}
		for (size_t i = 0; i < ARRAY_SIZE(fds); i++) {
			if (fds[i].revents & (ZSOCK_POLLERR | ZSOCK_POLLNVAL)) {
				return -EIO;
			}
			if (!(fds[i].revents & (ZSOCK_POLLIN | ZSOCK_POLLHUP))) {
				continue;
			}
			struct sockaddr_in from;
			socklen_t from_len = sizeof(from);
			ssize_t n = i == 0 ? zsock_recv(control, buf, 2048, ZSOCK_MSG_DONTWAIT) :
				zsock_recvfrom(fds[i].fd, buf, 65536, ZSOCK_MSG_DONTWAIT,
					       (struct sockaddr *)&from, &from_len);

			if (n < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
					continue;
				}
				return -errno;
			}
			if (i == 0) {
				if (n == 0) {
					return -ECONNRESET;
				}
				last_control = k_uptime_get();
				ret = miracast_rtsp(s, buf, n);
				if (ret) {
					return ret;
				}
			} else if (from_len == sizeof(from) && from.sin_family == AF_INET &&
				   from.sin_addr.s_addr == peer.sin_addr.s_addr) {
				if (i == 1) {
					if (s->server_rtp && ntohs(from.sin_port) != s->server_rtp) {
						continue;
					}
					ret = miracast_rtp(s, buf, n);
					if (ret && ret != -EBADMSG) {
						return ret;
					}
					if (!ret && s->state == MIRACAST_PLAYING) {
						reception_rtp(&reception, buf, n, &from, s->server_rtcp);
					}
				} else {
					if (s->server_rtcp && ntohs(from.sin_port) != s->server_rtcp) {
						continue;
					}
					if (reception_rtcp(&reception, buf, n, &from)) {
						mc_state(s, MIRACAST_CLOSED);
					}
				}
			}
		}
		int64_t now = k_uptime_get();

		if (s->pending == MC_NONE) {
			pending_since = 0;
		} else if (!pending_since || s->pending_seq != pending_seq) {
			pending_since = now;
			pending_seq = s->pending_seq;
		} else if (now - pending_since > CONFIG_MIRACAST_TIMEOUT_MS) {
			return -ETIMEDOUT;
		}
		uint32_t idle = s->session[0] && s->timeout_ms > CONFIG_MIRACAST_TIMEOUT_MS * 2 ?
			s->timeout_ms : CONFIG_MIRACAST_TIMEOUT_MS * 2;

		if (now - last_control > idle) {
			return -ETIMEDOUT;
		}
		if ((s->state == MIRACAST_PLAYING || s->state == MIRACAST_PAUSED) &&
		    s->pending == MC_NONE && now - last_keepalive >=
		    (s->timeout_ms / 2 < 15000 ? s->timeout_ms / 2 : 15000)) {
			ret = miracast_keepalive(s);
			if (ret) {
				return ret;
			}
			last_keepalive = now;
		}
		if (reception.valid && now - last_report >= 1000) {
			report(rtcp, &reception);
			last_report = now;
		}
		if ((s->need_idr || atomic_get(&idr_requested)) &&
		    s->state == MIRACAST_PLAYING && s->pending == MC_NONE &&
		    now - last_idr >= 1000) {
			atomic_clear(&idr_requested);
			ret = miracast_request_idr(s);
			if (!ret) {
				s->need_idr = false;
				last_idr = now;
			}
		}
	}
	return 0;
}

static void run(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	while (true) {
		k_sem_take(&started, K_FOREVER);
		struct miracast_ops ops = {.send = send_rtsp, .media = media, .state = state};
		struct miracast_session *s = miracast_create(&cfg.stream, &ops);
		uint8_t *buf = malloc(65536);
		int ret = -ENOMEM, rtp = -1, rtcp = -1;

		state(NULL, MIRACAST_NEGOTIATING);
		if (!s || !buf) {
			goto done;
		}
		rtp = udp_bind(cfg.stream.rtp_port);
		if (rtp < 0) {
			ret = rtp;
			goto done;
		}
		rtcp = udp_bind(cfg.stream.rtp_port + 1);
		if (rtcp < 0) {
			ret = rtcp;
			goto done;
		}
		control = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (control < 0) {
			ret = -errno;
			goto done;
		}
		ret = connect_source();
		if (!ret) {
			LOG_INF("RTSP source connected");
			ret = receive(s, rtp, rtcp, buf);
			if (atomic_get(&stopping) && s->state != MIRACAST_CLOSED &&
			    s->pending == MC_NONE && s->session[0]) {
				/* Best effort TEARDOWN on local stop. */
				atomic_clear(&stopping);
				miracast_teardown(s);
				atomic_set(&stopping, 1);
			}
		}
done:
		if (ret && ret != -ECANCELED) {
			LOG_WRN("receiver stopped: %d", ret);
		}
		if (control >= 0) {
			zsock_close(control);
			control = -1;
		}
		if (rtp >= 0) {
			zsock_close(rtp);
		}
		if (rtcp >= 0) {
			zsock_close(rtcp);
		}
		if (!s || s->state != MIRACAST_CLOSED) {
			state(NULL, MIRACAST_CLOSED);
		}
		miracast_destroy(s);
		free(buf);
		k_sem_give(&stopped);
		atomic_clear(&running);
	}
}

K_THREAD_DEFINE(receiver_thread, CONFIG_MIRACAST_STACK_SIZE, run, NULL, NULL, NULL,
		CONFIG_MIRACAST_THREAD_PRIORITY, 0, 0);

int miracast_receiver_start(const struct miracast_receiver_config *config)
{
	if (!config || !config->peer || !config->ops.media || !config->stream.rtp_port ||
	    (config->stream.rtp_port & 1) || (config->stream.cea_modes & ~0x21U) ||
	    config->stream.max_video > 4 * 1024 * 1024U) {
		return -EINVAL;
	}
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons(config->rtsp_port ? config->rtsp_port : 7236),
	};

	if (zsock_inet_pton(AF_INET, config->peer, &address.sin_addr) != 1 ||
	    !address.sin_addr.s_addr || (ntohl(address.sin_addr.s_addr) & 0xf0000000U) == 0xe0000000U ||
	    address.sin_addr.s_addr == INADDR_BROADCAST) {
		return -EINVAL;
	}
	k_mutex_lock(&lock, K_FOREVER);
	if (atomic_get(&running)) {
		k_mutex_unlock(&lock);
		return -EBUSY;
	}
	k_sem_reset(&stopped);
	cfg = *config;
	cfg.peer = NULL;
	peer = address;
	atomic_clear(&stopping);
	atomic_clear(&idr_requested);
	atomic_set(&running, 1);
	k_sem_give(&started);
	k_mutex_unlock(&lock);
	return 0;
}

void miracast_receiver_stop(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	if (atomic_get(&running)) {
		atomic_set(&stopping, 1);
		k_sem_take(&stopped, K_FOREVER);
		while (atomic_get(&running)) {
			k_msleep(1);
		}
	}
	k_mutex_unlock(&lock);
}

bool miracast_receiver_running(void)
{
	return atomic_get(&running) != 0;
}

void miracast_receiver_request_idr(void)
{
	atomic_set(&idr_requested, 1);
}
