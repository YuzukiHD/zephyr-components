/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <mbedtls/ssl.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>

#include "net/http.h"

#define RECV_SIZE	8192
#define REQ_SIZE	1536
#define USER_AGENT	"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36"

struct url {
	int tls;
	char host[96];
	char port[8];
	const char *path;
};

static char proxy_host[64];
static char proxy_port[8];

void http_set_proxy(const char *hostport)
{
	const char *colon = hostport != NULL ? strrchr(hostport, ':') : NULL;

	proxy_host[0] = '\0';
	if (colon == NULL || colon == hostport || (size_t)(colon - hostport) >= sizeof(proxy_host) ||
	    strlen(colon + 1) >= sizeof(proxy_port)) {
		return;
	}
	memcpy(proxy_host, hostport, colon - hostport);
	proxy_host[colon - hostport] = '\0';
	strcpy(proxy_port, colon + 1);
}

static int url_parse(const char *s, struct url *u)
{
	const char *p, *end;
	size_t n;

	if (strncmp(s, "https://", 8) == 0) {
		u->tls = 1;
		p = s + 8;
	} else if (strncmp(s, "http://", 7) == 0) {
		u->tls = 0;
		p = s + 7;
	} else {
		return -EINVAL;
	}
	end = p + strcspn(p, "/:");
	n = end - p;
	if (n == 0 || n >= sizeof(u->host)) {
		return -EINVAL;
	}
	memcpy(u->host, p, n);
	u->host[n] = '\0';
	if (*end == ':') {
		size_t pl = strcspn(end + 1, "/");

		if (pl == 0 || pl >= sizeof(u->port)) {
			return -EINVAL;
		}
		memcpy(u->port, end + 1, pl);
		u->port[pl] = '\0';
		end += 1 + pl;
	} else {
		strcpy(u->port, u->tls ? "443" : "80");
	}
	u->path = *end == '\0' ? "/" : end;

	return 0;
}

/* ---- connections ----------------------------------------------------------------------------
 *
 * Three kinds: plain TCP (also to a proxy), a TLS socket of Zephyr (direct https, the handshake is
 * done by connect()), and mbedTLS on a plain socket, which is what a CONNECT tunnel through a proxy
 * needs: the tunnel is set up on the plain socket first and the handshake follows.
 */
struct conn {
	int fd;
	int timeout_ms;
	bool mbed;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
};

static int tcp_connect(const char *host, const char *port, int tls_native, int verify)
{
	struct zsock_addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
	struct zsock_addrinfo *res;
	int sock, ret;

	ret = zsock_getaddrinfo(host, port, &hints, &res);
	if (ret != 0) {
		printk("http: cannot resolve %s (%d, errno %d)\n", host, ret, errno);
		return -EHOSTUNREACH;
	}
	sock = zsock_socket(res->ai_family, res->ai_socktype, tls_native ? IPPROTO_TLS_1_2 : IPPROTO_TCP);
	if (sock < 0) {
		ret = -errno;
		printk("http: socket() for %s failed (%d)\n", host, ret);
		zsock_freeaddrinfo(res);
		return ret;
	}
	if (tls_native) {
		int v = verify ? TLS_PEER_VERIFY_REQUIRED : TLS_PEER_VERIFY_NONE;

		zsock_setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY, &v, sizeof(v));
		zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME, host, strlen(host) + 1);
	}
	ret = zsock_connect(sock, res->ai_addr, res->ai_addrlen);
	if (ret < 0) {
		char ip[NET_IPV4_ADDR_LEN] = "?";

		ret = -errno;
		zsock_inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, ip, sizeof(ip));
		zsock_close(sock);
		printk("http: connect to %s (%s:%s) failed (%d)\n", host, ip, port, ret);
		zsock_freeaddrinfo(res);
		return ret;
	}
	zsock_freeaddrinfo(res);

	return sock;
}

