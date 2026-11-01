/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/audio/codec.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/vdec.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <miracast/player.h>

#define VIDEO_MAX (512 * 1024)
#define PCM_BLOCK 1920

struct packet {
	size_t len;
	int64_t pts;
	bool discontinuity;
	uint8_t data[];
};

K_MSGQ_DEFINE(wfd_video_q, sizeof(struct packet *), 6, sizeof(void *));
K_MSGQ_DEFINE(wfd_audio_q, sizeof(struct packet *), 24, sizeof(void *));
K_MEM_SLAB_DEFINE_STATIC(pcm_slab, PCM_BLOCK, 8, 4);
static K_SEM_DEFINE(video_stopped, 0, 1);
static K_SEM_DEFINE(audio_stopped, 0, 1);
static atomic_t frames, stopping, accepting;
static unsigned volume;
static bool video_gap, audio_gap;
static struct k_spinlock clock_lock;
static int64_t clock_pts, clock_ms;
static bool clock_valid;

static const struct device *const ve = DEVICE_DT_GET(DT_NODELABEL(ve));
static const struct device *const display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *const i2s = DEVICE_DT_GET(DT_NODELABEL(audio_codec));
static const struct device *const codec = DEVICE_DT_GET(DT_NODELABEL(codec_analog));

static void clock_reset(void)
{
	k_spinlock_key_t key = k_spin_lock(&clock_lock);

	clock_valid = false;
	k_spin_unlock(&clock_lock, key);
}

static int64_t presentation(int64_t pts)
{
	int64_t now = k_uptime_get();

	if (pts < 0) {
		return now;
	}
	k_spinlock_key_t key = k_spin_lock(&clock_lock);

	if (!clock_valid) {
		clock_pts = pts;
		clock_ms = now + 120;
		clock_valid = true;
	}
	/* Signed distance in the 33-bit PTS clock, including wrap. */
	int64_t delta = (pts - clock_pts) & 0x1ffffffffLL;

	if (delta & 0x100000000LL) {
		delta -= 0x200000000LL;
	}
	int64_t target = clock_ms + delta / 90;

	if (target > now + 2000 || target < now - 2000) {
		clock_pts = pts;
		clock_ms = target = now + 120;
	}
	k_spin_unlock(&clock_lock, key);
	return target;
}

static void wait_until(int64_t target)
{
	while (!atomic_get(&stopping)) {
		int64_t delay = target - k_uptime_get();

		if (delay <= 0) break;
		k_msleep(MIN(delay, 5));
	}
}

int miracast_player_media(void *ctx, enum miracast_codec type, const void *data, size_t len,
			  int64_t pts, bool discontinuity)
{
	ARG_UNUSED(ctx);
	if (type == MIRACAST_AAC) {
		return -ENOTSUP;
	}
	struct k_msgq *q = type == MIRACAST_H264 ? &wfd_video_q : &wfd_audio_q;
	bool *gap = type == MIRACAST_H264 ? &video_gap : &audio_gap;
	struct packet *p;

	if (!atomic_get(&accepting) || atomic_get(&stopping) || k_msgq_num_free_get(q) == 0) {
		*gap = true;
		return -ENOBUFS;
	}
	p = malloc(sizeof(*p) + len);
	if (!p) {
		*gap = true;
		return -ENOBUFS;
	}
	p->len = len;
	p->pts = pts;
	p->discontinuity = discontinuity || *gap;
	memcpy(p->data, data, len);
	if (k_msgq_put(q, &p, K_NO_WAIT)) {
		free(p);
		*gap = true;
		return -ENOBUFS;
	}
	*gap = false;
	return 0;
}

static struct vdec_stream *stream;
static struct vdec_frame shown, older;

static void video_close(void)
{
	if (!stream) {
		return;
	}
	display_sunxi_hide_yuv(display);
	/* The display latches the disabled plane at the next vblank. */
	k_msleep(35);
	if (older.priv) {
		vdec_frame_release(ve, &older);
	}
	if (shown.priv) {
		vdec_frame_release(ve, &shown);
	}
	memset(&shown, 0, sizeof(shown));
	memset(&older, 0, sizeof(older));
	vdec_stream_close(ve, stream);
	stream = NULL;
}

