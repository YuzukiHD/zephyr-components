/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* H.264 elementary streams (Annex B) */

#include "features/video/player_priv.h"

/*
 * A file of NAL units with start codes and no container: no time stamps, no index, no sound. The
 * pictures come out of the decoder in display order, so the nth picture is shown at n / fps;
 * the frame rate is the one the stream states (VUI timing) or 30. The length is estimated from the
 * number of pictures in the first megabytes, a seek jumps to a byte offset and starts at the next
 * sequence parameter set.
 */

#define RAW_BUF		(1024 * 1024)
#define RAW_SCAN	(4 * 1024 * 1024)

bool pl_raw_mode;
uint64_t pl_raw_size;
uint32_t pl_raw_period_us = 33333;
uint64_t pl_raw_duration_us;
uint16_t pl_raw_w, pl_raw_h;

struct nalr {
	uint8_t *buf;
	size_t len, pos;	/* data in buf, and where the next NAL is searched from */
	uint64_t off;		/* file offset of buf[0] */
	bool eof;
};

static int raw_fill(struct nalr *n)
{
	ssize_t got;

	if (n->pos > 0) {
		memmove(n->buf, n->buf + n->pos, n->len - n->pos);
		n->off += n->pos;
		n->len -= n->pos;
		n->pos = 0;
	}
	if (n->len >= RAW_BUF || n->eof) {
		return 0;
	}
	if (fs_seek(&pl_vfile.f, (off_t)(n->off + n->len), FS_SEEK_SET) != 0) {
		return -EIO;
	}
	got = fs_read(&pl_vfile.f, n->buf + n->len, RAW_BUF - n->len);
	if (got < 0) {
		return (int)got;
	}
	if (got == 0) {
		n->eof = true;
	}
	n->len += got;

	return 0;
}

/* index of the first 00 00 01 at or after from, or -1 */
static ssize_t raw_find(const uint8_t *b, size_t len, size_t from)
{
	for (size_t i = from; i + 2 < len; i++) {
		if (b[i + 2] > 1) {
			i += 2;
		} else if (b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1) {
			return (ssize_t)i;
		}
	}

	return -1;
}

/*
 * The next NAL unit with its start code: *start is its position in buf, *size its length. The
 * data stays valid until the next call. 0 on success, -ENODATA at the end of the file.
 */
static int raw_next(struct nalr *n, size_t *start, size_t *size)
{
	for (;;) {
		ssize_t s = raw_find(n->buf, n->len, n->pos);
		ssize_t e = -1;

		if (s >= 0) {
			e = raw_find(n->buf, n->len, s + 3);
		}
		if (s >= 0 && e >= 0) {
			*start = s;
			*size = e - s;
			n->pos = e;
			return 0;
		}
		if (n->eof) {
			if (s < 0 || n->len - s <= 3) {
				return -ENODATA;
			}
			*start = s;
			*size = n->len - s;
			n->pos = n->len;
			return 0;
		}
		/* no start code in the whole buffer: drop it; otherwise move the NAL to the front */
		if (s < 0) {
			n->pos = n->len > 2 ? n->len - 2 : 0;
		} else {
			n->pos = s;
		}
		if (n->pos == 0 && n->len >= RAW_BUF) {
			n->pos = n->len;
		}
		if (raw_fill(n) != 0) {
			return -EIO;
		}
	}
}

/* ---- the sequence parameter set: size and frame rate ---- */

struct bits {
	const uint8_t *p;
	size_t n, bit;
};

static uint32_t bits_u(struct bits *b, int cnt)
{
	uint32_t v = 0;

	while (cnt-- > 0) {
		size_t byte = b->bit >> 3;

		v = (v << 1) | (byte < b->n ? (b->p[byte] >> (7 - (b->bit & 7))) & 1 : 0);
		b->bit++;
	}

	return v;
}

