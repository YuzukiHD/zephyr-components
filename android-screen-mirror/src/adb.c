/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/byteorder.h>
#include <mirror_zephyr/adb.h>
#include "tls_io.h"

#define A_CNXN	0x4e584e43U
#define A_AUTH	0x48545541U
#define A_OPEN	0x4e45504fU
#define A_OKAY	0x59414b4fU
#define A_WRTE	0x45545257U
#define A_CLSE	0x45534c43U

#define A_STLS	0x534c5453U
#define A_STLS_VERSION	0x01000000U
#define A_VERSION	0x01000001U
#define AUTH_TOKEN	1
#define AUTH_SIGNATURE	2
#define AUTH_RSAPUBLICKEY	3

#define HDR_SIZE	24
#define MAX_STREAMS	4
#define IO_TIMEOUT_MS	8000

struct adb_stream {
	struct adb_conn *conn;
	bool used;
	volatile bool closed;
	uint32_t local_id;
	uint32_t remote_id;
	adb_sink_t sink;
	void *sink_arg;
	struct ring_buf rb;
	uint8_t *rbuf;
	struct k_sem data_sem;	/* receive thread -> reader */
	struct k_sem space_sem;	/* reader -> receive thread */
	struct k_sem ack_sem;	/* OKAY of the peer for a WRTE */
	struct k_sem open_sem;	/* OKAY or CLSE of an OPEN */
};

struct adb_conn {
	int fd;
	bool tls;
	struct tls_io tls_io;
	uint32_t peer_maxdata;
	volatile bool down;
	struct k_mutex tx_lock;
	uint8_t *tx;
	uint8_t *rx;
	struct adb_stream streams[MAX_STREAMS];
	uint32_t next_id;
	struct k_thread rx_thread;
	k_tid_t rx_tid;
	K_KERNEL_STACK_MEMBER(rx_stack, 16384);
};

/* ---- socket helpers ------------------------------------------------------------------- */

static int recv_full(int fd, void *buf, size_t len, int timeout_ms)
{
	uint8_t *p = buf;

	while (len > 0U) {
		struct zsock_pollfd pfd = {.fd = fd, .events = ZSOCK_POLLIN};
		ssize_t n;

		if (timeout_ms >= 0) {
			int r = zsock_poll(&pfd, 1, timeout_ms);

			if (r == 0) {
				return -ETIMEDOUT;
			}
			if (r < 0) {
				return -errno;
			}
		}
		n = zsock_recv(fd, p, len, 0);
		if (n == 0) {
			return -ECONNRESET;
		}
		if (n < 0) {
			return -errno;
		}
		p += n;
		len -= n;
	}

	return 0;
}

static int send_full(int fd, const void *buf, size_t len)
{
	const uint8_t *p = buf;

	while (len > 0U) {
		ssize_t n = zsock_send(fd, p, len, 0);

		if (n < 0) {
			return -errno;
		}
		p += n;
		len -= n;
	}

	return 0;
}

static int conn_recv(struct adb_conn *c, void *buf, size_t len, int timeout_ms)
{
	return c->tls ? tls_io_recv(&c->tls_io, buf, len, timeout_ms)
		      : recv_full(c->fd, buf, len, timeout_ms);
}

/* One packet in one send: header and payload */
static int send_packet(struct adb_conn *c, uint32_t cmd, uint32_t arg0, uint32_t arg1,
		       const void *data, size_t len)
{
	int ret;

	k_mutex_lock(&c->tx_lock, K_FOREVER);
	sys_put_le32(cmd, c->tx);
	sys_put_le32(arg0, c->tx + 4);
	sys_put_le32(arg1, c->tx + 8);
	sys_put_le32(len, c->tx + 12);
	sys_put_le32(0, c->tx + 16);
	sys_put_le32(cmd ^ 0xffffffffU, c->tx + 20);
	if (len > 0U) {
		memcpy(c->tx + HDR_SIZE, data, len);
	}
	ret = c->tls ? tls_io_send(&c->tls_io, c->tx, HDR_SIZE + len)
		     : send_full(c->fd, c->tx, HDR_SIZE + len);
	k_mutex_unlock(&c->tx_lock);

	return ret;
}

struct hdr {
	uint32_t cmd, arg0, arg1, len;
};

static int recv_header(struct adb_conn *c, struct hdr *h, int timeout_ms)
{
	uint8_t b[HDR_SIZE];
	int ret = conn_recv(c, b, sizeof(b), timeout_ms);

	if (ret != 0) {
		return ret;
	}
	h->cmd = sys_get_le32(b);
	h->arg0 = sys_get_le32(b + 4);
	h->arg1 = sys_get_le32(b + 8);
	h->len = sys_get_le32(b + 12);
	if ((h->cmd ^ 0xffffffffU) != sys_get_le32(b + 20) ||
	    h->len > CONFIG_MIRROR_ADB_MAX_PAYLOAD) {
		return -EBADMSG;
	}

	return 0;
}

