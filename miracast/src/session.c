/* SPDX-License-Identifier: Apache-2.0 */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "internal.h"

struct message {
	char *method, *headers, *body;
	uint32_t cseq;
	int status;
};

static int append(char *out, size_t cap, size_t *len, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(out + *len, cap - *len, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= cap - *len) {
		return -ENOSPC;
	}
	*len += (size_t)n;
	return 0;
}

static int decimal(const char *p, size_t len, uint32_t *out)
{
	uint32_t value = 0;

	if (!len) {
		return -EINVAL;
	}
	for (size_t i = 0; i < len; i++) {
		if (p[i] < '0' || p[i] > '9' || value > (UINT32_MAX - (p[i] - '0')) / 10) {
			return -EINVAL;
		}
		value = value * 10 + p[i] - '0';
	}
	*out = value;
	return 0;
}

/* Header values and body parameters are copied, never kept in the receive buffer. */
static int field(const char *text, const char *name, char *out, size_t cap)
{
	size_t key = strlen(name);
	bool found = false;

	for (const char *p = text; *p;) {
		const char *end = strstr(p, "\r\n");
		size_t n = end ? (size_t)(end - p) : strlen(p);

		if (n > key && !strncasecmp(p, name, key) && p[key] == ':') {
			const char *v = p + key + 1;

			if (found) {
				return -EINVAL;
			}
			while (v < p + n && (*v == ' ' || *v == '\t')) {
				v++;
			}
			while (p + n > v && (p[n - 1] == ' ' || p[n - 1] == '\t')) {
				n--;
			}
			size_t size = (size_t)(p + n - v);

			if (size >= cap) {
				return -ENOSPC;
			}
			memcpy(out, v, size);
			out[size] = 0;
			found = true;
		}
		p += n;
		/* n may have lost trailing spaces. Advance using the original line end. */
		if (!end) {
			break;
		}
		p = end + 2;
	}
	return found ? 0 : -ENOENT;
}

void mc_state(struct miracast_session *s, enum miracast_state state)
{
	if (s->state == state) {
		return;
	}
	s->state = state;
	if (s->ops.state) {
		s->ops.state(s->ops.ctx, state);
	}
}

static int response(struct miracast_session *s, uint32_t seq, int status,
		    const char *extra, const char *body)
{
	char out[2048];
	size_t n = 0;
	const char *reason = status == 200 ? "OK" : status == 405 ? "Method Not Allowed" :
		status == 406 ? "Not Acceptable" : status == 454 ? "Session Not Found" :
		status == 455 ? "Method Not Valid in This State" :
		status == 501 ? "Not Implemented" : "Bad Request";
	int ret = append(out, sizeof(out), &n, "RTSP/1.0 %d %s\r\nCSeq: %u\r\n%s",
			 status, reason, (unsigned)seq, extra ? extra : "");

	if (!ret && status == 200 && s->session[0]) {
		ret = append(out, sizeof(out), &n, "Session: %s\r\n", s->session);
	}
	if (ret == 0 && body && *body) {
		ret = append(out, sizeof(out), &n,
			     "Content-Type: text/parameters\r\nContent-Length: %u\r\n\r\n%s",
			     (unsigned)strlen(body), body);
	} else if (ret == 0) {
		ret = append(out, sizeof(out), &n, "\r\n");
	}
	return ret ? ret : s->ops.send(s->ops.ctx, out, n);
}

static int request(struct miracast_session *s, enum mc_request what)
{
	static const char *const methods[] = {
		"", "OPTIONS", "SETUP", "PLAY", "PAUSE", "TEARDOWN", "SET_PARAMETER", "GET_PARAMETER"
	};
	char out[1024];
	size_t n = 0;
	int ret;

	if (s->pending != MC_NONE) {
		return -EAGAIN;
	}
	if (what != MC_OPTIONS && !s->uri[0]) {
		return -EINVAL;
	}
	s->pending_seq = ++s->cseq;
	ret = append(out, sizeof(out), &n, "%s %s RTSP/1.0\r\nCSeq: %u\r\n",
		     methods[what], what == MC_OPTIONS ? "*" : s->uri, (unsigned)s->cseq);
	if (ret == 0 && what == MC_OPTIONS) {
		ret = append(out, sizeof(out), &n, "Require: org.wfa.wfd1.0\r\n");
	} else if (ret == 0 && what == MC_SETUP) {
		ret = append(out, sizeof(out), &n,
			     "Transport: RTP/AVP/UDP;unicast;client_port=%u-%u\r\n",
			     s->config.rtp_port, s->config.rtp_port + 1);
	} else if (ret == 0) {
		ret = append(out, sizeof(out), &n, "Session: %s\r\n", s->session);
	}
	if (ret == 0 && what == MC_IDR) {
		const char body[] = "wfd_idr_request\r\n";

		ret = append(out, sizeof(out), &n,
			     "Content-Type: text/parameters\r\nContent-Length: %u\r\n\r\n%s",
			     (unsigned)(sizeof(body) - 1), body);
	} else if (ret == 0) {
		ret = append(out, sizeof(out), &n, "\r\n");
	}
	if (!ret) {
		s->pending = what;
		ret = s->ops.send(s->ops.ctx, out, n);
		if (ret) {
			s->pending = MC_NONE;
		}
	}
	return ret;
}