static int drain(void)
{
	struct vdec_frame f;
	int count = 0;

	while (vdec_stream_get_frame(ve, stream, &f) == 0) {
		int64_t target = presentation(f.pts);

		if (atomic_get(&stopping) || target < k_uptime_get() - 150) {
			vdec_frame_release(ve, &f);
			continue;
		}
		wait_until(target);
		struct display_sunxi_yuv yuv = {
			.y = f.plane[0], .uv = f.plane[1], .width = f.width, .height = f.height,
			.stride_y = f.stride[0], .stride_uv = f.stride[1],
			.bt709 = f.width >= 1280, .nonblock = true,
		};

		if (display_sunxi_show_yuv(display, &yuv)) {
			vdec_frame_release(ve, &f);
			return -EIO;
		}
		if (older.priv) {
			vdec_frame_release(ve, &older);
		}
		older = shown;
		shown = f;
		atomic_inc(&frames);
		count++;
	}
	return count;
}

static int feed(const uint8_t *data, size_t len, int64_t pts)
{
	while (len && !atomic_get(&stopping)) {
		size_t used = 0;
		int ret = vdec_stream_feed(ve, stream, data, len, pts, &used);

		if (ret && ret != -EAGAIN) {
			return ret;
		}
		int decoded = drain();

		if (decoded < 0) {
			return decoded;
		}
		if (!used && !decoded) {
			return -ENOBUFS;
		}
		data += used;
		len -= used;
	}
	return 0;
}

static size_t start_code(const uint8_t *data, size_t len, size_t from)
{
	for (size_t i = from; i + 3 <= len; i++) {
		if (!data[i] && !data[i + 1] &&
		    (data[i + 2] == 1 || (i + 4 <= len && !data[i + 2] && data[i + 3] == 1))) {
			return i;
		}
	}
	return len;
}

static void video_run(void *a, void *b, void *c)
{
	uint8_t *buf = NULL, sps[512], pps[512];
	size_t len = 0, sps_len = 0, pps_len = 0;
	int64_t pts = -1;
	bool keyframe = true;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	while (true) {
		struct packet *pkt;

		k_msgq_get(&wfd_video_q, &pkt, K_FOREVER);
		if (!pkt) {
			video_close();
			free(buf);
			buf = NULL;
			len = sps_len = pps_len = 0;
			keyframe = true;
			k_sem_give(&video_stopped);
			continue;
		}
		if (atomic_get(&stopping)) {
			free(pkt);
			continue;
		}
		if (!buf) {
			buf = malloc(VIDEO_MAX);
		}
		if (!buf) {
			free(pkt);
			miracast_receiver_request_idr();
			continue;
		}
		if (pkt->discontinuity || pkt->len > VIDEO_MAX - len) {
			video_close();
			len = 0;
			keyframe = true;
			miracast_receiver_request_idr();
		}
		if (pkt->len > VIDEO_MAX) {
			free(pkt);
			continue;
		}
		if (!len) {
			pts = pkt->pts;
		}
		memcpy(buf + len, pkt->data, pkt->len);
		len += pkt->len;
		size_t pos = start_code(buf, len, 0);

		while (pos < len) {
			size_t prefix = buf[pos + 2] == 1 ? 3 : 4;
			size_t next = start_code(buf, len, pos + prefix);

			if (next == len || pos + prefix == next) {
				break;
			}
			unsigned type = buf[pos + prefix] & 31;
			size_t size = next - pos;
			int ret = 0;

			if (type == 7 && size <= sizeof(sps)) {
				memcpy(sps, buf + pos, size);
				sps_len = size;
			} else if (type == 8 && size <= sizeof(pps)) {
				memcpy(pps, buf + pos, size);
				pps_len = size;
			}
			if (keyframe && type == 5 && sps_len && pps_len) {
				struct vdec_stream_config config = {
					.codec = VDEC_CODEC_H264, .format = VDEC_FORMAT_NV12,
					.buffer_size = VIDEO_MAX, .no_cache_ops = true,
					.holding_frames = 2,
				};

				ret = vdec_stream_open(ve, &config, &stream);
				if (!ret) ret = feed(sps, sps_len, pts);
				if (!ret) ret = feed(pps, pps_len, pts);
				if (!ret) keyframe = false;
			}
			if (!ret && !keyframe && (type == 1 || type == 5 || type == 7 || type == 8)) {
				ret = feed(buf + pos, size, pts);
			}
			if (ret) {
				printk("miracast video: %d, waiting for IDR\n", ret);
				video_close();
				keyframe = true;
				miracast_receiver_request_idr();
			}
			pos = next;
			pts = pkt->pts;
		}
		if (pos == len) {
			/* Preserve a possible split start code. */
			pos = len > 3 ? len - 3 : 0;
		}
		memmove(buf, buf + pos, len - pos);
		len -= pos;
		free(pkt);
	}
}