/* ---- streams -------------------------------------------------------------------------- */

static struct adb_stream *stream_by_local(struct adb_conn *c, uint32_t id)
{
	for (int i = 0; i < MAX_STREAMS; i++) {
		if (c->streams[i].used && c->streams[i].local_id == id) {
			return &c->streams[i];
		}
	}

	return NULL;
}

static void stream_mark_closed(struct adb_stream *s)
{
	s->closed = true;
	k_sem_give(&s->data_sem);
	k_sem_give(&s->space_sem);
	k_sem_give(&s->ack_sem);
	k_sem_give(&s->open_sem);
}

static void rx_main(void *a, void *b, void *d)
{
	struct adb_conn *c = a;
	struct hdr h;

	while (!c->down) {
		struct adb_stream *s;
		int ret = recv_header(c, &h, -1);

		if (ret != 0) {
			break;
		}
		if (h.len > 0U && conn_recv(c, c->rx, h.len, -1) != 0) {
			break;
		}
		if (CONFIG_MIRROR_ADB_TRACE) {
			printk("adb rx: cmd %.4s arg0 %u arg1 %u len %u\n", (char *)&h.cmd, h.arg0,
			       h.arg1, h.len);
		}
		/* arg0 is the id of the peer, arg1 ours */
		s = stream_by_local(c, h.arg1);
		if (s == NULL) {
			continue;
		}
		switch (h.cmd) {
		case A_OKAY:
			if (s->remote_id == 0U) {
				s->remote_id = h.arg0;
				k_sem_give(&s->open_sem);
			} else {
				k_sem_give(&s->ack_sem);
			}
			break;
		case A_CLSE:
			stream_mark_closed(s);
			break;
		case A_WRTE: {
			const uint8_t *p = c->rx;
			size_t left = h.len;

			if (s->sink != NULL) {
				s->sink(p, left, s->sink_arg);
			} else {
				while (left > 0U && !s->closed && !c->down) {
					uint32_t n = ring_buf_put(&s->rb, p, left);

					if (CONFIG_MIRROR_ADB_TRACE) {
						printk("adb rx: put %u of %u into stream %u (space %u, cap %u)\n",
						       n, (unsigned int)left, s->local_id,
						       ring_buf_space_get(&s->rb),
						       ring_buf_capacity_get(&s->rb));
					}

					p += n;
					left -= n;
					if (n > 0U) {
						k_sem_give(&s->data_sem);
					}
					if (left > 0U) {
						k_sem_take(&s->space_sem, K_MSEC(100));
					}
				}
			}
			send_packet(c, A_OKAY, s->local_id, s->remote_id, NULL, 0);
			break;
		}
		default:
			break;
		}
	}
	c->down = true;
	for (int i = 0; i < MAX_STREAMS; i++) {
		if (c->streams[i].used) {
			stream_mark_closed(&c->streams[i]);
		}
	}
}

int adb_open(struct adb_conn *c, const char *service, adb_sink_t sink, void *sink_arg,
	     struct adb_stream **out)
{
	struct adb_stream *s = NULL;
	int ret;

	for (int i = 0; i < MAX_STREAMS; i++) {
		if (!c->streams[i].used) {
			s = &c->streams[i];
			break;
		}
	}
	if (s == NULL) {
		return -EMFILE;
	}
	memset(s, 0, sizeof(*s));
	s->conn = c;
	s->sink = sink;
	s->sink_arg = sink_arg;
	k_sem_init(&s->data_sem, 0, 1);
	k_sem_init(&s->space_sem, 0, 1);
	k_sem_init(&s->ack_sem, 0, 1);
	k_sem_init(&s->open_sem, 0, 1);
	if (sink == NULL) {
		s->rbuf = malloc(CONFIG_MIRROR_ADB_RX_BUFFER);
		if (s->rbuf == NULL) {
			return -ENOMEM;
		}
		ring_buf_init(&s->rb, CONFIG_MIRROR_ADB_RX_BUFFER, s->rbuf);
		if (CONFIG_MIRROR_ADB_TRACE) {
			printk("adb: ring %p capacity %u space %u\n", s->rbuf,
			       ring_buf_capacity_get(&s->rb), ring_buf_space_get(&s->rb));
		}
	}
	s->local_id = ++c->next_id;
	s->used = true;