static int fd_send_all(int sock, const void *data, size_t len)
{
	const char *p = data;

	while (len > 0) {
		ssize_t n = zsock_send(sock, p, len, 0);

		if (n < 0) {
			return -errno;
		}
		p += n;
		len -= n;
	}

	return 0;
}

/* waits for data; 0 when there is some, -ETIMEDOUT or -errno otherwise */
static int fd_wait(int fd, int timeout_ms)
{
	struct zsock_pollfd pfd = {.fd = fd, .events = ZSOCK_POLLIN};
	int ret = zsock_poll(&pfd, 1, timeout_ms);

	if (ret < 0) {
		printk("http: poll failed (%d)\n", -errno);
	}

	return ret > 0 ? 0 : (ret == 0 ? -ETIMEDOUT : -errno);
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
	struct conn *c = ctx;
	ssize_t n = zsock_send(c->fd, buf, len, 0);

	return n < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : (int)n;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
	struct conn *c = ctx;
	ssize_t n;

	if (fd_wait(c->fd, c->timeout_ms) != 0) {
		return MBEDTLS_ERR_SSL_TIMEOUT;
	}
	n = zsock_recv(c->fd, buf, len, 0);

	return n < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : (int)n;
}

static int rng(void *ctx, unsigned char *buf, size_t len)
{
	sys_rand_get(buf, len);

	return 0;
}

/* asks the proxy for a tunnel to host:port */
static int proxy_tunnel(int fd, const char *host, const char *port, int timeout_ms)
{
	char buf[256];
	size_t n = 0;
	int ret = snprintf(buf, sizeof(buf), "CONNECT %s:%s HTTP/1.1\r\nHost: %s:%s\r\n\r\n", host, port,
			   host, port);

	ret = fd_send_all(fd, buf, ret);
	while (ret == 0 && n < sizeof(buf) - 1) {
		ssize_t r;

		ret = fd_wait(fd, timeout_ms);
		if (ret != 0) {
			break;
		}
		/* the server speaks only after our hello, so nothing past the header can be read here */
		r = zsock_recv(fd, buf + n, sizeof(buf) - 1 - n, 0);
		if (r <= 0) {
			ret = -ECONNRESET;
			break;
		}
		n += r;
		buf[n] = '\0';
		if (strstr(buf, "\r\n\r\n") != NULL) {
			if (strncmp(buf, "HTTP/1.", 7) == 0 && buf[8] == ' ' && buf[9] == '2') {
				return 0;
			}
			printk("http: the proxy refused the tunnel: %.40s\n", buf);
			return -ECONNREFUSED;
		}
	}

	return ret != 0 ? ret : -EMSGSIZE;
}

static void conn_close(struct conn *c)
{
	if (c->mbed) {
		mbedtls_ssl_close_notify(&c->ssl);
		mbedtls_ssl_free(&c->ssl);
		mbedtls_ssl_config_free(&c->conf);
	}
	if (c->fd >= 0) {
		zsock_close(c->fd);
	}
}

static int conn_open(struct conn *c, const struct url *u, const struct http_opts *o)
{
	bool proxied = proxy_host[0] != '\0';
	int verify = o != NULL && o->tls_verify;
	int ret;

	memset(c, 0, sizeof(*c));
	c->timeout_ms = o != NULL && o->timeout_ms > 0 ? o->timeout_ms : 15000;
	if (!proxied) {
		c->fd = tcp_connect(u->host, u->port, u->tls, verify);
		return c->fd < 0 ? c->fd : 0;
	}
	c->fd = tcp_connect(proxy_host, proxy_port, 0, 0);
	if (c->fd < 0) {
		return c->fd;
	}
	if (!u->tls) {
		return 0;
	}
	ret = proxy_tunnel(c->fd, u->host, u->port, c->timeout_ms);
	if (ret != 0) {
		goto fail;
	}
	c->mbed = true;
	mbedtls_ssl_init(&c->ssl);
	mbedtls_ssl_config_init(&c->conf);
	ret = mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
					  MBEDTLS_SSL_PRESET_DEFAULT);
	if (ret != 0) {
		goto tls_fail;
	}
	mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_NONE);
	mbedtls_ssl_conf_rng(&c->conf, rng, NULL);
	ret = mbedtls_ssl_setup(&c->ssl, &c->conf);
	if (ret != 0) {
		goto tls_fail;
	}
	mbedtls_ssl_set_hostname(&c->ssl, u->host);
	mbedtls_ssl_set_bio(&c->ssl, c, bio_send, bio_recv, NULL);
	do {
		ret = mbedtls_ssl_handshake(&c->ssl);
	} while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);
	if (ret != 0) {
		goto tls_fail;
	}

	return 0;

