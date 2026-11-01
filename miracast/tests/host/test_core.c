/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"

struct capture {
	char sent[8192];
	size_t sent_len, bytes[3];
	unsigned count[3], discontinuities;
	uint64_t hash[3];
	int64_t pts;
	FILE *video, *audio, *timestamps;
};

static int send_msg(void *arg, const void *p, size_t n)
{
	struct capture *c = arg;
	assert(c->sent_len + n < sizeof(c->sent));
	memcpy(c->sent + c->sent_len, p, n);
	c->sent_len += n;
	c->sent[c->sent_len] = 0;
	return 0;
}

static int media(void *arg, enum miracast_codec codec, const void *p, size_t n,
		 int64_t pts, bool discontinuity)
{
	struct capture *c = arg;
	assert(codec <= MIRACAST_AAC && n);
	c->count[codec]++;
	c->bytes[codec] += n;
	c->discontinuities += discontinuity;
	c->pts = pts;
	for (size_t i = 0; i < n; i++) c->hash[codec] = c->hash[codec] * 131 + ((const uint8_t *)p)[i];
	FILE *out = codec == MIRACAST_H264 ? c->video : c->audio;
	if (out) {
		assert(fwrite(p, 1, n, out) == n);
	}
	if (c->timestamps) {
		fprintf(c->timestamps, "%d %lld\n", codec, (long long)pts);
	}
	return 0;
}

static struct miracast_session *create(struct capture *c, size_t max_video)
{
	struct miracast_config cfg = {.rtp_port = 19000, .aac = true, .max_video = max_video};
	struct miracast_ops ops = {.send = send_msg, .media = media, .ctx = c};
	struct miracast_session *s = miracast_create(&cfg, &ops);
	assert(s);
	return s;
}

static void message(struct miracast_session *s, const char *method, unsigned seq,
		    const char *extra, const char *body, bool fragmented)
{
	char msg[4096];
	int len = snprintf(msg, sizeof(msg), "%s\r\nCSeq: %u\r\n%sContent-Length: %zu\r\n\r\n%s",
			   method, seq, extra ? extra : "", body ? strlen(body) : 0,
			   body ? body : "");
	assert(len > 0 && (size_t)len < sizeof(msg));
	if (fragmented) {
		for (int i = 0; i < len; i++) {
			assert(miracast_rtsp(s, msg + i, 1) == 0);
		}
	} else {
		assert(miracast_rtsp(s, msg, len) == 0);
	}
}

static void handshake(struct miracast_session *s, struct capture *c)
{
	message(s, "OPTIONS * RTSP/1.0", 19, "Require: org.wfa.wfd1.0\r\n", NULL, true);
	assert(strstr(c->sent, "Public: org.wfa.wfd1.0"));
	assert(strstr(c->sent, "OPTIONS * RTSP/1.0\r\nCSeq: 1"));
	c->sent_len = 0;
	message(s, "RTSP/1.0 200 OK", 1, "Public: org.wfa.wfd1.0\r\n", NULL, true);
	message(s, "GET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0", 20, NULL,
		"wfd_video_formats\r\nwfd_audio_codecs\r\nwfd_client_rtp_ports\r\n"
		"wfd_content_protection\r\n", true);
	assert(strstr(c->sent, "LPCM 00000002 00, AAC 00000001 00"));
	assert(strstr(c->sent, "RTP/AVP/UDP;unicast 19000 0 mode=play"));
	assert(strstr(c->sent, "wfd_content_protection: none"));
	c->sent_len = 0;
	message(s, "SET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0", 21, NULL,
		"wfd_video_formats: 00 00 01 01 00000001 00000000 00000000 00 0000 0000 00 none none\r\n"
		"wfd_audio_codecs: LPCM 00000002 00\r\n"
		"wfd_presentation_URL: rtsp://192.168.49.2/wfd1.0/streamid=0 none\r\n", true);
	message(s, "SET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0", 22, NULL,
		"wfd_trigger_method: SETUP\r\n", true);
	assert(strstr(c->sent, "SETUP rtsp://192.168.49.2/wfd1.0/streamid=0 RTSP/1.0"));
	assert(strstr(c->sent, "client_port=19000-19001"));
	c->sent_len = 0;
	message(s, "RTSP/1.0 200 OK", 2,
		"Session: 123456;timeout=30\r\n"
		"Transport: RTP/AVP/UDP;unicast;client_port=19000-19001;server_port=20000-20001\r\n",
		NULL, true);
	assert(miracast_state(s) == MIRACAST_READY);
	assert(strstr(c->sent, "PLAY rtsp://192.168.49.2/wfd1.0/streamid=0 RTSP/1.0"));
	c->sent_len = 0;
	message(s, "RTSP/1.0 200 OK", 3, "Session: 123456\r\n", NULL, true);
	assert(miracast_state(s) == MIRACAST_PLAYING);
}