static uint32_t bits_ue(struct bits *b)
{
	int zeros = 0;

	while (bits_u(b, 1) == 0 && zeros < 32 && (b->bit >> 3) < b->n) {
		zeros++;
	}

	return zeros == 0 ? 0 : ((1U << zeros) - 1 + bits_u(b, zeros));
}

/* width, height and frame rate (0: not stated) of an SPS (without the start code and header byte) */
static void raw_sps(const uint8_t *rbsp_in, size_t n, uint16_t *w, uint16_t *h, uint32_t *fps_x1000)
{
	uint8_t rbsp[160];
	size_t m = 0;
	struct bits b;
	uint32_t profile, poc_type, mbs_w, mbs_h, frame_mbs_only, cl = 0, cr = 0, ct = 0, cb = 0;
	uint32_t chroma = 1;

	/* undo the emulation prevention bytes */
	for (size_t i = 0; i < n && m < sizeof(rbsp); i++) {
		if (i >= 2 && rbsp_in[i] == 3 && rbsp_in[i - 1] == 0 && rbsp_in[i - 2] == 0) {
			continue;
		}
		rbsp[m++] = rbsp_in[i];
	}
	b = (struct bits){rbsp, m, 0};
	profile = bits_u(&b, 8);
	bits_u(&b, 16);
	bits_ue(&b);
	*fps_x1000 = 0;
	if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 ||
	    profile == 83 || profile == 86 || profile == 118 || profile == 128) {
		chroma = bits_ue(&b);
		if (chroma == 3) {
			bits_u(&b, 1);
		}
		bits_ue(&b);
		bits_ue(&b);
		bits_u(&b, 1);
		if (bits_u(&b, 1)) {
			for (int i = 0; i < (chroma != 3 ? 8 : 12); i++) {
				if (bits_u(&b, 1)) {
					int last = 8, next = 8, size = i < 6 ? 16 : 64;

					for (int j = 0; j < size; j++) {
						if (next != 0) {
							int d = (int)bits_ue(&b);

							d = (d & 1) ? (d + 1) / 2 : -(d / 2);
							next = (last + d + 256) % 256;
						}
						last = next == 0 ? last : next;
					}
				}
			}
		}
	}
	bits_ue(&b);
	poc_type = bits_ue(&b);
	if (poc_type == 0) {
		bits_ue(&b);
	} else if (poc_type == 1) {
		bits_u(&b, 1);
		bits_ue(&b);
		bits_ue(&b);
		for (uint32_t i = bits_ue(&b); i > 0 && i < 256; i--) {
			bits_ue(&b);
		}
	}
	bits_ue(&b);
	bits_u(&b, 1);
	mbs_w = bits_ue(&b) + 1;
	mbs_h = bits_ue(&b) + 1;
	frame_mbs_only = bits_u(&b, 1);
	if (!frame_mbs_only) {
		bits_u(&b, 1);
	}
	bits_u(&b, 1);
	if (bits_u(&b, 1)) {
		cl = bits_ue(&b);
		cr = bits_ue(&b);
		ct = bits_ue(&b);
		cb = bits_ue(&b);
	}
	{
		uint32_t cu_x = chroma == 0 || chroma == 3 ? 1 : 2;
		uint32_t cu_y = (chroma == 1 ? 2 : 1) * (2 - frame_mbs_only);

		if (chroma == 0 || chroma == 3) {
			cu_y = 2 - frame_mbs_only;
		}
		*w = mbs_w * 16 - cu_x * (cl + cr);
		*h = (2 - frame_mbs_only) * mbs_h * 16 - cu_y * (ct + cb);
	}
	if (bits_u(&b, 1)) {			/* VUI */
		if (bits_u(&b, 1) && bits_u(&b, 8) == 255) {
			bits_u(&b, 32);
		}
		if (bits_u(&b, 1)) {
			bits_u(&b, 1);
		}
		if (bits_u(&b, 1)) {
			bits_u(&b, 4);
			if (bits_u(&b, 1)) {
				bits_u(&b, 24);
			}
		}
		if (bits_u(&b, 1)) {
			bits_ue(&b);
			bits_ue(&b);
		}
		if (bits_u(&b, 1)) {
			uint32_t units = bits_u(&b, 32), scale = bits_u(&b, 32);

			if (units > 0 && scale > 0) {
				*fps_x1000 = (uint32_t)((uint64_t)scale * 500ULL / units);
			}
		}
	}
}

