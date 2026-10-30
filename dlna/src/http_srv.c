/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The HTTP side of the renderer: device and service descriptions, SOAP control and event
 * subscriptions. One thread serves one connection at a time (every answer closes the connection);
 * between connections it sends the event notices when the transport state changed.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/fdtable.h>
#include <zephyr/sys/printk.h>

#include "dlna_priv.h"
#include "soap.h"
#include "upnp.h"
#include "util.h"

#define RESP_SIZE	UPNP_EVENT_SIZE
#define IO_TIMEOUT_MS	3000
#define SUB_SECONDS	1800

K_THREAD_STACK_DEFINE(http_stack, CONFIG_DLNA_STACK_SIZE);
static struct k_thread http_thread;
static volatile bool stop_req;
static bool thread_up;
static int listen_fd = -1;

struct sub {
	bool used;
	enum upnp_svc svc;
	char sid[48];
	char host[16];
	uint16_t port;
	char path[96];
	int64_t expires;
	uint32_t seq;
};

static struct sub subs[CONFIG_DLNA_MAX_SUBSCRIBERS];

/* ---- socket helpers ------------------------------------------------------------------------- */

static int send_all(int fd, const char *p, size_t n)
{
	while (n) {
		struct zsock_pollfd pf = {.fd = fd, .events = ZSOCK_POLLOUT};
		ssize_t w;

		if (zsock_poll(&pf, 1, IO_TIMEOUT_MS) <= 0) {
			return -ETIMEDOUT;
		}
		w = zsock_send(fd, p, n, 0);
		if (w <= 0) {
			return -EIO;
		}
		p += w;
		n -= w;
	}
	return 0;
}

/* reads the header block and the body (as long as Content-Length says) into buf; returns the
 * length, the header end in *body_at, or a negative errno */
static int read_request(int fd, char *buf, size_t size, size_t *body_at)
{
	size_t len = 0, need = 0;
	bool have_headers = false;

	while (len + 1 < size) {
		struct zsock_pollfd pf = {.fd = fd, .events = ZSOCK_POLLIN};
		ssize_t r;

		if (zsock_poll(&pf, 1, IO_TIMEOUT_MS) <= 0) {
			return -ETIMEDOUT;
		}
		r = zsock_recv(fd, buf + len, size - 1 - len, 0);
		if (r <= 0) {
			return have_headers && len >= need ? (int)len : -ECONNRESET;
		}
		len += r;
		buf[len] = '\0';
		if (!have_headers) {
			char *end = strstr(buf, "\r\n\r\n");

			if (!end) {
				continue;
			}
			have_headers = true;
			*body_at = end + 4 - buf;
			const char *cl = header_find(buf, "Content-Length");
			const char *expect = header_find(buf, "Expect");
			char *cl_end;
			unsigned long content_len = 0;

			if (header_find(buf, "Transfer-Encoding")) {
				return -EINVAL;
			}
			if (cl) {
				content_len = strtoul(cl, &cl_end, 10);
				while (*cl_end == ' ' || *cl_end == '\t') {
					cl_end++;
				}
				if (*cl == '-' || cl_end == cl || (*cl_end != '\r' && *cl_end != '\n')) {
					return -EINVAL;
				}
			}
			if (content_len > size - 1 - *body_at) {
				return -EMSGSIZE;
			}
			need = *body_at + content_len;
			if (len < need && expect && str_starts_ci(expect, "100-continue")) {
				send_all(fd, "HTTP/1.1 100 Continue\r\n\r\n", 25);
			}
		}
		if (have_headers && len >= need) {
			buf[need] = '\0';
			return (int)need;
		}
	}
	return -EMSGSIZE;
}

static void respond(int fd, int code, const char *reason, const char *ctype, const char *body,
		    size_t len, const char *extra)
{
	char head[384];
	int n = snprintf(head, sizeof(head),
			 "HTTP/1.1 %d %s\r\nSERVER: " DLNA_SERVER "\r\nCONNECTION: close\r\n"
			 "EXT:\r\nCONTENT-LENGTH: %u\r\n%s%s%s%s\r\n",
			 code, reason, (unsigned)len, ctype ? "CONTENT-TYPE: " : "", ctype ? ctype : "",
			 ctype ? "\r\n" : "", extra ? extra : "");

	if (send_all(fd, head, n) == 0 && len) {
		send_all(fd, body, len);
	}
}