static void test_rtsp(void)
{
	struct capture c = {0};
	struct miracast_session *s = create(&c, 0);
	handshake(s, &c);
	const char pipelined[] =
		"GET_PARAMETER rtsp://x RTSP/1.0\r\nCSeq: 34\r\n\r\n"
		"GET_PARAMETER rtsp://x RTSP/1.0\r\nCSeq: 35\r\nContent-Length: 0\r\n\r\n";
	assert(miracast_rtsp(s, pipelined, sizeof(pipelined) - 1) == 0);
	assert(strstr(c.sent, "CSeq: 34") && strstr(c.sent, "CSeq: 35"));
	c.sent_len = 0;
	message(s, "SET_PARAMETER rtsp://x RTSP/1.0", 36, NULL,
		"wfd_content_protection: HDCP2.1 port=1024\r\n", false);
	assert(strstr(c.sent, "406 Not Acceptable"));
	c.sent_len = 0;
	message(s, "SET_PARAMETER rtsp://x RTSP/1.0", 37, NULL,
		"wfd_video_formats: 00 00 02 08 ffffffff 00000000 00000000 00 0000 0000 00 none none\r\n",
		false);
	assert(strstr(c.sent, "406 Not Acceptable"));
	c.sent_len = 0;
	assert(!miracast_pause(s, true));
	assert(miracast_pause(s, false) == -EAGAIN);
	message(s, "RTSP/1.0 200 OK", 4, NULL, NULL, false);
	assert(miracast_state(s) == MIRACAST_PAUSED);
	assert(!miracast_pause(s, false));
	message(s, "RTSP/1.0 200 OK", 5, NULL, NULL, false);
	assert(miracast_state(s) == MIRACAST_PLAYING);
	assert(!miracast_request_idr(s));
	assert(strstr(c.sent, "wfd_idr_request\r\n"));
	message(s, "RTSP/1.0 406 Not Acceptable", 6, NULL, NULL, false);
	c.sent_len = 0;
	message(s, "TEARDOWN rtsp://x RTSP/1.0", 38, "Session: another-session\r\n", NULL, true);
	assert(strstr(c.sent, "454 Session Not Found"));
	assert(miracast_state(s) == MIRACAST_PLAYING);
	c.sent_len = 0;
	message(s, "GET_PARAMETER rtsp://x RTSP/1.0", 39, "Session: 123456\r\n", NULL, true);
	assert(strstr(c.sent, "Session: 123456\r\n"));
	assert(!miracast_keepalive(s));
	assert(strstr(c.sent, "GET_PARAMETER rtsp://192.168.49.2/wfd1.0/streamid=0"));
	message(s, "RTSP/1.0 200 OK", 7, NULL, NULL, true);
	assert(!miracast_teardown(s));
	message(s, "RTSP/1.0 200 OK", 8, NULL, NULL, false);
	assert(miracast_state(s) == MIRACAST_CLOSED);
	miracast_destroy(s);

	const char *bad[] = {
		"OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nCSeq: 2\r\n\r\n",
		"OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nContent-Length: -1\r\n\r\n",
		"OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nContent-Length: 999999\r\n\r\n",
		"OPTIONS * RTSP/1.0\r\nCSeq: 4294967296\r\n\r\n",
		"OPTIONS * RTSP/1.0\r\nCSeq: \t \r\n\r\n",
		"OPTIONS * RTSP/1.0garbage\r\nCSeq: 1\r\n\r\n",
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		c.sent_len = 0;
		s = create(&c, 0);
		assert(miracast_rtsp(s, bad[i], strlen(bad[i])) < 0);
		miracast_destroy(s);
	}
}