static void audio_run(void *a, void *b, void *c)
{
	struct i2s_config config = {
		.word_size = 16, .channels = 2, .format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
		.frame_clk_freq = 48000, .mem_slab = &pcm_slab,
		.block_size = PCM_BLOCK, .timeout = 200,
	};
	bool configured = false, playing = false;
	unsigned queued = 0;
	int64_t first = -1;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	while (true) {
		struct packet *p;

		k_msgq_get(&wfd_audio_q, &p, K_FOREVER);
		if (!p) {
			if (configured) i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_DROP);
			audio_codec_stop_output(codec);
			configured = playing = false;
			queued = 0;
			k_sem_give(&audio_stopped);
			continue;
		}
		if (atomic_get(&stopping)) {
			free(p);
			continue;
		}
		int bytes = miracast_lpcm_decode(p->data, p->len, p->data, p->len);

		if (bytes < 0) {
			printk("miracast LPCM: %d\n", bytes);
			free(p);
			continue;
		}
		if (!configured) {
			if (i2s_configure(i2s, I2S_DIR_TX, &config)) {
				free(p);
				continue;
			}
			audio_property_value_t gain = {.vol = volume * 200 / 100};
			audio_property_value_t mute = {.mute = false};

			audio_codec_set_property(codec, AUDIO_PROPERTY_OUTPUT_VOLUME, AUDIO_CHANNEL_ALL, gain);
			audio_codec_set_property(codec, AUDIO_PROPERTY_OUTPUT_MUTE, AUDIO_CHANNEL_ALL, mute);
			audio_codec_start_output(codec);
			configured = true;
		}
		if (p->discontinuity) {
			i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_DROP);
			playing = false;
			queued = 0;
		}
		for (int pos = 0; pos < bytes && !atomic_get(&stopping);) {
			void *block;
			size_t size = MIN(PCM_BLOCK, bytes - pos);
			int ret = k_mem_slab_alloc(&pcm_slab, &block, K_MSEC(200));

			if (ret) break;
			memcpy(block, p->data + pos, size);
			ret = i2s_write(i2s, block, size);
			if (ret == -EIO) {
				i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
				playing = false;
				queued = 0;
				clock_reset();
				ret = i2s_write(i2s, block, size);
			}
			if (ret) {
				k_mem_slab_free(&pcm_slab, block);
				break;
			}
			if (!queued) {
				first = p->pts < 0 ? -1 : p->pts + (int64_t)pos * 90000 / (48000 * 4);
			}
			if (!playing && ++queued >= 2) {
				wait_until(presentation(first));
				if (!atomic_get(&stopping)) {
					playing = i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_START) == 0;
				}
			}
			pos += size;
		}
		free(p);
	}
}

K_THREAD_DEFINE(wfd_video_thread, 16384, video_run, NULL, NULL, NULL, 8, 0, 0);
K_THREAD_DEFINE(wfd_audio_thread, 8192, audio_run, NULL, NULL, NULL, 6, 0, 0);

int miracast_player_start(unsigned percent)
{
	if (percent > 100) {
		return -EINVAL;
	}
	if (!device_is_ready(ve) || !device_is_ready(display) ||
	    !device_is_ready(i2s) || !device_is_ready(codec)) {
		return -ENODEV;
	}
	if (atomic_get(&accepting)) {
		return -EBUSY;
	}
	volume = percent;
	atomic_clear(&stopping);
	atomic_set(&accepting, 1);
	return 0;
}

void miracast_player_stop(void)
{
	struct packet *end = NULL;

	if (!atomic_get(&accepting)) {
		return;
	}
	atomic_clear(&accepting);
	atomic_set(&stopping, 1);
	k_msgq_put(&wfd_video_q, &end, K_FOREVER);
	k_msgq_put(&wfd_audio_q, &end, K_FOREVER);
	k_sem_take(&video_stopped, K_FOREVER);
	k_sem_take(&audio_stopped, K_FOREVER);
	clock_reset();
	atomic_clear(&frames);
	atomic_clear(&stopping);
	video_gap = audio_gap = false;
}

unsigned miracast_player_frames(void)
{
	return atomic_get(&frames);
}