	if (CONFIG_MIRROR_ADB_TRACE) {
		printk("adb open %s as %u\n", service, s->local_id);
	}
	ret = send_packet(c, A_OPEN, s->local_id, 0, service, strlen(service) + 1U);
	if (ret == 0 && k_sem_take(&s->open_sem, K_MSEC(IO_TIMEOUT_MS)) != 0) {
		ret = -ETIMEDOUT;
	}
	if (ret == 0 && s->closed) {
		ret = -ECONNREFUSED;
	}
	if (ret != 0) {
		printk("adb: open of %s failed: %d\n", service, ret);
		s->used = false;
		free(s->rbuf);
		s->rbuf = NULL;

		return ret;
	}
	*out = s;

	return 0;
}

void adb_close(struct adb_stream *s)
{
	if (s == NULL || !s->used) {
		return;
	}
	if (!s->closed && !s->conn->down) {
		send_packet(s->conn, A_CLSE, s->local_id, s->remote_id, NULL, 0);
	}
	s->closed = true;
	k_msleep(5);
	s->used = false;
	free(s->rbuf);
	s->rbuf = NULL;
}

int adb_write(struct adb_stream *s, const void *data, size_t len)
{
	const uint8_t *p = data;
	size_t max = MIN(s->conn->peer_maxdata, CONFIG_MIRROR_ADB_MAX_PAYLOAD);

	while (len > 0U) {
		size_t n = MIN(len, max);
		int ret;

		if (s->closed) {
			return -EPIPE;
		}
		k_sem_reset(&s->ack_sem);
		ret = send_packet(s->conn, A_WRTE, s->local_id, s->remote_id, p, n);
		if (ret != 0) {
			return ret;
		}
		if (k_sem_take(&s->ack_sem, K_MSEC(IO_TIMEOUT_MS)) != 0) {
			printk("adb: no ack for a write of %u bytes on stream %u\n", (unsigned int)n,
			       s->local_id);
			return -ETIMEDOUT;
		}
		if (s->closed) {
			return -EPIPE;
		}
		p += n;
		len -= n;
	}

	return 0;
}

int adb_read(struct adb_stream *s, void *buf, size_t len, k_timeout_t timeout)
{
	uint8_t *p = buf;

	while (len > 0U) {
		uint32_t n = ring_buf_get(&s->rb, p, len);

		if (n > 0U) {
			p += n;
			len -= n;
			k_sem_give(&s->space_sem);
			continue;
		}
		if (s->closed) {
			return -ECONNRESET;
		}
		if (k_sem_take(&s->data_sem, timeout) != 0) {
			printk("adb: read timeout on stream %u, wanted %u more, %u queued\n",
			       s->local_id, (unsigned int)len, ring_buf_size_get(&s->rb));
			return -ETIMEDOUT;
		}
	}

	return 0;
}

size_t adb_available(struct adb_stream *s)
{
	return ring_buf_size_get(&s->rb);
}

bool adb_is_closed(struct adb_stream *s)
{
	return s->closed || s->conn->down;
}

/* ---- connection ----------------------------------------------------------------------- */

static int authenticate(struct adb_conn *c, int accept_timeout_ms)
{
	static const char banner[] = "host::\0";
	bool signed_sent = false, key_sent = false;
	struct hdr h;
	int ret;

	ret = send_packet(c, A_CNXN, A_VERSION, CONFIG_MIRROR_ADB_MAX_PAYLOAD, banner,
			  sizeof(banner));
	if (ret != 0) {
		return ret;
	}
	for (int round = 0; round < 6; round++) {
		/* after the key was offered the user has time to accept it */
		int timeout = key_sent ? accept_timeout_ms : IO_TIMEOUT_MS;

		ret = recv_header(c, &h, timeout);
		if (ret != 0) {
			return ret;
		}
		if (h.len > 0U) {
			ret = conn_recv(c, c->rx, h.len, IO_TIMEOUT_MS);
			if (ret != 0) {
				return ret;
			}
		}
		if (h.cmd == A_CNXN) {
			c->peer_maxdata = MIN(h.arg1, (uint32_t)CONFIG_MIRROR_ADB_MAX_PAYLOAD);
			c->rx[MIN(h.len, (uint32_t)CONFIG_MIRROR_ADB_MAX_PAYLOAD - 1U)] = '\0';
			printk("adb: connected to %s (max data %u)\n", c->rx, c->peer_maxdata);
			return 0;
		}
		if (h.cmd != A_AUTH || h.arg0 != AUTH_TOKEN) {
			return -EPROTO;
		}
		if (!signed_sent) {
			uint8_t sig[256];
			size_t sl = 0;

			ret = adb_auth_sign(c->rx, h.len, sig, &sl);
			if (ret != 0) {
				return ret;
			}
			ret = send_packet(c, A_AUTH, AUTH_SIGNATURE, 0, sig, sl);
			signed_sent = true;
		} else if (!key_sent) {
			char *pub = malloc(1024);
			size_t pl = 0;

			if (pub == NULL) {
				return -ENOMEM;
			}
			ret = adb_auth_public_key(pub, 1024, &pl);
			if (ret == 0) {
				printk("adb: accept the key on the phone\n");
				ret = send_packet(c, A_AUTH, AUTH_RSAPUBLICKEY, 0, pub, pl);
			}
			free(pub);
			key_sent = true;
		} else {
			return -EACCES;
		}
		if (ret != 0) {
			return ret;
		}
	}

	return -EPROTO;
}