static void test_setup(void)
{
	static const struct {
		const char *session, *transport;
		uint16_t rtp, rtcp;
		uint32_t timeout;
	} good[] = {
		{"android;timeout=2", "RTP/AVP/UDP;unicast;client_port=19000-19001;"
		 "server_port=20000-21000", 20000, 21000, 2000},
		{"android", "RTP/AVP;unicast;server_port=20000;client_port=19000",
		 20000, 0, 60000},
		{"android; timeout=90", "RTP/AVP/UDP; unicast; mode=\"PLAY\"", 0, 0, 90000},
	};
	static const struct { const char *session, *transport; } bad[] = {
		{"android", "RTP/AVP/TCP;unicast;interleaved=0-1"},
		{"android", "RTP/AVP/UDP;unicast;interleaved=0-1"},
		{"android", "RTP/AVP/UDP;unicast;multicast"},
		{"android", "RTP/AVP/UDP;unicast-invalid"},
		{"android", "RTP/AVP/UDP;unicast;client_port=19002-19003"},
		{"android", "RTP/AVP/UDP;unicast;client_port=19000-19002"},
		{"android", "RTP/AVP/UDP;unicast;server_port=0-1"},
		{"android", "RTP/AVP/UDP;unicast;server_port=20000-65536"},
		{"android", "RTP/AVP/UDP;unicast;server_port=20000-20000"},
		{"android", "RTP/AVP/UDP;unicast;server_port=20000;server_port=21000"},
		{"android", "RTP/AVP/UDP;unicast;server_port=4294967296"},
		{"android;timeout=0", "RTP/AVP/UDP;unicast"},
		{"android;timeout=1;timeout=2", "RTP/AVP/UDP;unicast"},
		{"android;timeout=4294967296", "RTP/AVP/UDP;unicast"},
		{"", "RTP/AVP/UDP;unicast"},
	};
	for (size_t i = 0; i < sizeof(good) / sizeof(good[0]) +
		 sizeof(bad) / sizeof(bad[0]); i++) {
		struct capture c = {0};
		struct miracast_session *s = create(&c, 0);
		bool valid = i < sizeof(good) / sizeof(good[0]);
		size_t j = valid ? i : i - sizeof(good) / sizeof(good[0]);
		char msg[1024];

		assert(miracast_keepalive(s) == -EINVAL);
		message(s, "SET_PARAMETER rtsp://x RTSP/1.0", 1, NULL,
			"wfd_video_formats: 00 00 01 01 00000001 00000000 00000000 00 0000 0000 00 none none\r\n"
			"wfd_presentation_URL: rtsp://192.168.49.2/wfd1.0/streamid=0 none\r\n"
			"wfd_client_rtp_ports: RTP/AVP/UDP;unicast 19000 0 mode=play\r\n"
			"wfd_trigger_method: SETUP\r\n", true);
		assert(strstr(c.sent, "SETUP rtsp://"));
		int len = snprintf(msg, sizeof(msg),
			"RTSP/1.0 200 OK\r\nCSeq: 1\r\nSession: %s\r\nTransport: %s\r\n\r\n",
			valid ? good[j].session : bad[j].session,
			valid ? good[j].transport : bad[j].transport);
		int ret = miracast_rtsp(s, msg, len);

		if (valid) {
			assert(!ret && miracast_state(s) == MIRACAST_READY);
			assert(s->server_rtp == good[j].rtp && s->server_rtcp == good[j].rtcp);
			assert(s->timeout_ms == good[j].timeout);
			assert(strstr(c.sent, "Session: android\r\n"));
			const char wrong_session[] = "RTSP/1.0 200 OK\r\nCSeq: 2\r\n"
				"Session: another-session\r\n\r\n";

			assert(miracast_rtsp(s, wrong_session, sizeof(wrong_session) - 1) == -EPROTO);
			assert(miracast_state(s) == MIRACAST_READY);
		} else {
			assert(ret == -EPROTO && miracast_state(s) == MIRACAST_NEGOTIATING);
			assert(!s->session[0]);
		}
		miracast_destroy(s);
	}
	struct capture c = {0};
	struct miracast_session *s = create(&c, 0);

	message(s, "SET_PARAMETER rtsp://x RTSP/1.0", 1, NULL,
		"wfd_client_rtp_ports: RTP/AVP/UDP;unicast 20000 0 mode=play\r\n", true);
	assert(strstr(c.sent, "406 Not Acceptable"));
	miracast_destroy(s);
}

static void rtp_header(uint8_t *p, unsigned seq)
{
	memset(p, 0, 12);
	p[0] = 0x80;
	p[1] = 33;
	p[2] = seq >> 8;
	p[3] = seq;
	p[11] = 1;
}

static int feed_ts(struct miracast_session *s, const uint8_t *ts, unsigned seq)
{
	uint8_t p[200];
	rtp_header(p, seq);
	memcpy(p + 12, ts, 188);
	return miracast_rtp(s, p, sizeof(p));
}