tls_fail:
	printk("http: TLS through the proxy failed: -0x%x\n", -ret);
	ret = ret == MBEDTLS_ERR_SSL_TIMEOUT ? -ETIMEDOUT : -ECONNABORTED;
fail:
	conn_close(c);
	c->fd = -1;
	c->mbed = false;

	return ret;
}

static int conn_send(struct conn *c, const char *data, size_t len)
{
	if (!c->mbed) {
		return fd_send_all(c->fd, data, len);
	}
	while (len > 0) {
		int n = mbedtls_ssl_write(&c->ssl, (const unsigned char *)data, len);

		if (n < 0) {
			return -EIO;
		}
		data += n;
		len -= n;
	}

	return 0;
}

/* bytes received, 0 when the peer closed, a negative errno otherwise */
static ssize_t conn_recv(struct conn *c, uint8_t *buf, size_t size)
{
	ssize_t n;

	if (c->mbed) {
		n = mbedtls_ssl_read(&c->ssl, buf, size);
		if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
			return 0;
		}
		if (n == MBEDTLS_ERR_SSL_TIMEOUT) {
			return -ETIMEDOUT;
		}
		return n < 0 ? -ECONNRESET : n;
	}
	n = fd_wait(c->fd, c->timeout_ms);
	if (n != 0) {
		return n;
	}
	n = zsock_recv(c->fd, buf, size, 0);
	if (n < 0) {
		printk("http: recv failed (%d)\n", -errno);
	}

	return n < 0 ? -errno : n;
}

struct gate {
	http_body_fn fn;
	void *ctx;
	const struct http_parser *p;
};

/* only the body of a successful answer is of interest */
static int gate_body(void *ctx, const uint8_t *d, size_t n)
{
	struct gate *g = ctx;

	return g->p->status / 100 == 2 ? g->fn(g->ctx, d, n) : 0;
}

int http_get(const char *url, const struct http_opts *opts, http_body_fn fn, void *ctx,
	     struct http_result *res)
{
	struct url u;
	struct http_parser *p;
	struct conn *c;
	uint8_t *rx;
	char *req;
	struct gate g;
	bool via_proxy_plain;
	uint32_t t_start, t_connect = 0, t_first = 0;
	int ret;

	ret = url_parse(url, &u);
	if (ret != 0) {
		return ret;
	}
	p = malloc(sizeof(*p));
	c = malloc(sizeof(*c));
	rx = malloc(RECV_SIZE);
	req = malloc(REQ_SIZE);
	if (p == NULL || c == NULL || rx == NULL || req == NULL) {
		free(p);
		free(c);
		free(rx);
		free(req);
		return -ENOMEM;
	}
	http_parser_init(p);
	g = (struct gate){.fn = fn, .ctx = ctx, .p = p};
	t_start = k_uptime_get_32();