static bool query(const char *body, const char *name)
{
	size_t n = strlen(name);

	for (const char *p = body; *p;) {
		const char *end = strstr(p, "\r\n");
		size_t len = end ? (size_t)(end - p) : strlen(p);

		if (len == n && !memcmp(p, name, n)) {
			return true;
		}
		if (!end) {
			break;
		}
		p = end + 2;
	}
	return false;
}

static int capabilities(struct miracast_session *s, const struct message *m)
{
	char body[1536];
	size_t n = 0;
	int ret = 0;

#define CAP(name, ...) do { \
	if (!ret && query(m->body, name)) { \
		ret = append(body, sizeof(body), &n, name ": " __VA_ARGS__); \
	} \
} while (0)
	CAP("wfd_video_formats", "00 00 01 01 %08x 00000000 00000000 00 0000 0000 00 none none\r\n",
	    (unsigned)s->config.cea_modes);
	CAP("wfd_audio_codecs", "%s\r\n", s->config.aac ?
	    "LPCM 00000002 00, AAC 00000001 00" : "LPCM 00000002 00");
	CAP("wfd_client_rtp_ports", "RTP/AVP/UDP;unicast %u 0 mode=play\r\n", s->config.rtp_port);
	CAP("wfd_content_protection", "none\r\n");
	CAP("wfd_display_edid", "none\r\n");
	CAP("wfd_coupled_sink", "00 none\r\n");
	CAP("wfd_uibc_capability", "none\r\n");
	CAP("wfd_standby_resume_capability", "none\r\n");
	CAP("wfd_3d_video_formats", "none\r\n");
	CAP("wfd_connector_type", "05\r\n");
	CAP("wfd_preferred_display_mode", "none\r\n");
#undef CAP
	body[n] = 0;
	return ret ? ret : response(s, m->cseq, 200, NULL, body);
}

static bool valid_video(struct miracast_session *s, const char *value)
{
	static const uint8_t widths[] = {2, 2, 2, 2, 8, 8, 8, 2, 4, 4, 2};
	uint32_t v[11] = {0};

	for (size_t i = 0; i < sizeof(widths); i++) {
		for (size_t j = 0; j < widths[i]; j++) {
			unsigned char c = *value++;
			unsigned digit;

			if (c >= '0' && c <= '9') digit = c - '0';
			else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
			else return false;
			v[i] = (v[i] << 4) | digit;
		}
		if (*value != ' ') return false;
		while (*value == ' ') value++;
	}
	/* The negotiated stream must use a single advertised CEA timing and CBP level 3.1. */
	return !v[1] && v[2] == 1 && v[3] == 1 && v[4] && !(v[4] & (v[4] - 1)) &&
	       !(v[4] & ~s->config.cea_modes) && !v[5] && !v[6] && !strcmp(value, "none none");
}