static void test_lpcm(void)
{
	/* WFD: one AU of 80 stereo frames, 16-bit / 48 kHz, signed big endian. */
	uint8_t packet[324] = {0xa0, 1, 0, 0x11, 0x12, 0x34, 0xff, 0xfe, 0x80, 0, 0x7f, 0xff};
	uint8_t pcm[320];
	assert(miracast_lpcm_decode(packet, sizeof(packet), pcm, sizeof(pcm)) == 320);
	assert(!memcmp(pcm, "\x34\x12\xfe\xff\x00\x80\xff\x7f", 8));
	assert(miracast_lpcm_decode(packet, sizeof(packet) - 1, pcm, sizeof(pcm)) == -EINVAL);
	assert(miracast_lpcm_decode(packet, sizeof(packet), pcm, 319) == -ENOSPC);
	packet[3] = 0x51;
	assert(miracast_lpcm_decode(packet, sizeof(packet), pcm, sizeof(pcm)) == -ENOTSUP);
	packet[3] = 0x11;
	assert(miracast_lpcm_decode(packet, sizeof(packet), packet, sizeof(packet)) == 320);
	assert(!memcmp(pcm, packet, sizeof(pcm)));
}

static void ts_payload(uint8_t *out, uint16_t pid, uint8_t cc, bool start,
		       const uint8_t *payload, size_t size)
{
	assert(size && size <= 182);
	memset(out, 0xff, 188);
	out[0] = 0x47;
	out[1] = (pid >> 8) | (start ? 0x40 : 0);
	out[2] = pid;
	out[3] = 0x30 | (cc & 15);
	out[4] = 183 - size;
	out[5] = 0;
	memcpy(out + 188 - size, payload, size);
}

static void test_wfd_pes(void)
{
	/* One program: H.264 PID 0x101 and WFD LPCM PID 0x102, with PSI CRCs. */
	static const uint8_t pat[] = {
		0, 0x00, 0xb0, 0x0d, 0x00, 0x01, 0xc1, 0x00, 0x00,
		0x00, 0x01, 0xe1, 0x00, 0xe8, 0xf9, 0x5e, 0x7d,
	};
	static const uint8_t pmt[] = {
		0, 0x02, 0xb0, 0x17, 0x00, 0x01, 0xc1, 0x00, 0x00,
		0xe1, 0x01, 0xf0, 0x00, 0x1b, 0xe1, 0x01, 0xf0, 0x00,
		0x83, 0xe1, 0x02, 0xf0, 0x00, 0xf2, 0xa1, 0x44, 0xe4,
	};
	struct capture c = {.audio = tmpfile()};
	struct miracast_session *s = create(&c, 0);
	uint8_t ts[188], bad[sizeof(pat)];
	unsigned seq = 0;
	assert(c.audio);
	handshake(s, &c);
	memcpy(bad, pat, sizeof(bad));
	bad[sizeof(bad) - 1] ^= 1;
	ts_payload(ts, 0, 0, true, bad, sizeof(bad));
	assert(!feed_ts(s, ts, seq++));
	assert(s->pmt_pid == 0x1fff);
	ts_payload(ts, 0, 1, true, pat, sizeof(pat));
	assert(!feed_ts(s, ts, seq++));
	ts_payload(ts, 0x100, 0, true, pmt, sizeof(pmt));
	assert(!feed_ts(s, ts, seq++));
	assert(s->audio.pid == 0x102 && s->video.pid == 0x101);
	uint8_t pes[338] = {0, 0, 1, 0xbd, 1, 0x4c, 0x80, 0x80, 5};
	uint64_t pts = 0x100003039ULL;

	pes[9] = 0x21 | ((pts >> 29) & 0x0e);
	pes[10] = pts >> 22;
	pes[11] = 1 | ((pts >> 14) & 0xfe);
	pes[12] = pts >> 7;
	pes[13] = 1 | ((pts << 1) & 0xfe);
	pes[14] = 0xa0;
	pes[15] = 1;
	pes[17] = 0x11;
	for (size_t i = 18; i < sizeof(pes); i++) pes[i] = i;
	/* The PES length and the PTS header both cross TS packet boundaries. */
	size_t pos = 0;
	unsigned cc = 0;
	while (pos < sizeof(pes)) {
		size_t n = pos == 0 ? 5 : pos == 5 ? 6 : 182;

		if (n > sizeof(pes) - pos) n = sizeof(pes) - pos;
		ts_payload(ts, 0x102, cc++, pos == 0, pes + pos, n);
		assert(!feed_ts(s, ts, seq++));
		pos += n;
	}
	assert(c.count[MIRACAST_LPCM] == 1 && c.bytes[MIRACAST_LPCM] == 324);
	assert(c.pts == (int64_t)pts);
	uint8_t actual[324], pcm[320];
	rewind(c.audio);
	assert(fread(actual, 1, sizeof(actual), c.audio) == sizeof(actual));
	assert(!memcmp(actual, pes + 14, sizeof(actual)));
	assert(miracast_lpcm_decode(actual, sizeof(actual), pcm, sizeof(pcm)) == 320);
	for (size_t i = 0; i < sizeof(pcm); i++) assert(pcm[i] == pes[18 + (i ^ 1)]);
	miracast_destroy(s);
	fclose(c.audio);
}