/* ---- SOAP ----------------------------------------------------------------------------------- */

static const char *err_text(int code)
{
	switch (code) {
	case UPNP_ERR_INVALID_ACTION:
		return "Invalid Action";
	case UPNP_ERR_INVALID_ARGS:
		return "Invalid Args";
	case UPNP_ERR_NOT_AVAILABLE:
		return "Transition not available";
	case UPNP_ERR_SEEK_MODE:
		return "Seek mode not supported";
	case UPNP_ERR_SEEK_TARGET:
		return "Illegal seek target";
	case UPNP_ERR_NO_CONTENT:
		return "Not supported or invalid media";
	case UPNP_ERR_BAD_INSTANCE:
		return "Invalid InstanceID";
	default:
		return "Action Failed";
	}
}

#define ENVELOPE_HEAD "<?xml version=\"1.0\"?>\r\n<s:Envelope " \
	"xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" " \
	"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
#define ENVELOPE_TAIL "</s:Body></s:Envelope>"

static void handle_control(int fd, enum upnp_svc svc, const char *headers, const char *body,
			   char *out, size_t out_size)
{
	const char *sa = header_find(headers, "SOAPACTION");
	char action[48];
	struct sb b;
	int ret, n = 0;

	if (!sa) {
		respond(fd, 400, "Bad Request", NULL, NULL, 0, NULL);
		return;
	}
	/* "urn:...:service:AVTransport:1#Play" with or without the quotes */
	sa = strchr(sa, '#');
	if (!sa) {
		respond(fd, 400, "Bad Request", NULL, NULL, 0, NULL);
		return;
	}
	for (sa++; *sa && *sa != '"' && *sa != '\r' && *sa != '\n' && n < (int)sizeof(action) - 1;) {
		action[n++] = *sa++;
	}
	action[n] = '\0';

	sb_init(&b, out, out_size);
	sb_puts(&b, ENVELOPE_HEAD);
	ret = upnp_action(svc, action, body, &b);
	if (ret == UPNP_ERR_INVALID_ARGS && !strcmp(action, "SetAVTransportURI")) {
		printk("dlna: invalid CurrentURI in %u-byte SOAP body (limit %u decoded bytes)\n",
		       (unsigned)strlen(body), (unsigned)DLNA_URI_SIZE - 1);
	}
	if (ret || strncmp(action, "Get", 3)) {
		printk("dlna: %s %s -> %d\n", upnp_services[svc].name, action, ret);
	}
	if (ret == 0 && !b.over) {
		sb_puts(&b, ENVELOPE_TAIL);
		respond(fd, 200, "OK", "text/xml; charset=\"utf-8\"", b.buf, b.len, NULL);
		return;
	}
	if (ret == 0) {
		ret = UPNP_ERR_FAILED;
	}
	sb_init(&b, out, out_size);
	sb_printf(&b, ENVELOPE_HEAD "<s:Fault><faultcode>s:Client</faultcode>"
		      "<faultstring>UPnPError</faultstring><detail>"
		      "<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\"><errorCode>%d</errorCode>"
		      "<errorDescription>%s</errorDescription></UPnPError></detail></s:Fault>"
		      ENVELOPE_TAIL, ret, err_text(ret));
	respond(fd, 500, "Internal Server Error", "text/xml; charset=\"utf-8\"", b.buf, b.len, NULL);
}

/* ---- events --------------------------------------------------------------------------------- */

static int connect_to(const char *host, uint16_t port)
{
	struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(port)};
	struct zsock_pollfd pf;
	int fd, fl;

	if (zsock_inet_pton(AF_INET, host, &a.sin_addr) != 1) {
		return -EINVAL;
	}
	fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		return -errno;
	}
	fl = zsock_fcntl(fd, ZVFS_F_GETFL, 0);
	zsock_fcntl(fd, ZVFS_F_SETFL, fl | ZVFS_O_NONBLOCK);
	if (zsock_connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0 && errno != EINPROGRESS) {
		int err = errno;

		zsock_close(fd);
		return -err;
	}
	pf.fd = fd;
	pf.events = ZSOCK_POLLOUT;
	if (zsock_poll(&pf, 1, 1500) <= 0 || (pf.revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP))) {
		zsock_close(fd);
		return -ETIMEDOUT;
	}
	{
		int err = 0;
		socklen_t len = sizeof(err);

		if (zsock_getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err) {
			int ret = err ? -err : -errno;

			zsock_close(fd);
			return ret;
		}
	}
	return fd;
}