static int stls_stage(struct adb_conn *c)
{
	static const char banner[] = "host::\0";
	struct hdr h;
	int ret;

	ret = send_packet(c, A_CNXN, A_VERSION, CONFIG_MIRROR_ADB_MAX_PAYLOAD, banner,
			  sizeof(banner));
	if (ret == 0) {
		ret = recv_header(c, &h, IO_TIMEOUT_MS);
	}
	if (ret != 0) {
		return ret;
	}
	if (h.cmd != A_STLS) {
		printk("adb: the phone did not ask for TLS (0x%x)\n", h.cmd);
		return -EPROTO;
	}
	ret = send_packet(c, A_STLS, A_STLS_VERSION, 0, NULL, 0);
	if (ret == 0) {
		ret = tls_io_start(&c->tls_io, c->fd, IO_TIMEOUT_MS * 2);
	}
	if (ret != 0) {
		return ret;
	}
	c->tls = true;
	/* the phone speaks first inside the tunnel, a refused key shows up as an alert here */
	ret = recv_header(c, &h, IO_TIMEOUT_MS * 2);
	if (ret != 0) {
		printk("adb: no answer in the tunnel (%d), is this client paired?\n", ret);
		return -EACCES;
	}
	if (h.len > 0U) {
		ret = conn_recv(c, c->rx, h.len, IO_TIMEOUT_MS);
		if (ret != 0) {
			return ret;
		}
	}
	if (h.cmd != A_CNXN) {
		return -EPROTO;
	}
	c->peer_maxdata = MIN(h.arg1, (uint32_t)CONFIG_MIRROR_ADB_MAX_PAYLOAD);
	c->rx[MIN(h.len, (uint32_t)CONFIG_MIRROR_ADB_MAX_PAYLOAD - 1U)] = '\0';
	printk("adb: connected over TLS to %s (max data %u)\n", c->rx, c->peer_maxdata);

	return 0;
}