/* looks into the start of the file: size, frame rate and the number of pictures */
int pl_raw_probe(uint64_t file_size)
{
	struct nalr n = {.buf = malloc(RAW_BUF)};
	size_t st, sz;
	uint32_t pictures = 0;
	uint64_t scanned = 0;
	bool have_sps = false;

	if (n.buf == NULL) {
		return -ENOMEM;
	}
	if (raw_fill(&n) != 0) {
		free(n.buf);
		return -EIO;
	}
	while (scanned < RAW_SCAN && raw_next(&n, &st, &sz) == 0) {
		const uint8_t *p = n.buf + st;
		size_t hdr = p[2] == 1 ? 3 : 4;
		uint8_t type = p[hdr] & 0x1f;

		if (type == 7 && !have_sps && sz > hdr + 4) {
			uint32_t fps = 0;

			raw_sps(p + hdr + 1, MIN(sz - hdr - 1, 160), &pl_raw_w, &pl_raw_h, &fps);
			pl_raw_period_us = fps > 0 ? (uint32_t)(1000000000ULL / fps) : 33333;
			have_sps = true;
		} else if ((type == 1 || type == 5) && sz > hdr + 1 && (p[hdr + 1] & 0x80)) {
			/* the first macroblock of a slice is 0: a new picture */
			pictures++;
		}
		scanned = n.off + n.pos;
	}
	free(n.buf);
	if (!have_sps || pictures == 0) {
		return -ENOTSUP;
	}
	pl_raw_duration_us = (uint64_t)((double)pictures * (double)file_size / (double)scanned *
				     (double)pl_raw_period_us);

	return 0;
}

/* where a seek starts: the first sequence parameter set at or after the byte offset */
static void raw_seek(struct nalr *n, uint64_t offset)
{
	size_t st, sz;

	n->off = offset & ~(uint64_t)4095;
	n->len = n->pos = 0;
	n->eof = false;
	if (raw_fill(n) != 0) {
		return;
	}
	while (raw_next(n, &st, &sz) == 0) {
		const uint8_t *p = n->buf + st;
		size_t hdr = p[2] == 1 ? 3 : 4;

		if ((p[hdr] & 0x1f) == 7) {
			n->pos = st;
			return;
		}
	}
	/* none: from the start of the file */
	n->off = 0;
	n->len = n->pos = 0;
	n->eof = false;
	raw_fill(n);
}