static void send_event(struct sub *s, const char *body, size_t len)
{
	char head[320];
	int fd = connect_to(s->host, s->port);
	int n;

	if (fd < 0) {
		printk("dlna: event callback %s:%u failed (%d)\n", s->host, s->port, fd);
		s->used = false;	/* a subscriber that cannot be reached is dropped */
		return;
	}
	n = snprintf(head, sizeof(head),
		     "NOTIFY %s HTTP/1.1\r\nHOST: %s:%u\r\nCONTENT-TYPE: text/xml; charset=\"utf-8\"\r\n"
		     "NT: upnp:event\r\nNTS: upnp:propchange\r\nSID: %s\r\nSEQ: %u\r\n"
		     "CONTENT-LENGTH: %u\r\nCONNECTION: close\r\n\r\n",
		     s->path, s->host, s->port, s->sid, (unsigned)s->seq++, (unsigned)len);
	if (send_all(fd, head, n) == 0 && send_all(fd, body, len) == 0) {
		char reply[1024];
		size_t body_at = 0;

		/* Consume the response before closing; a partial header leaves unread TCP data. */
		read_request(fd, reply, sizeof(reply), &body_at);
	}
	zsock_close(fd);
}

static void notify_service(enum upnp_svc svc, char *buf, size_t size, int only)
{
	struct sb b;

	sb_init(&b, buf, size);
	upnp_event(svc, &b);
	if (b.over) {
		printk("dlna: event too large for %s\n", upnp_services[svc].name);
		return;
	}
	for (int i = 0; i < CONFIG_DLNA_MAX_SUBSCRIBERS; i++) {
		if (subs[i].used && subs[i].svc == svc && (only < 0 || only == i)) {
			send_event(&subs[i], b.buf, b.len);
		}
	}
}

/* "<http://1.2.3.4:5/path>" -> host, port, path; false if it is not that form */
static bool parse_callback(const char *cb, struct sub *s)
{
	const char *p = strchr(cb, '<');
	const char *host, *colon, *slash, *end;
	size_t n;

	if (!p || strncmp(p + 1, "http://", 7)) {
		return false;
	}
	host = p + 8;
	end = strchr(host, '>');
	slash = strchr(host, '/');
	if (!end) {
		return false;
	}
	if (slash && slash > end) {
		slash = NULL;
	}
	colon = strchr(host, ':');
	if (colon && (colon > end || (slash && colon > slash))) {
		colon = NULL;
	}
	n = (colon ? colon : (slash ? slash : end)) - host;
	if (n == 0 || n >= sizeof(s->host)) {
		return false;
	}
	memcpy(s->host, host, n);
	s->host[n] = '\0';
	s->port = colon ? (uint16_t)atoi(colon + 1) : 80;
	if (slash) {
		n = end - slash;
		if (n >= sizeof(s->path)) {
			return false;
		}
		memcpy(s->path, slash, n);
		s->path[n] = '\0';
	} else {
		strcpy(s->path, "/");
	}
	return true;
}

static int handle_subscribe(int fd, enum upnp_svc svc, const char *headers)
{
	const char *sid = header_find(headers, "SID");
	const char *cb = header_find(headers, "CALLBACK");
	struct sub *s = NULL;
	char sidtext[48], extra[160];
	int idx = -1;

	if (sid) {
		int n = 0;

		while (sid[n] && sid[n] != '\r' && sid[n] != '\n' && n < (int)sizeof(sidtext) - 1) {
			sidtext[n] = sid[n];
			n++;
		}
		sidtext[n] = '\0';
		for (int i = 0; i < CONFIG_DLNA_MAX_SUBSCRIBERS; i++) {
			if (subs[i].used && subs[i].svc == svc && !strcmp(subs[i].sid, sidtext)) {
				s = &subs[i];
			}
		}
		if (!s) {
			respond(fd, 412, "Precondition Failed", NULL, NULL, 0, NULL);
			return -1;
		}
	} else {
		struct sub fresh = {.svc = svc};

		if (!cb || !parse_callback(cb, &fresh)) {
			respond(fd, 412, "Precondition Failed", NULL, NULL, 0, NULL);
			return -1;
		}
		for (int i = 0; i < CONFIG_DLNA_MAX_SUBSCRIBERS; i++) {
			if (!subs[i].used) {
				idx = i;
				break;
			}
		}
		if (idx < 0) {
			/* replace the one that expires first */
			idx = 0;
			for (int i = 1; i < CONFIG_DLNA_MAX_SUBSCRIBERS; i++) {
				if (subs[i].expires < subs[idx].expires) {
					idx = i;
				}
			}
		}
		s = &subs[idx];
		*s = fresh;
		snprintf(s->sid, sizeof(s->sid), "uuid:%08x-%04x-%04x-%04x-%08x%04x",
			 (unsigned)sys_rand32_get(), (unsigned)(sys_rand32_get() & 0xffff),
			 (unsigned)(sys_rand32_get() & 0xffff), (unsigned)(sys_rand32_get() & 0xffff),
			 (unsigned)sys_rand32_get(), (unsigned)(sys_rand32_get() & 0xffff));
		s->used = true;
	}
	s->expires = k_uptime_get() + SUB_SECONDS * 1000LL;
	snprintf(extra, sizeof(extra), "SID: %s\r\nTIMEOUT: Second-%d\r\n", s->sid, SUB_SECONDS);
	respond(fd, 200, "OK", NULL, NULL, 0, extra);

	/* The caller closes the response before making the first callback. */
	return sid ? -1 : idx;
}