static void test_fragmentation(const char *input)
{
	struct capture normal = {0}, split = {0};
	struct miracast_session *s = create(&normal, 0), *t = create(&split, 0);
	handshake(s, &normal);
	handshake(t, &split);
	FILE *in = fopen(input, "rb");
	uint8_t ts[188], fragmented[188], rtp[216];
	unsigned seq = 65520, normal_seq = 65520, cc = 0;
	assert(in);
	while (fread(ts, 1, sizeof(ts), in) == sizeof(ts)) {
		assert(!feed_ts(s, ts, normal_seq++));
		unsigned pid = ((ts[1] & 31) << 8) | ts[2];
		if ((pid == 0 || pid == s->pmt_pid) && (ts[1] & 0x40)) {
			size_t pos = 4 + ((ts[3] & 0x20) ? 1 + ts[4] : 0);
			pos += 1 + ts[pos];
			size_t size = 3 + (((ts[pos + 1] & 15) << 8) | ts[pos + 2]);
			assert(size < 180);
			uint8_t first[] = {0, ts[pos], ts[pos + 1]};
			ts_payload(fragmented, pid, cc++, true, first, sizeof(first));
			assert(!feed_ts(t, fragmented, seq++));
			ts_payload(fragmented, pid, cc++, false, ts + pos + 2, size - 2);
			assert(!feed_ts(t, fragmented, seq++));
		} else {
			/* Exercise CSRC, header extension, marker bit, padding, and sequence wrap. */
			rtp_header(rtp, seq++);
			rtp[0] = 0xb1;
			rtp[1] |= 0x80;
			memset(rtp + 12, 0, 12);
			rtp[16] = 0xbe;
			rtp[17] = 0xde;
			rtp[19] = 1;
			memcpy(rtp + 24, ts, 188);
			memset(rtp + 212, 0, 4);
			rtp[215] = 4;
			assert(!miracast_rtp(t, rtp, sizeof(rtp)));
		}
	}
	assert(!miracast_flush(s) && !miracast_flush(t));
	for (int i = 0; i < 3; i++) {
		assert(normal.count[i] == split.count[i]);
		assert(normal.hash[i] == split.hash[i]);
	}
	assert(split.bytes[0] > 10000);
	fclose(in);
	miracast_destroy(s);
	miracast_destroy(t);
}

static void test_real(const char *input, const char *prefix)
{
	struct capture c = {0};
	char path[1024];
	FILE *in = fopen(input, "rb");
	assert(in);
	snprintf(path, sizeof(path), "%s.h264", prefix);
	c.video = fopen(path, "wb");
	snprintf(path, sizeof(path), "%s.aac", prefix);
	c.audio = fopen(path, "wb");
	snprintf(path, sizeof(path), "%s.pts", prefix);
	c.timestamps = fopen(path, "w");
	assert(c.video && c.audio && c.timestamps);
	struct miracast_session *s = create(&c, 0);
	handshake(s, &c);
	uint8_t ts[188];
	unsigned seq = 65520;
	while (fread(ts, 1, sizeof(ts), in) == sizeof(ts)) {
		assert(feed_ts(s, ts, seq++) == 0);
		/* A duplicated packet must not duplicate any audio/video bytes. */
		assert(feed_ts(s, ts, seq - 1) == 0);
	}
	assert(!miracast_flush(s));
	assert(c.count[MIRACAST_H264] >= 30 && c.count[MIRACAST_AAC] > 0);
	assert(c.discontinuities == 2);
	miracast_destroy(s);
	fclose(in);
	fclose(c.video);
	fclose(c.audio);
	fclose(c.timestamps);
	printf("real TS: %zu video bytes, %zu AAC bytes\n", c.bytes[0], c.bytes[2]);
}

