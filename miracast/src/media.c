/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define TS_SIZE 188
#define PID_NONE 0x1fff

static uint16_t be16(const uint8_t *p)
{
	return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t be32(const uint8_t *p)
{
	return ((uint32_t)be16(p) << 16) | be16(p + 2);
}

static void pes_reset(struct mc_pes *p)
{
	p->len = p->expected = 0;
	p->active = p->cc_valid = false;
	p->damaged = false;
	p->discontinuity = true;
	p->pts = -1;
}

void mc_media_reset(struct miracast_session *s)
{
	pes_reset(&s->video);
	pes_reset(&s->audio);
	memset(&s->pat, 0, sizeof(s->pat));
	memset(&s->pmt, 0, sizeof(s->pmt));
	s->rtp_valid = false;
	s->need_idr = true;
}

/* MPEG-2 PSI CRC, including the transmitted CRC, has a zero remainder. */
static uint32_t crc32(const uint8_t *p, size_t len)
{
	uint32_t crc = UINT32_MAX;

	while (len--) {
		crc ^= (uint32_t)*p++ << 24;
		for (int bit = 0; bit < 8; bit++) {
			crc = (crc << 1) ^ ((crc & 0x80000000U) ? 0x04c11db7U : 0);
		}
	}
	return crc;
}

static void section(struct miracast_session *s, bool pat, const uint8_t *p, size_t len)
{
	if (len < 12 || (p[1] & 0xf0) != 0xb0 || !(p[5] & 1) || crc32(p, len)) {
		return;
	}
	if (pat) {
		if (p[0] != 0 || (len - 12) % 4) {
			return;
		}
		for (size_t pos = 8; pos + 4 <= len - 4; pos += 4) {
			uint16_t program = be16(p + pos);
			uint16_t pid = be16(p + pos + 2) & 0x1fff;

			if (!program || pid == PID_NONE || pid == 0) {
				continue;
			}
			if (s->pmt_pid != pid || s->program != program) {
				s->pmt_pid = pid;
				s->program = program;
				memset(&s->pmt, 0, sizeof(s->pmt));
				pes_reset(&s->video);
				pes_reset(&s->audio);
				s->video.pid = s->audio.pid = PID_NONE;
			}
			return;
		}
		return;
	}
	if (p[0] != 2 || len < 16 || be16(p + 3) != s->program || p[6] || p[7]) {
		return;
	}
	uint16_t video = PID_NONE, audio = PID_NONE;
	uint8_t type = 0;
	size_t pos = 12 + (be16(p + 10) & 0xfff);

	if (pos > len - 4) {
		return;
	}
	while (pos < len - 4) {
		if (len - 4 - pos < 5) {
			return;
		}
		uint16_t pid = be16(p + pos + 1) & 0x1fff;
		size_t next = pos + 5 + (be16(p + pos + 3) & 0xfff);

		if (next > len - 4) {
			return;
		}
		if (pid != 0 && pid != PID_NONE && pid != s->pmt_pid) {
			if (p[pos] == 0x1b && video == PID_NONE) {
				video = pid;
			} else if (audio == PID_NONE &&
				   (p[pos] == 0x83 || (p[pos] == 0x0f && s->config.aac))) {
				audio = pid;
				type = p[pos];
			}
		}
		pos = next;
	}
	if (video == audio && video != PID_NONE) {
		return;
	}
	if (s->video.pid != video) {
		pes_reset(&s->video);
		s->video.pid = video;
		s->video.type = 0x1b;
		s->need_idr = true;
	}
	if (s->audio.pid != audio || s->audio.type != type) {
		pes_reset(&s->audio);
		s->audio.pid = audio;
		s->audio.type = type;
	}
}

/* Append at most one section; return the bytes consumed, including an invalid header. */
static size_t psi_append(struct miracast_session *s, struct mc_psi *psi, bool pat,
			 const uint8_t *data, size_t len)
{
	size_t used = 0;

	while (used < len) {
		size_t target = psi->len < 3 ? 3 : psi->expected;
		size_t n = target - psi->len;

		if (n > len - used) {
			n = len - used;
		}
		memcpy(psi->data + psi->len, data + used, n);
		psi->len += n;
		used += n;
		if (psi->len < target) {
			break;
		}
		if (target == 3) {
			psi->expected = 3 + (be16(psi->data + 1) & 0xfff);
			if (psi->expected < 12 || psi->expected > sizeof(psi->data)) {
				psi->len = psi->expected = 0;
				return len;
			}
		} else {
			section(s, pat, psi->data, psi->len);
			psi->len = psi->expected = 0;
			break;
		}
	}
	return used;
}

static void psi_feed(struct miracast_session *s, struct mc_psi *psi, bool pat,
		     const uint8_t *data, size_t len, bool start, uint8_t cc, bool discontinuity)
{
	if (psi->cc_valid && !discontinuity && cc == psi->cc) {
		return;
	}
	if (discontinuity || (psi->cc_valid && cc != ((psi->cc + 1) & 15))) {
		psi->len = psi->expected = 0;
	}
	psi->cc = cc;
	psi->cc_valid = true;
	if (start) {
		if (!len || data[0] > len - 1) {
			psi->len = psi->expected = 0;
			return;
		}
		size_t pointer = *data++;

		len--;
		if (psi->len) {
			psi_append(s, psi, pat, data, pointer);
		}
		psi->len = psi->expected = 0;
		data += pointer;
		len -= pointer;
		while (len && data[0] != 0xff) {
			size_t used = psi_append(s, psi, pat, data, len);

			data += used;
			len -= used;
		}
	} else if (psi->len) {
		psi_append(s, psi, pat, data, len);
	}
}

static int emit(struct miracast_session *s, struct mc_pes *p)
{
	const uint8_t *data = p->data;
	size_t header;
	int ret;

	p->active = false;
	if (p->damaged || p->len < 9 || memcmp(data, "\0\0\1", 3) ||
	    (data[6] & 0xf0) != 0x80 || (p->expected && p->expected != p->len)) {
		goto damaged;
	}
	header = 9 + data[8];
	if (header >= p->len || p->len - header >
	    (p->type == 0x1b ? s->config.max_video : MC_AUDIO_MAX)) {
		goto damaged;
	}
	p->pts = -1;
	if (data[7] & 0x80) {
		if (data[8] < 5 || (data[9] >> 4) != ((data[7] >> 6) & 3) ||
		    !(data[9] & data[11] & data[13] & 1)) {
			goto damaged;
		}
		p->pts = ((int64_t)(data[9] & 0x0e) << 29) | ((int64_t)data[10] << 22) |
			 ((int64_t)(data[11] & 0xfe) << 14) | ((uint32_t)data[12] << 7) |
			 (data[13] >> 1);
		if ((data[7] & 0x40) && (data[8] < 10 || (data[14] >> 4) != 1 ||
		    !(data[14] & data[16] & data[18] & 1))) {
			goto damaged;
		}
	} else if (data[7] & 0x40) {
		goto damaged;
	}
	if ((p->type == 0x1b && (data[3] & 0xf0) != 0xe0) ||
	    (p->type == 0x83 && data[3] != 0xbd) ||
	    (p->type == 0x0f && (data[3] & 0xe0) != 0xc0)) {
		goto damaged;
	}
	ret = s->ops.media(s->ops.ctx, p->type == 0x1b ? MIRACAST_H264 :
			   p->type == 0x83 ? MIRACAST_LPCM : MIRACAST_AAC,
			   data + header, p->len - header, p->pts, p->discontinuity);
	p->discontinuity = ret != 0;
	if (ret && p->type == 0x1b) {
		s->need_idr = true;
	}
	return ret == -EAGAIN || ret == -ENOBUFS ? 0 : ret;

damaged:
	p->discontinuity = true;
	if (p->type == 0x1b) {
		s->need_idr = true;
	}
	return 0;
}

static int pes_feed(struct miracast_session *s, struct mc_pes *p, const uint8_t *data,
		    size_t len, bool start, uint8_t cc, bool discontinuity)
{
	int ret;

	if (p->cc_valid && !discontinuity && cc == p->cc) {
		return 0;
	}
	if (discontinuity || (p->cc_valid && cc != ((p->cc + 1) & 15))) {
		pes_reset(p);
		if (p->type == 0x1b) {
			s->need_idr = true;
		}
	}
	p->cc = cc;
	p->cc_valid = true;
	if (start) {
		if (p->active && (ret = emit(s, p)) != 0) {
			return ret;
		}
		p->active = true;
		p->damaged = false;
		p->len = p->expected = 0;
	}
	if (!p->active || p->damaged) {
		return 0;
	}
	size_t limit = (p->type == 0x1b ? s->config.max_video : MC_AUDIO_MAX) + 264;
	size_t copy = len;

	if (!p->expected && p->len < 6) {
		copy = 6 - p->len;
		if (copy > len) {
			copy = len;
		}
	} else if (p->expected && copy > p->expected - p->len) {
		copy = p->expected - p->len;
	}
	if (copy > limit - p->len) {
		p->damaged = p->discontinuity = true;
		s->need_idr |= p->type == 0x1b;
		return 0;
	}
	if (p->len + copy > p->cap) {
		size_t cap = p->cap ? p->cap * 2 : 4096;

		if (cap < p->len + copy) {
			cap = p->len + copy;
		}
		if (cap > limit) {
			cap = limit;
		}
		uint8_t *buf = realloc(p->data, cap);

		if (!buf) {
			return -ENOMEM;
		}
		p->data = buf;
		p->cap = cap;
	}
	memcpy(p->data + p->len, data, copy);
	p->len += copy;
	data += copy;
	len -= copy;
	if (p->len == 6 && !p->expected) {
		if (memcmp(p->data, "\0\0\1", 3)) {
			p->damaged = p->discontinuity = true;
			return 0;
		}
		p->expected = be16(p->data + 4);
		if (p->expected) {
			p->expected += 6;
			if (p->expected > limit || p->expected < 9) {
				p->damaged = p->discontinuity = true;
				return 0;
			}
		}
	}
	if (p->expected && p->len == p->expected) {
		return emit(s, p);
	}
	/* The first six bytes may straddle packets; append this packet's remainder once. */
	if (len) {
		/* Preserve continuity while appending bytes from this same TS packet. */
		p->cc_valid = false;
		return pes_feed(s, p, data, len, false, cc, false);
	}
	return 0;
}

static int transport_stream(struct miracast_session *s, const uint8_t *data)
{
	uint16_t pid = be16(data + 1) & 0x1fff;
	bool start = (data[1] & 0x40) != 0;
	bool discontinuity = false;
	uint8_t cc = data[3] & 15;
	unsigned afc = (data[3] >> 4) & 3;
	size_t pos = 4;

	if (data[0] != 0x47 || (data[1] & 0x80) || (data[3] & 0xc0) || !afc) {
		mc_media_reset(s);
		return -EBADMSG;
	}
	if (afc & 2) {
		size_t adapt = data[pos++];

		if (adapt > TS_SIZE - pos) {
			mc_media_reset(s);
			return -EBADMSG;
		}
		discontinuity = adapt && (data[pos] & 0x80);
		pos += adapt;
	}
	if (!(afc & 1)) {
		if (discontinuity) {
			if (pid == 0) {
				memset(&s->pat, 0, sizeof(s->pat));
			} else if (pid == s->pmt_pid) {
				memset(&s->pmt, 0, sizeof(s->pmt));
			} else if (pid == s->video.pid) {
				pes_reset(&s->video);
				s->need_idr = true;
			} else if (pid == s->audio.pid) {
				pes_reset(&s->audio);
			}
		}
		return 0;
	}
	if (pos >= TS_SIZE) {
		mc_media_reset(s);
		return -EBADMSG;
	}
	if (pid == 0) {
		psi_feed(s, &s->pat, true, data + pos, TS_SIZE - pos, start, cc, discontinuity);
	} else if (pid == s->pmt_pid) {
		psi_feed(s, &s->pmt, false, data + pos, TS_SIZE - pos, start, cc, discontinuity);
	} else if (pid != PID_NONE && pid == s->video.pid) {
		return pes_feed(s, &s->video, data + pos, TS_SIZE - pos, start, cc, discontinuity);
	} else if (pid != PID_NONE && pid == s->audio.pid) {
		return pes_feed(s, &s->audio, data + pos, TS_SIZE - pos, start, cc, discontinuity);
	}
	return 0;
}

int miracast_rtp(struct miracast_session *s, const void *packet, size_t len)
{
	const uint8_t *p = packet;
	size_t pos;

	if (!s || !p) {
		return -EINVAL;
	}
	if (s->state != MIRACAST_PLAYING) {
		return 0;
	}
	if (len < 12 || (p[0] >> 6) != 2 || (p[1] & 0x7f) != 33) {
		return -EBADMSG;
	}
	pos = 12 + (p[0] & 15) * 4;
	if (pos > len) {
		return -EBADMSG;
	}
	if (p[0] & 0x10) {
		if (len - pos < 4) {
			return -EBADMSG;
		}
		size_t ext = 4 + (size_t)be16(p + pos + 2) * 4;

		if (ext > len - pos) {
			return -EBADMSG;
		}
		pos += ext;
	}
	if (p[0] & 0x20) {
		if (pos == len || !p[len - 1] || p[len - 1] > len - pos) {
			return -EBADMSG;
		}
		len -= p[len - 1];
	}
	if (pos == len || (len - pos) % TS_SIZE) {
		return -EBADMSG;
	}
	uint16_t seq = be16(p + 2);
	uint32_t ssrc = be32(p + 8);

	if (s->rtp_valid && s->ssrc == ssrc) {
		uint16_t delta = seq - s->rtp_seq;

		if (!delta || delta >= 0x8000) {
			return 0;
		}
		if (delta != 1) {
			mc_media_reset(s);
		}
	} else {
		mc_media_reset(s);
	}
	s->rtp_valid = true;
	s->rtp_seq = seq;
	s->ssrc = ssrc;
	for (; pos < len; pos += TS_SIZE) {
		int ret = transport_stream(s, p + pos);

		if (ret) {
			return ret;
		}
	}
	return 0;
}

int miracast_flush(struct miracast_session *s)
{
	if (!s) {
		return -EINVAL;
	}
	int ret = s->video.active ? emit(s, &s->video) : 0;

	return ret ? ret : s->audio.active ? emit(s, &s->audio) : 0;
}

int miracast_lpcm_decode(const void *data, size_t len, void *pcm, size_t capacity)
{
	const uint8_t *in = data;
	uint8_t *out = pcm;

	if (!in || !out || len < 4 || in[0] != 0xa0 || !in[1] ||
	    len - 4 != (size_t)in[1] * 80 * 4) {
		return -EINVAL;
	}
	if (in[2] != 0 || in[3] != 0x11) {
		return -ENOTSUP;
	}
	if (capacity < len - 4) {
		return -ENOSPC;
	}
	/* memmove permits in-place conversion without overwriting a future input sample. */
	memmove(out, in + 4, len - 4);
	for (size_t i = 0; i < len - 4; i += 2) {
		uint8_t high = out[i];

		out[i] = out[i + 1];
		out[i + 1] = high;
	}
	return (int)(len - 4);
}