static void handle_unsubscribe(int fd, const char *headers)
{
	const char *sid = header_find(headers, "SID");

	if (sid) {
		for (int i = 0; i < CONFIG_DLNA_MAX_SUBSCRIBERS; i++) {
			if (subs[i].used && !strncmp(subs[i].sid, sid, strlen(subs[i].sid))) {
				subs[i].used = false;
			}
		}
	}
	respond(fd, 200, "OK", NULL, NULL, 0, NULL);
}

/* ---- requests ------------------------------------------------------------------------------- */

static bool match_service(const char *path, const char *suffix, enum upnp_svc *svc)
{
	for (int i = 0; i < SVC_COUNT; i++) {
		size_t n = strlen(upnp_services[i].path);

		if (!strncmp(path, upnp_services[i].path, n) && !strcmp(path + n, suffix)) {
			*svc = i;
			return true;
		}
	}
	return false;
}

/* "METHOD SP target SP version" */
static bool split_request_line(const char *line, char *method, size_t msize, char *path, size_t psize)
{
	const char *sp = strchr(line, ' ');
	const char *sp2;
	size_t n;

	if (!sp || sp - line >= (long)msize) {
		return false;
	}
	memcpy(method, line, sp - line);
	method[sp - line] = '\0';
	sp++;
	sp2 = sp;
	while (*sp2 && *sp2 != ' ' && *sp2 != '\r' && *sp2 != '\n') {
		sp2++;
	}
	n = sp2 - sp;
	if (n == 0 || n >= psize) {
		return false;
	}
	memcpy(path, sp, n);
	path[n] = '\0';
	return true;
}

/* Returns the new subscriber index, or -1 for an ordinary response. */
static int handle_request(int fd, char *req, size_t body_at, char *out, size_t out_size)
{
	char method[16], path[96];
	enum upnp_svc svc;
	struct sb b;
	const char *headers;
	char *body = req + body_at;

	if (!split_request_line(req, method, sizeof(method), path, sizeof(path))) {
		respond(fd, 400, "Bad Request", NULL, NULL, 0, NULL);
		return -1;
	}
	if (strcmp(method, "POST")) {
		printk("dlna: %s %s\n", method, path);
	}
	headers = strchr(req, '\n');
	headers = headers ? headers + 1 : "";
	/* a query string or an absolute URI is not expected from controllers */
	if (!strcmp(method, "GET") || !strcmp(method, "HEAD")) {
		sb_init(&b, out, out_size);
		if (!strcmp(path, "/description.xml")) {
			upnp_description(&b);
		} else if (match_service(path, "/scpd.xml", &svc)) {
			upnp_scpd(svc, &b);
		} else {
			respond(fd, 404, "Not Found", NULL, NULL, 0, NULL);
			return -1;
		}
		respond(fd, 200, "OK", "text/xml; charset=\"utf-8\"", b.buf,
			strcmp(method, "HEAD") ? b.len : 0, NULL);
		return -1;
	}
	if (!strcmp(method, "POST") && match_service(path, "/control", &svc)) {
		handle_control(fd, svc, headers, body, out, out_size);
		return -1;
	}
	if (!strcmp(method, "SUBSCRIBE") && match_service(path, "/event", &svc)) {
		return handle_subscribe(fd, svc, headers);
	}
	if (!strcmp(method, "UNSUBSCRIBE") && match_service(path, "/event", &svc)) {
		handle_unsubscribe(fd, headers);
		return -1;
	}
	respond(fd, 405, "Method Not Allowed", NULL, NULL, 0, NULL);
	return -1;
}