static void test_loss(const char *input)
{
	struct capture c = {0};
	struct miracast_session *s = create(&c, 0);
	FILE *in = fopen(input, "rb");
	uint8_t ts[188], saved[188];
	assert(in);
	handshake(s, &c);
	unsigned seq = 0;
	/* Stop inside an unbounded video PES, then lose a packet. */
	while (fread(ts, 1, sizeof(ts), in) == sizeof(ts)) {
		assert(!feed_ts(s, ts, seq++));
		if (s->video.active && s->video.len > 300) {
			memcpy(saved, ts, 188);
			break;
		}
	}
	assert(s->video.active);
	unsigned count = c.count[0];
	assert(fread(ts, 1, sizeof(ts), in) == sizeof(ts));
	assert(!feed_ts(s, ts, ++seq));
	assert(!miracast_flush(s));
	assert(c.count[0] == count);
	/* A reordered old packet cannot revive the damaged PES. */
	assert(!feed_ts(s, saved, seq - 2));
	assert(!miracast_flush(s));
	assert(c.count[0] == count);
	while (fread(ts, 1, sizeof(ts), in) == sizeof(ts)) {
		assert(!feed_ts(s, ts, ++seq));
	}
	assert(!miracast_flush(s));
	assert(c.count[0] > count && c.discontinuities >= 2);
	miracast_destroy(s);
	fclose(in);

	memset(&c, 0, sizeof(c));
	s = create(&c, 100);
	handshake(s, &c);
	in = fopen(input, "rb");
	seq = 0;
	while (fread(ts, 1, sizeof(ts), in) == sizeof(ts)) {
		assert(!feed_ts(s, ts, seq++));
	}
	assert(!miracast_flush(s));
	assert(c.count[0] == 0 && c.count[2] > 0);
	fclose(in);
	miracast_destroy(s);
}

static void test_malformed(const char *input)
{
	struct capture c = {0};
	struct miracast_session *s = create(&c, 0);
	handshake(s, &c);
	FILE *in = fopen(input, "rb");
	uint8_t p[200];
	unsigned seq = 0;
	assert(in);
	while (fread(p + 12, 1, 188, in) == 188) {
		rtp_header(p, seq++);
		for (size_t n = 0; n < sizeof(p); n++) {
			(void)miracast_rtp(s, p, n);
		}
		for (size_t n = 12; n < sizeof(p); n += 13) {
			uint8_t orig = p[n];
			rtp_header(p, seq++);
			p[n] ^= 0xff;
			(void)miracast_rtp(s, p, sizeof(p));
			p[n] = orig;
		}
		(void)miracast_rtp(s, p, sizeof(p));
	}
	fclose(in);
	miracast_destroy(s);
}

static void test_rtsp_malformed(void)
{
	const char seed[] = "GET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0\r\nCSeq: 1\r\n"
		"Content-Length: 18\r\n\r\nwfd_audio_codecs\r\n";
	struct capture c = {0};
	char candidate[sizeof(seed)];

	for (size_t pos = 0; pos < sizeof(seed) - 1; pos++) {
		for (unsigned value = 0; value < 256; value += 17) {
			struct miracast_session *s = create(&c, 0);

			c.sent_len = 0;
			memcpy(candidate, seed, sizeof(seed));
			candidate[pos] = value;
			for (size_t off = 0; off < sizeof(seed) - 1; off++) {
				if (miracast_rtsp(s, candidate + off, 1) < 0) break;
			}
			miracast_destroy(s);
		}
	}
	struct miracast_session *s = create(&c, 0);
	char *huge = malloc(MC_RTSP_MAX + 100);

	assert(huge);
	memset(huge, 'x', MC_RTSP_MAX + 100);
	assert(miracast_rtsp(s, huge, MC_RTSP_MAX + 100) == -E2BIG);
	free(huge);
	miracast_destroy(s);
}

int main(int argc, char **argv)
{
	assert(argc == 3);
	test_rtsp();
	test_setup();
	test_rtsp_malformed();
	test_lpcm();
	test_wfd_pes();
	test_real(argv[1], argv[2]);
	test_fragmentation(argv[1]);
	test_loss(argv[1]);
	test_malformed(argv[1]);
	puts("Miracast core: PASS");
	return 0;
}
