/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* A small HTTP/1.1 client for the cast address: GET, redirects, Content-Length or chunked body */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/fdtable.h>
#include <zephyr/sys/printk.h>
#include <dlna/dlna.h>

#include "fetch.h"

#define IO_SLICE_MS	500
#define IDLE_LIMIT_MS	15000
#define MAX_REDIRECTS	3
#define CHUNK		16384

struct conn {
	int fd;
	const atomic_t *cancel;
	uint8_t buf[1024];
	size_t pos, len;
};

struct url {
	char host[96];
	char port[8];
	char path[DLNA_URI_SIZE];
};

static int parse_url(const char *s, struct url *u)
{
	const char *host, *p;
	size_t n;

	if (strncmp(s, "http://", 7)) {
		return -ENOTSUP;
	}
	if (strchr(s, '\r') != NULL || strchr(s, '\n') != NULL) {
		return -EINVAL;
	}
	host = s + 7;
	p = host;
	while (*p && *p != '/' && *p != ':') {
		p++;
	}
	n = p - host;
	if (n == 0 || n >= sizeof(u->host)) {
		return -EINVAL;
	}
	memcpy(u->host, host, n);
	u->host[n] = '\0';
	strcpy(u->port, "80");
	if (*p == ':') {
		const char *q = ++p;

		while (*p && *p != '/') {
			p++;
		}
		if (p == q || (size_t)(p - q) >= sizeof(u->port)) {
			return -EINVAL;
		}
		memcpy(u->port, q, p - q);
		u->port[p - q] = '\0';
	}
	if (strlen(p) >= sizeof(u->path)) {
		return -ENAMETOOLONG;
	}
	strcpy(u->path, *p ? p : "/");
	return 0;
}

static int sock_open(const struct url *u, const atomic_t *cancel)
{
	struct zsock_addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
	struct zsock_addrinfo *res = NULL;
	int fd, ret;

	ret = zsock_getaddrinfo(u->host, u->port, &hints, &res);
	if (ret != 0 || !res) {
		printk("dlna: DNS failed for %s (%d)\n", u->host, ret);
		return -EHOSTUNREACH;
	}
	fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		zsock_freeaddrinfo(res);
		return -errno;
	}
	{
		int fl = zsock_fcntl(fd, ZVFS_F_GETFL, 0);
		struct zsock_pollfd pf = {.fd = fd, .events = ZSOCK_POLLOUT};
		int waited = 0;

		zsock_fcntl(fd, ZVFS_F_SETFL, fl | ZVFS_O_NONBLOCK);
		if (zsock_connect(fd, res->ai_addr, res->ai_addrlen) < 0 && errno != EINPROGRESS) {
			ret = -errno;
		} else {
			ret = -ETIMEDOUT;
			while (waited < 10000) {
				if (atomic_get(cancel)) {
					ret = -ECANCELED;
					break;
				}
				if (zsock_poll(&pf, 1, IO_SLICE_MS) > 0) {
					int err = 0;
					socklen_t len = sizeof(err);

					ret = zsock_getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 ?
						-errno : -err;
					if (ret == 0 && !(pf.revents & ZSOCK_POLLOUT)) {
						ret = -ECONNRESET;
					}
					break;
				}
				waited += IO_SLICE_MS;
			}
		}
	}
	zsock_freeaddrinfo(res);
	if (ret != 0) {
		zsock_close(fd);
		return ret;
	}
	return fd;
}

static int fill(struct conn *c)
{
	int idle = 0;

	while (idle < IDLE_LIMIT_MS) {
		struct zsock_pollfd pf = {.fd = c->fd, .events = ZSOCK_POLLIN};
		int r;

		if (atomic_get(c->cancel)) {
			return -ECANCELED;
		}
		r = zsock_poll(&pf, 1, IO_SLICE_MS);
		if (r > 0) {
			ssize_t n = zsock_recv(c->fd, c->buf, sizeof(c->buf), 0);

			if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
				continue;
			}
			if (n <= 0) {
				return n == 0 ? -ENODATA : -errno;
			}
			c->pos = 0;
			c->len = n;
			return 0;
		}
		idle += IO_SLICE_MS;
	}
	return -ETIMEDOUT;
}

/* reads a line without the line end; -ENOBUFS when it is longer than size */
static int read_line(struct conn *c, char *out, size_t size)
{
	size_t n = 0;

	for (;;) {
		if (c->pos >= c->len) {
			int r = fill(c);

			if (r < 0) {
				return r;
			}
		}
		char ch = c->buf[c->pos++];

		if (ch == '\n') {
			if (n && out[n - 1] == '\r') {
				n--;
			}
			out[n] = '\0';
			return 0;
		}
		if (n + 1 >= size) {
			return -ENOBUFS;
		}
		out[n++] = ch;
	}
}

static int skip_line(struct conn *c)
{
	for (;;) {
		if (c->pos >= c->len) {
			int r = fill(c);

			if (r < 0) {
				return r;
			}
		}
		if (c->buf[c->pos++] == '\n') {
			return 0;
		}
	}
}