	ret = conn_open(c, &u, opts);
	if (ret != 0) {
		goto out;
	}
	t_connect = k_uptime_get_32() - t_start;
	via_proxy_plain = proxy_host[0] != '\0' && !u.tls;
	if (via_proxy_plain) {
		/* a proxy takes the whole address in the request line */
		ret = snprintf(req, REQ_SIZE, "GET http://%s%s%s%s HTTP/1.1\r\nHost: %s\r\n", u.host,
			       strcmp(u.port, "80") != 0 ? ":" : "", strcmp(u.port, "80") != 0 ? u.port : "",
			       u.path, u.host);
	} else {
		ret = snprintf(req, REQ_SIZE, "GET %s HTTP/1.1\r\nHost: %s\r\n", u.path, u.host);
	}
	ret += snprintf(req + ret, REQ_SIZE - ret,
			"User-Agent: " USER_AGENT "\r\n"
			"Referer: https://www.bilibili.com/\r\nAccept: */*\r\nConnection: close\r\n");
	if (opts != NULL && opts->range_start > 0) {
		ret += snprintf(req + ret, REQ_SIZE - ret, "Range: bytes=%lld-\r\n",
				(long long)opts->range_start);
	}
	if (opts != NULL && opts->headers != NULL) {
		ret += snprintf(req + ret, REQ_SIZE - ret, "%s", opts->headers);
	}
	if (ret + 2 >= REQ_SIZE) {
		ret = -EMSGSIZE;
		goto close;
	}
	strcpy(req + ret, "\r\n");
	ret = conn_send(c, req, ret + 2);
	if (ret != 0) {
		goto close;
	}

	for (;;) {
		ssize_t n = conn_recv(c, rx, RECV_SIZE);

		if (t_first == 0 && n > 0) {
			t_first = k_uptime_get_32() - t_start;
		}
		if (n < 0) {
			ret = n;
			break;
		}
		if (n == 0) {
			ret = http_parser_eof(p) == 1 ? 0 : -ECONNRESET;
			break;
		}
		ret = http_parser_feed(p, rx, n, gate_body, &g);
		if (ret == 1) {
			ret = 0;
			break;
		}
		if (ret < 0) {
			break;
		}
	}
close:
	conn_close(c);
out:
	if (res != NULL) {
		size_t len = 0;
		const char *l = strstr(p->hdr, "\r\n");

		/* the name=value part of every Set-Cookie field */
		res->set_cookie[0] = '\0';
		while (l != NULL && p->state != HP_HEADER) {
			l += 2;
			if (strncasecmp(l, "Set-Cookie:", 11) == 0) {
				const char *v = l + 11;
				size_t n;

				while (*v == ' ') {
					v++;
				}
				n = strcspn(v, ";\r\n");
				if (len + n + 3 < sizeof(res->set_cookie)) {
					len += snprintf(res->set_cookie + len, sizeof(res->set_cookie) - len, "%s%.*s",
							len ? "; " : "", (int)n, v);
				}
			}
			l = strstr(l, "\r\n");
		}
		res->status = p->status;
		res->content_length = p->content_length;
		res->range_total = p->range_total;
		res->connect_ms = t_connect;
		res->first_byte_ms = t_first;
		res->total_ms = k_uptime_get_32() - t_start;
	}
	free(p);
	free(c);
	free(rx);
	free(req);

	return ret;
}

struct text {
	char *buf;
	size_t size, len;
};

static int text_body(void *ctx, const uint8_t *d, size_t n)
{
	struct text *t = ctx;

	if (t->len + n >= t->size) {
		return -ENOSPC;
	}
	memcpy(t->buf + t->len, d, n);
	t->len += n;

	return 0;
}

int http_get_text(const char *url, const struct http_opts *opts, char *buf, size_t size,
		  struct http_result *res)
{
	struct text t = {.buf = buf, .size = size};
	struct http_result r;
	int ret = http_get(url, opts, text_body, &t, &r);

	buf[t.len] = '\0';
	if (res != NULL) {
		*res = r;
	}
	if (ret != 0) {
		return ret;
	}

	return r.status / 100 == 2 ? (int)t.len : -EIO;
}