static void serve(int fd, char *req, char *out)
{
	size_t body_at = 0;
	int len = read_request(fd, req, CONFIG_DLNA_MAX_REQUEST + 1, &body_at);

	if (len < 0) {
		printk("dlna: request read failed (%d)\n", len);
		if (len == -EMSGSIZE) {
			respond(fd, 413, "Payload Too Large", NULL, NULL, 0, NULL);
		} else if (len == -EINVAL) {
			respond(fd, 400, "Bad Request", NULL, NULL, 0, NULL);
		}
		zsock_close(fd);
		return;
	}
	int idx = handle_request(fd, req, body_at, out, RESP_SIZE);

	zsock_close(fd);
	if (idx >= 0) {
		notify_service(subs[idx].svc, out, RESP_SIZE, idx);
	}
}

static void http_main(void *a, void *b, void *c)
{
	char *req = malloc(CONFIG_DLNA_MAX_REQUEST + 1);
	char *out = malloc(RESP_SIZE);
	char last_state[24] = "", last_status[16] = "";
	static char last_uri[sizeof(upnp.uri)];
	int last_volume = -1, last_mute = -1;
	uint32_t last_duration = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	last_uri[0] = '\0';

	while (!stop_req && req && out) {
		struct zsock_pollfd pf = {.fd = listen_fd, .events = ZSOCK_POLLIN};
		const char *status = "", *state;
		const struct dlna_ops *ops = upnp.cfg->ops;
		int volume;
		uint32_t duration;
		int64_t now;

		if (zsock_poll(&pf, 1, 500) > 0 && (pf.revents & ZSOCK_POLLIN)) {
			int fd = zsock_accept(listen_fd, NULL, NULL);

			if (fd >= 0) {
				serve(fd, req, out);
			}
		}

		/* events: the state changed since the last look */
		volume = ops->get_volume ? ops->get_volume(ops->ctx) : 100;
		duration = ops->duration_ms ? ops->duration_ms(ops->ctx) : 0;
		state = upnp.cfg ? upnp_transport_state(&status) : "";
		if (strcmp(state, last_state) || strcmp(status, last_status) ||
		    strcmp(upnp.uri, last_uri) || duration != last_duration) {
			strncpy(last_state, state, sizeof(last_state) - 1);
			strncpy(last_status, status, sizeof(last_status) - 1);
			strcpy(last_uri, upnp.uri);
			last_duration = duration;
			notify_service(SVC_AVT, out, RESP_SIZE, -1);
		}
		if (volume != last_volume || (int)upnp.muted != last_mute) {
			last_volume = volume;
			last_mute = upnp.muted;
			notify_service(SVC_RCS, out, RESP_SIZE, -1);
		}
		now = k_uptime_get();
		for (int i = 0; i < CONFIG_DLNA_MAX_SUBSCRIBERS; i++) {
			if (subs[i].used && subs[i].expires < now) {
				subs[i].used = false;
			}
		}
	}
	free(req);
	free(out);
}

int http_srv_start(void)
{
	struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(CONFIG_DLNA_HTTP_PORT)};
	int on = 1;

	listen_fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_fd < 0) {
		return -errno;
	}
	zsock_setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	if (zsock_bind(listen_fd, (struct sockaddr *)&a, sizeof(a)) < 0 ||
	    zsock_listen(listen_fd, 4) < 0) {
		int e = errno;

		zsock_close(listen_fd);
		listen_fd = -1;
		return -e;
	}
	memset(subs, 0, sizeof(subs));
	stop_req = false;
	k_thread_create(&http_thread, http_stack, K_THREAD_STACK_SIZEOF(http_stack), http_main, NULL,
			NULL, NULL, CONFIG_DLNA_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&http_thread, "dlna_http");
	thread_up = true;
	return 0;
}

void http_srv_stop(void)
{
	if (!thread_up) {
		return;
	}
	stop_req = true;
	k_thread_join(&http_thread, K_SECONDS(5));
	thread_up = false;
	zsock_close(listen_fd);
	listen_fd = -1;
}