static int connect_common(struct adb_conn **out, const char *host, uint16_t port,
			  const char *key_path, int accept_timeout_ms, bool use_tls)
{
	struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(port)};
	struct adb_conn *c;
	int one = 1, ret;

	ret = adb_auth_init(key_path);
	if (ret != 0) {
		return ret;
	}
	if (zsock_inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		return -EINVAL;
	}
	c = calloc(1, sizeof(*c));
	if (c == NULL) {
		return -ENOMEM;
	}
	c->tx = malloc(HDR_SIZE + CONFIG_MIRROR_ADB_MAX_PAYLOAD);
	c->rx = malloc(CONFIG_MIRROR_ADB_MAX_PAYLOAD + 1);
	if (c->tx == NULL || c->rx == NULL) {
		ret = -ENOMEM;
		goto fail;
	}
	k_mutex_init(&c->tx_lock);
	c->peer_maxdata = 4096;
	c->fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (c->fd < 0) {
		ret = -errno;
		goto fail;
	}
	zsock_setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	if (zsock_connect(c->fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		ret = -errno;
		zsock_close(c->fd);
		goto fail;
	}
	ret = use_tls ? stls_stage(c) : authenticate(c, accept_timeout_ms);
	if (ret != 0) {
		if (c->tls) {
			tls_io_free(&c->tls_io);
		}
		zsock_close(c->fd);
		goto fail;
	}
	c->rx_tid = k_thread_create(&c->rx_thread, c->rx_stack, K_KERNEL_STACK_SIZEOF(c->rx_stack),
				    rx_main, c, NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_name_set(c->rx_tid, "adb_rx");
	*out = c;

	return 0;
fail:
	free(c->tx);
	free(c->rx);
	free(c);

	return ret;
}

int adb_connect(struct adb_conn **out, const char *host, uint16_t port, const char *key_path,
		int accept_timeout_ms)
{
	return connect_common(out, host, port, key_path, accept_timeout_ms, false);
}

int adb_connect_tls(struct adb_conn **out, const char *host, uint16_t port, const char *key_path)
{
	return connect_common(out, host, port, key_path, 0, true);
}

void adb_disconnect(struct adb_conn *c)
{
	if (c == NULL) {
		return;
	}
	c->down = true;
	zsock_shutdown(c->fd, ZSOCK_SHUT_RDWR);
	k_thread_join(&c->rx_thread, K_SECONDS(5));
	if (c->tls) {
		tls_io_free(&c->tls_io);
	}
	zsock_close(c->fd);
	for (int i = 0; i < MAX_STREAMS; i++) {
		if (c->streams[i].used) {
			free(c->streams[i].rbuf);
		}
	}
	free(c->tx);
	free(c->rx);
	free(c);
}

/* ---- sync service ---------------------------------------------------------------------- */

#define SYNC_STAT	0x54415453U	/* "STAT" */
#define SYNC_SEND	0x444e4553U	/* "SEND" */
#define SYNC_DATA	0x41544144U	/* "DATA" */
#define SYNC_DONE	0x454e4f44U	/* "DONE" */
#define SYNC_OKAY	0x59414b4fU	/* "OKAY" */

static int sync_request(struct adb_stream *s, uint32_t id, const void *arg, size_t len)
{
	uint8_t *m = malloc(8 + len);
	int ret;

	if (m == NULL) {
		return -ENOMEM;
	}
	sys_put_le32(id, m);
	sys_put_le32(len, m + 4);
	memcpy(m + 8, arg, len);
	ret = adb_write(s, m, 8 + len);
	free(m);

	return ret;
}

int adb_push_file(struct adb_conn *c, const char *local, const char *remote, unsigned int mode,
		  bool *pushed)
{
	struct adb_stream *s;
	struct fs_dirent st;
	struct fs_file_t f;
	uint8_t resp[16];
	uint8_t *chunk;
	size_t chunk_max;
	char spec[160];
	int ret;

	*pushed = false;
	ret = fs_stat(local, &st);
	if (ret != 0) {
		return ret;
	}
	ret = adb_open(c, "sync:", NULL, NULL, &s);
	if (ret != 0) {
		return ret;
	}
	ret = sync_request(s, SYNC_STAT, remote, strlen(remote));
	printk("adb: STAT request: %d\n", ret);
	if (ret == 0) {
		ret = adb_read(s, resp, 16, K_MSEC(IO_TIMEOUT_MS));
		printk("adb: STAT answer: %d\n", ret);
	}
	if (ret == 0 && sys_get_le32(resp) == SYNC_STAT && sys_get_le32(resp + 4) != 0U &&
	    sys_get_le32(resp + 8) == st.size) {
		adb_close(s);

		return 0;
	}
	if (ret != 0) {
		adb_close(s);

		return ret;
	}

	chunk_max = MIN(32768U, c->peer_maxdata - 8U);
	chunk = malloc(8 + chunk_max);
	if (chunk == NULL) {
		adb_close(s);

		return -ENOMEM;
	}
	fs_file_t_init(&f);
	ret = fs_open(&f, local, FS_O_READ);
	if (ret != 0) {
		goto out;
	}
	snprintk(spec, sizeof(spec), "%s,%u", remote, mode);
	ret = sync_request(s, SYNC_SEND, spec, strlen(spec));
	printk("adb: SEND request: %d\n", ret);
	while (ret == 0) {
		ssize_t n = fs_read(&f, chunk + 8, chunk_max);

		if (n < 0) {
			ret = (int)n;
			break;
		}
		if (n == 0) {
			break;
		}
		sys_put_le32(SYNC_DATA, chunk);
		sys_put_le32(n, chunk + 4);
		ret = adb_write(s, chunk, 8 + n);
	}
	fs_close(&f);
	if (ret == 0) {
		uint8_t done[8];

		sys_put_le32(SYNC_DONE, done);
		sys_put_le32(0, done + 4);
		ret = adb_write(s, done, sizeof(done));
	}
	if (ret == 0) {
		ret = adb_read(s, resp, 8, K_MSEC(IO_TIMEOUT_MS * 2));
	}
	if (ret == 0) {
		if (sys_get_le32(resp) != SYNC_OKAY) {
			printk("adb: push of %s failed\n", remote);
			ret = -EIO;
		} else {
			*pushed = true;
		}
	}
out:
	free(chunk);
	adb_close(s);

	return ret;
}