static ssize_t read_some(struct conn *c, uint8_t *dst, size_t max)
{
	size_t n;

	if (c->pos >= c->len) {
		int r = fill(c);

		if (r < 0) {
			return r;
		}
	}
	n = MIN(max, c->len - c->pos);
	memcpy(dst, c->buf + c->pos, n);
	c->pos += n;
	return n;
}

static bool header_is(const char *line, const char *name)
{
	size_t n = strlen(name);

	for (size_t i = 0; i < n; i++) {
		if (tolower((unsigned char)line[i]) != tolower((unsigned char)name[i])) {
			return false;
		}
	}
	return line[n] == ':';
}

static bool has_chunked(const char *line)
{
	for (; *line; line++) {
		size_t i = 0;

		while (i < 7 && line[i] && tolower((unsigned char)line[i]) == "chunked"[i]) {
			i++;
		}
		if (i == 7 && (line[i] == '\0' || line[i] == ',' || isspace((unsigned char)line[i]))) {
			return true;
		}
	}
	return false;
}

static int write_all(struct fs_file_t *f, const uint8_t *p, size_t n)
{
	while (n) {
		ssize_t w = fs_write(f, p, n);

		if (w <= 0) {
			return -EIO;
		}
		p += w;
		n -= w;
	}
	return 0;
}

struct sink {
	struct fs_file_t *file;
	uint8_t *buf;
	size_t used;
	uint64_t written, synced, total;
	dlna_fetch_progress_fn progress;
	void *ctx;
};

static int flush(struct sink *s, bool final)
{
	int ret = write_all(s->file, s->buf, s->used);

	if (ret != 0) {
		return ret;
	}
	s->written += s->used;
	s->used = 0;
	if (final || s->written - s->synced >= 64 * 1024) {
		ret = fs_sync(s->file);
		if (ret != 0) {
			return ret;
		}
		s->synced = s->written;
		s->progress(s->ctx, s->synced, s->total);
	}
	return 0;
}

/* A clean close terminates only a body without a length. A reset is always a failure. */
static int copy_body(struct conn *c, struct sink *s, int64_t len)
{
	while (len != 0) {
		size_t want = CHUNK - s->used;
		ssize_t n;

		if (len > 0) {
			want = MIN((uint64_t)want, (uint64_t)len);
		}
		n = read_some(c, s->buf + s->used, want);
		if (n < 0) {
			return len < 0 && n == -ENODATA ? 0 : (int)n;
		}
		s->used += n;
		if (s->used == CHUNK) {
			int ret = flush(s, false);

			if (ret != 0) {
				return ret;
			}
		}
		if (len > 0) {
			len -= n;
		}
	}
	return 0;
}

static int copy_chunked(struct conn *c, struct sink *s)
{
	char line[80];

	for (;;) {
		char *end;
		unsigned long long size;
		int ret = read_line(c, line, sizeof(line));

		if (ret < 0) {
			return ret;
		}
		errno = 0;
		size = strtoull(line, &end, 16);
		if (!isxdigit((unsigned char)line[0]) || end == line ||
		    (*end && *end != ';') || errno == ERANGE || size > INT64_MAX) {
			return -EBADMSG;
		}
		if (size == 0) {
			/* Consume the trailers and their terminating blank line. */
			do {
				ret = read_line(c, line, sizeof(line));
				if (ret != 0) {
					return ret;
				}
			} while (line[0]);
			return 0;
		}
		ret = copy_body(c, s, size);
		if (ret != 0) {
			return ret;
		}
		ret = read_line(c, line, sizeof(line));
		if (ret != 0 || line[0] != '\0') {
			return ret != 0 ? ret : -EBADMSG;
		}
	}
}

static int send_all(struct conn *c, const char *p, size_t n)
{
	int idle = 0;

	while (n) {
		struct zsock_pollfd pf = {.fd = c->fd, .events = ZSOCK_POLLOUT};
		ssize_t w;

		if (atomic_get(c->cancel)) {
			return -ECANCELED;
		}
		if (zsock_poll(&pf, 1, IO_SLICE_MS) <= 0) {
			idle += IO_SLICE_MS;
			if (idle >= IDLE_LIMIT_MS) {
				return -ETIMEDOUT;
			}
			continue;
		}
		w = zsock_send(c->fd, p, n, 0);
		if (w < 0 && (errno == EAGAIN || errno == EINTR)) {
			continue;
		}
		if (w <= 0) {
			return w < 0 ? -errno : -EIO;
		}
		idle = 0;
		p += w;
		n -= w;
	}
	return 0;
}