static int set_parameters(struct miracast_session *s, const struct message *m)
{
	char value[1024], uri[MC_URI_MAX];
	bool video = s->negotiated;
	bool have_uri = false;
	enum mc_request trigger = MC_NONE;
	int ret;

	ret = field(m->body, "wfd_video_formats", value, sizeof(value));
	if (ret != -ENOENT) {
		if (ret || !valid_video(s, value)) {
			return response(s, m->cseq, 406, NULL, NULL);
		}
		video = true;
	}
	ret = field(m->body, "wfd_audio_codecs", value, sizeof(value));
	if (ret != -ENOENT && (ret || (strcmp(value, "LPCM 00000002 00") &&
	    (!s->config.aac || strcmp(value, "AAC 00000001 00"))))) {
		return response(s, m->cseq, 406, NULL, NULL);
	}
	ret = field(m->body, "wfd_content_protection", value, sizeof(value));
	if (ret != -ENOENT && (ret || strcmp(value, "none"))) {
		return response(s, m->cseq, 406, NULL, NULL);
	}
	ret = field(m->body, "wfd_client_rtp_ports", value, sizeof(value));
	if (ret != -ENOENT) {
		char ports[80];

		snprintf(ports, sizeof(ports), "RTP/AVP/UDP;unicast %u 0 mode=play",
			 s->config.rtp_port);
		if (ret || strcmp(value, ports)) {
			return response(s, m->cseq, 406, NULL, NULL);
		}
	}
	ret = field(m->body, "wfd_presentation_URL", value, sizeof(value));
	if (ret != -ENOENT) {
		if (ret) {
			return response(s, m->cseq, 400, NULL, NULL);
		}
		size_t len = strcspn(value, " \t");

		if (len < 8 || len >= sizeof(uri) || strncmp(value, "rtsp://", 7)) {
			return response(s, m->cseq, 400, NULL, NULL);
		}
		for (size_t i = 0; i < len; i++) {
			if ((unsigned char)value[i] <= 32 || (unsigned char)value[i] >= 127) {
				return response(s, m->cseq, 400, NULL, NULL);
			}
		}
		memcpy(uri, value, len);
		uri[len] = 0;
		have_uri = true;
	}
	ret = field(m->body, "wfd_trigger_method", value, sizeof(value));
	if (ret != -ENOENT) {
		if (ret) {
			return response(s, m->cseq, 400, NULL, NULL);
		}
		if (!strcmp(value, "SETUP")) {
			trigger = MC_SETUP;
		} else if (!strcmp(value, "PLAY")) {
			trigger = MC_PLAY;
		} else if (!strcmp(value, "PAUSE")) {
			trigger = MC_PAUSE;
		} else if (!strcmp(value, "TEARDOWN")) {
			trigger = MC_TEARDOWN;
		} else {
			return response(s, m->cseq, 501, NULL, NULL);
		}
		if (s->pending != MC_NONE || !video || (!have_uri && !s->uri[0]) ||
		    (trigger != MC_SETUP && !s->session[0]) ||
		    (trigger == MC_SETUP && s->session[0])) {
			return response(s, m->cseq, 455, NULL, NULL);
		}
	}
	if (have_uri) {
		strcpy(s->uri, uri);
	}
	s->negotiated = video;
	ret = response(s, m->cseq, 200, NULL, NULL);
	return ret || trigger == MC_NONE ? ret : request(s, trigger);
}

static char *trim(char *value)
{
	while (*value == ' ' || *value == '\t') {
		value++;
	}
	size_t len = strlen(value);

	while (len && (value[len - 1] == ' ' || value[len - 1] == '\t')) {
		value[--len] = 0;
	}
	return value;
}

static int ports(const char *value, uint16_t *first, uint16_t *second)
{
	const char *dash = strchr(value, '-');
	uint32_t a, b = 0;

	if (decimal(value, dash ? (size_t)(dash - value) : strlen(value), &a) ||
	    !a || a > UINT16_MAX ||
	    (dash && (decimal(dash + 1, strlen(dash + 1), &b) || !b ||
		      b > UINT16_MAX || a == b))) {
		return -EPROTO;
	}
	*first = a;
	*second = b;
	return 0;
}