static void raw_session(const struct request *r)
{
	const struct vdec_stream_config scfg = {
		.codec = VDEC_CODEC_H264,
		.format = VDEC_FORMAT_NV12,
		.buffer_size = CONFIG_NEKOBOX_VIDEO_STREAM_KB * 1024,
		.no_cache_ops = true,
	};
	struct vdec_frame frame;
	struct vdec_stream *stream;
	struct nalr n = {.buf = malloc(RAW_BUF)};
	size_t nal_st = 0, nal_len = 0, nal_used = 0;
	int64_t start_ms = k_uptime_get();
	int64_t base_us = r->pos_us;
	uint32_t shown_n = 0;
	bool flushed = false, held = false, any_shown = false;
	int ret;

	printk("player: raw session %u at %d ms, %s\n", r->seq, (int)(r->pos_us / 1000),
	       r->play ? "playing" : "paused");
	if (n.buf == NULL || vdec_stream_open(pl_vdec_dev, &scfg, &stream) != 0) {
		free(n.buf);
		pl_state = PLAYER_ERROR;
		while (!pl_req_changed(r->seq)) {
			k_msleep(20);
		}
		return;
	}
	if (r->pos_us > 0 && pl_raw_duration_us > 0) {
		uint64_t at = (uint64_t)((double)pl_raw_size * (double)r->pos_us / (double)pl_raw_duration_us);

		raw_seek(&n, MIN(at, pl_raw_size > 4096 ? pl_raw_size - 4096 : 0));
	} else {
		raw_fill(&n);
	}

	while (!pl_req_changed(r->seq)) {
		pl_release_done();
		ret = vdec_stream_get_frame(pl_vdec_dev, stream, &frame);
		if (ret == 0) {
			int64_t pts_us = base_us + (int64_t)shown_n * pl_raw_period_us;
			enum wait_result w;

			shown_n++;
			if (r->play) {
				pl_video_ready_seq = r->seq;
			}
			w = r->play ? pl_wait_for(r->seq, pts_us, r->pos_us, start_ms) : WAIT_ON_TIME;
			if (w == WAIT_ABORT) {
				vdec_frame_release(pl_vdec_dev, &frame);
				break;
			}
			if (w == WAIT_ON_TIME) {
				k_msgq_put(&disp_q, &frame, K_FOREVER);
				any_shown = true;
				pl_pos_publish(r->seq, pts_us / 1000);
				if (!r->play) {
					held = true;
					break;
				}
			} else {
				vdec_frame_release(pl_vdec_dev, &frame);
			}
			continue;
		}
		if (ret == -ENODATA) {
			pl_video_ready_seq = r->seq;
			if (pl_pos_publish(r->seq, pl_raw_duration_us / 1000) && !pl_req_changed(r->seq)) {
				pl_state = PLAYER_ENDED;
			}
			held = true;
			break;
		}
		if (ret == -EBUSY) {
			k_msleep(2);
			continue;
		}
		if (ret != -EAGAIN) {
			printk("video: decode error %d\n", ret);
			pl_state = PLAYER_ERROR;
			held = true;
			break;
		}

		/* the decoder wants data */
		if (nal_used >= nal_len) {
			int e = raw_next(&n, &nal_st, &nal_len);

			nal_used = 0;
			if (e != 0) {
				nal_len = 0;
				if (!flushed) {
					vdec_stream_flush(pl_vdec_dev, stream);
					flushed = true;
				}
				continue;
			}
		}
		if (nal_used < nal_len) {
			size_t used = 0;

			ret = vdec_stream_feed(pl_vdec_dev, stream, n.buf + nal_st + nal_used,
					       nal_len - nal_used, 0, &used);
			if (ret != 0 && ret != -EAGAIN) {
				nal_used = nal_len;
			} else {
				nal_used += used;
			}
		}
	}

	while (held && !pl_req_changed(r->seq)) {
		k_msleep(20);
	}
	if (any_shown) {
		struct vdec_frame end = {0};

		k_msgq_put(&disp_q, &end, K_FOREVER);
		k_sem_take(&disp_flushed, K_FOREVER);
	}
	pl_release_done();
	vdec_stream_close(pl_vdec_dev, stream);
	free(n.buf);
}

void pl_raw_main(void *a, void *b, void *c)
{
	uint32_t done = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	while (!pl_quit) {
		struct request r = pl_req_get();

		if (r.seq == done) {
			k_msleep(5);
			continue;
		}
		done = r.seq;
		raw_session(&r);
	}
}

bool pl_is_raw_name(const char *path)
{
	size_t n = strlen(path);
	static const char *const ext[] = {".h264", ".264", ".avc", ".H264", ".264"};

	for (int i = 0; i < ARRAY_SIZE(ext); i++) {
		size_t e = strlen(ext[i]);

		if (n > e && strcmp(path + n - e, ext[i]) == 0) {
			return true;
		}
	}

	return false;
}