int dlna_fetch(const char *url, const char *path, const atomic_t *cancel,
	       dlna_fetch_progress_fn progress, void *ctx)
{
	struct url *u = malloc(sizeof(*u));
	struct conn *c = malloc(sizeof(*c));
	uint8_t *tmp = malloc(CHUNK);
	size_t line_size = DLNA_URI_SIZE + 128;
	size_t req_size = sizeof(u->path) + sizeof(u->host) + 160;
	char *line = malloc(line_size), *req = malloc(req_size);
	char *loc = malloc(sizeof(u->path) + 128);
	struct fs_file_t f;
	struct sink sink = {.file = &f, .buf = tmp, .progress = progress, .ctx = ctx};
	int ret = -ENOMEM, redirects = 0;
	bool file_open = false;
	const char *stage = "allocation";

	if (!u || !c || !tmp || !line || !req || !loc) {
		goto out;
	}
	stage = "address";
	ret = parse_url(url, u);
	progress(ctx, 0, 0);

	while (ret == 0) {
		int status = 0;
		int64_t clen = -1;
		bool chunked = false;

		loc[0] = '\0';
		c->cancel = cancel;
		c->pos = c->len = 0;
		stage = "connect";
		printk("dlna: fetching %s:%s (%u-byte path)\n", u->host, u->port,
		       (unsigned)strlen(u->path));
		c->fd = sock_open(u, cancel);
		if (c->fd < 0) {
			ret = c->fd;
			break;
		}
		snprintf(req, req_size,
			 "GET %s HTTP/1.1\r\nHost: %s:%s\r\nUser-Agent: NekoBox DLNA\r\n"
			 "Accept: */*\r\nConnection: close\r\n\r\n", u->path, u->host, u->port);
		stage = "request";
		ret = send_all(c, req, strlen(req));
		if (ret == 0) {
			stage = "response";
			ret = read_line(c, line, line_size);
		}
		if (ret == 0) {
			/* "HTTP/1.1 200 OK" */
			const char *sp = strchr(line, ' ');

			status = (strncmp(line, "HTTP/", 5) == 0 && sp) ? atoi(sp + 1) : 0;
			if (status < 100) {
				ret = -EBADMSG;
			}
		}
		while (ret == 0) {
			ret = read_line(c, line, line_size);
			if (ret < 0 && ret != -ENOBUFS) {
				break;
			}
			if (ret == -ENOBUFS) {
				/* a header line that long is of no use; skip the rest of it */
				ret = skip_line(c);
				continue;
			}
			if (line[0] == '\0') {
				break;
			}
			if (header_is(line, "Content-Length")) {
				char *end;
				const char *value = line + 15;

				while (*value == ' ' || *value == '\t') {
					value++;
				}
				errno = 0;
				clen = strtoll(value, &end, 10);
				if (!isdigit((unsigned char)*value) || *end || errno == ERANGE || clen < 0) {
					ret = -EBADMSG;
				}
			} else if (header_is(line, "Transfer-Encoding") && has_chunked(line)) {
				chunked = true;
			} else if (header_is(line, "Location")) {
				const char *v = line + 9;

				while (*v == ' ') {
					v++;
				}
				snprintf(loc, sizeof(u->path) + 128, "%s", v);
			}
		}
		if (ret == 0 && (status == 301 || status == 302 || status == 303 || status == 307 ||
				 status == 308)) {
			zsock_close(c->fd);
			c->fd = -1;
			if (!loc[0] || ++redirects > MAX_REDIRECTS) {
				ret = -ELOOP;
				break;
			}
			if (loc[0] == '/') {	/* same host */
				if (strlen(loc) >= sizeof(u->path)) {
					ret = -ENAMETOOLONG;
					break;
				}
				strcpy(u->path, loc);
			} else {
				ret = parse_url(loc, u);
			}
			continue;
		}
		if (ret == 0 && status != 200) {
			printk("dlna: fetch: HTTP %d\n", status);
			ret = -EBADMSG;
		}
		if (ret == 0) {
			printk("dlna: HTTP %d, length %lld, chunked %d\n", status,
			       (long long)clen, chunked);
			stage = "file";
			fs_file_t_init(&f);
			fs_unlink(path);
			ret = fs_open(&f, path, FS_O_CREATE | FS_O_WRITE);
			if (ret == 0) {
				file_open = true;
				sink.total = !chunked && clen > 0 ? (uint64_t)clen : 0;
				ret = fs_sync(&f);
				if (ret == 0) {
					progress(ctx, 0, sink.total);
					stage = "body";
					ret = chunked ? copy_chunked(c, &sink) : copy_body(c, &sink, clen);
				}
				if (ret == 0) {
					ret = flush(&sink, true);
				}
			}
		}
		zsock_close(c->fd);
		break;
	}
out:
	if (ret < 0 && ret != -ECANCELED) {
		printk("dlna: download failed at %s (%d), %llu bytes synced\n", stage, ret,
		       (unsigned long long)sink.synced);
	}
	if (file_open) {
		int closed = fs_close(&f);

		if (ret == 0) {
			ret = closed;
		}
	}
	free(u);
	free(c);
	free(tmp);
	free(line);
	free(req);
	free(loc);
	return ret;
}