static int setup_reply(struct miracast_session *s, const struct message *m)
{
	char value[512], session[sizeof(s->session)], *next;
	uint32_t timeout = 60000;
	uint16_t rtp = 0, rtcp = 0;
	bool have_timeout = false, unicast = false, client = false, server = false;

	if (field(m->headers, "Session", value, sizeof(value))) {
		return -EPROTO;
	}
	char *token = strtok_r(value, ";", &next);

	if (!token) {
		return -EPROTO;
	}
	token = trim(token);
	size_t len = strlen(token);

	if (!len || len >= sizeof(session)) {
		return -EPROTO;
	}
	for (size_t i = 0; i < len; i++) {
		if ((unsigned char)token[i] <= 32 || (unsigned char)token[i] >= 127) {
			return -EPROTO;
		}
	}
	memcpy(session, token, len + 1);
	while ((token = strtok_r(NULL, ";", &next))) {
		token = trim(token);
		if (!strncasecmp(token, "timeout=", 8)) {
			uint32_t seconds;

			if (have_timeout || decimal(token + 8, strlen(token + 8), &seconds) ||
			    !seconds || seconds > 86400) {
				return -EPROTO;
			}
			timeout = seconds * 1000;
			have_timeout = true;
		}
	}
	if (field(m->headers, "Transport", value, sizeof(value))) {
		return -EPROTO;
	}
	token = strtok_r(value, ";", &next);
	if (!token) {
		return -EPROTO;
	}
	token = trim(token);
	if (strncasecmp(token, "RTP/AVP/UDP", sizeof("RTP/AVP/UDP")) &&
	    strncasecmp(token, "RTP/AVP", sizeof("RTP/AVP"))) {
		return -EPROTO;
	}
	while ((token = strtok_r(NULL, ";", &next))) {
		token = trim(token);
		if (!strncasecmp(token, "unicast", sizeof("unicast"))) {
			if (unicast) {
				return -EPROTO;
			}
			unicast = true;
		} else if (!strncasecmp(token, "client_port=", 12)) {
			uint16_t a, b;

			if (client || ports(token + 12, &a, &b) || a != s->config.rtp_port ||
			    (b && b != s->config.rtp_port + 1)) {
				return -EPROTO;
			}
			client = true;
		} else if (!strncasecmp(token, "server_port=", 12)) {
			if (server || ports(token + 12, &rtp, &rtcp)) {
				return -EPROTO;
			}
			server = true;
		} else if (!strncasecmp(token, "multicast", sizeof("multicast")) ||
			   !strncasecmp(token, "interleaved=", 12) || strchr(token, ',')) {
			return -EPROTO;
		}
	}
	if (!unicast) {
		return -EPROTO;
	}
	strcpy(s->session, session);
	s->timeout_ms = timeout;
	s->server_rtp = rtp;
	s->server_rtcp = rtcp;
	mc_state(s, MIRACAST_READY);
	return request(s, MC_PLAY);
}

static bool session_header(struct miracast_session *s, const char *headers)
{
	char value[256];
	int ret = field(headers, "Session", value, sizeof(value));
	size_t len = strlen(s->session);

	return ret == -ENOENT || (!ret && len && strcspn(value, "; \t") == len &&
				 !strncmp(value, s->session, len));
}

static int reply(struct miracast_session *s, const struct message *m)
{
	enum mc_request what = s->pending;

	if (what == MC_NONE || m->cseq != s->pending_seq ||
	    (what != MC_SETUP && !session_header(s, m->headers))) {
		return -EPROTO;
	}
	s->pending = MC_NONE;
	if (m->status != 200) {
		/* A source may decline a late IDR request without ending playback. */
		return what == MC_IDR ? 0 : -ECONNREFUSED;
	}
	if (what == MC_SETUP) {
		return setup_reply(s, m);
	}
	if (what == MC_PLAY) {
		mc_state(s, MIRACAST_PLAYING);
	} else if (what == MC_PAUSE) {
		mc_media_reset(s);
		mc_state(s, MIRACAST_PAUSED);
	} else if (what == MC_TEARDOWN) {
		mc_state(s, MIRACAST_CLOSED);
	}
	return 0;
}

static int dispatch(struct miracast_session *s, char *text, size_t header_len)
{
	struct message m = { .body = text + header_len };
	char seq[32];
	char *line = strstr(text, "\r\n");

	if (!line) {
		return -EPROTO;
	}
	*line = 0;
	m.headers = line + 2;
	if (!strncmp(text, "RTSP/1.0 ", 9)) {
		uint32_t status;

		if (strlen(text) < 12 || decimal(text + 9, 3, &status) ||
		    (text[12] && text[12] != ' ') || status < 100 || status > 599) {
			return -EPROTO;
		}
		m.status = status;
	} else {
		char *space = strchr(text, ' ');

		char *version = space ? strrchr(space + 1, ' ') : NULL;

		if (!version || version == space + 1 || strcmp(version, " RTSP/1.0")) {
			return -EPROTO;
		}
		*space = 0;
		m.method = text;
	}
	if (field(m.headers, "CSeq", seq, sizeof(seq)) || decimal(seq, strlen(seq), &m.cseq)) {
		return -EPROTO;
	}
	if (m.status) {
		return reply(s, &m);
	}
	if (!session_header(s, m.headers)) {
		return response(s, m.cseq, 454, NULL, NULL);
	}
	if (!strcmp(m.method, "OPTIONS")) {
		int ret = response(s, m.cseq, 200,
			"Public: org.wfa.wfd1.0, OPTIONS, GET_PARAMETER, SET_PARAMETER\r\n", NULL);

		if (!ret && !s->options_sent) {
			s->options_sent = true;
			ret = request(s, MC_OPTIONS);
		}
		return ret;
	}
	if (!strcmp(m.method, "GET_PARAMETER")) {
		return capabilities(s, &m);
	}
	if (!strcmp(m.method, "SET_PARAMETER")) {
		return set_parameters(s, &m);
	}
	if (!strcmp(m.method, "TEARDOWN")) {
		int ret = response(s, m.cseq, 200, NULL, NULL);

		mc_state(s, MIRACAST_CLOSED);
		return ret;
	}
	return response(s, m.cseq, 405, "Allow: OPTIONS, GET_PARAMETER, SET_PARAMETER\r\n", NULL);
}

struct miracast_session *miracast_create(const struct miracast_config *config,
				       const struct miracast_ops *ops)
{
	struct miracast_session *s;

	if (!config || !ops || !ops->send || !ops->media || !config->rtp_port ||
	    (config->rtp_port & 1) || config->rtp_port == UINT16_MAX ||
	    (config->cea_modes & ~0x21U) || config->max_video > 4 * 1024 * 1024U) {
		return NULL;
	}
	s = calloc(1, sizeof(*s));
	if (!s) {
		return NULL;
	}
	s->config = *config;
	s->config.cea_modes = config->cea_modes ? config->cea_modes : 1;
	s->config.max_video = config->max_video ? config->max_video : 256 * 1024;
	s->timeout_ms = 60000;
	s->ops = *ops;
	s->pmt_pid = s->video.pid = s->audio.pid = 0x1fff;
	s->video.pts = s->audio.pts = -1;
	s->video.discontinuity = s->audio.discontinuity = true;
	return s;
}

void miracast_destroy(struct miracast_session *s)
{
	if (s) {
		free(s->video.data);
		free(s->audio.data);
		free(s);
	}
}

int miracast_rtsp(struct miracast_session *s, const void *data, size_t len)
{
	const char *p = data;

	if (!s || (!data && len) || s->state == MIRACAST_CLOSED) {
		return -EINVAL;
	}
	while (len || s->rx_len) {
		size_t space = MC_RTSP_MAX - s->rx_len;
		size_t copy = len < space ? len : space;

		if (copy && memchr(p, 0, copy)) {
			return -EPROTO;
		}
		if (copy) {
			memcpy(s->rx + s->rx_len, p, copy);
			s->rx_len += copy;
			p += copy;
			len -= copy;
		}
		s->rx[s->rx_len] = 0;
		char *end = strstr(s->rx, "\r\n\r\n");

		if (!end) {
			return s->rx_len == MC_RTSP_MAX ? -E2BIG : 0;
		}
		size_t header = (size_t)(end - s->rx) + 4;
		char saved = *end, value[32];
		uint32_t body_len = 0;

		*end = 0;
		int ret = field(s->rx, "Content-Length", value, sizeof(value));
		*end = saved;
		if (ret != -ENOENT && (ret || decimal(value, strlen(value), &body_len))) {
			return -EPROTO;
		}
		if (body_len > MC_RTSP_MAX - header) {
			return -E2BIG;
		}
		size_t total = header + body_len;

		if (total > s->rx_len) {
			if (!len) {
				return 0;
			}
			continue;
		}
		char tail = s->rx[total];

		s->rx[total] = 0;
		/* Separate body from headers so their field namespaces cannot overlap. */
		*end = 0;
		ret = dispatch(s, s->rx, header);
		s->rx[total] = tail;
		memmove(s->rx, s->rx + total, s->rx_len - total);
		s->rx_len -= total;
		if (ret || s->state == MIRACAST_CLOSED) {
			return ret;
		}
		if (!len && !s->rx_len) {
			break;
		}
	}
	return 0;
}

int miracast_pause(struct miracast_session *s, bool paused)
{
	if (!s || (s->state != MIRACAST_PLAYING && s->state != MIRACAST_PAUSED)) {
		return -EINVAL;
	}
	return request(s, paused ? MC_PAUSE : MC_PLAY);
}

int miracast_teardown(struct miracast_session *s)
{
	return !s || !s->session[0] ? -EINVAL : request(s, MC_TEARDOWN);
}

int miracast_request_idr(struct miracast_session *s)
{
	return !s || s->state != MIRACAST_PLAYING ? -EINVAL : request(s, MC_IDR);
}

int miracast_keepalive(struct miracast_session *s)
{
	return !s || (s->state != MIRACAST_PLAYING && s->state != MIRACAST_PAUSED) ?
		-EINVAL : request(s, MC_KEEPALIVE);
}

enum miracast_state miracast_state(const struct miracast_session *s)
{
	return s ? s->state : MIRACAST_CLOSED;
}
