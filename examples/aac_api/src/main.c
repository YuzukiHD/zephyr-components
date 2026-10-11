/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Walks through the API of the OpenCORE AAC decoder (pvmp4audiodecoder_api.h) with two embedded
 * clips: the PCM checksums are compared with a decode of the same sources on the host, and two
 * frames of each clip with the output of ffmpeg.
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/audio/codec.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>
#include <pvmp4audiodecoder_api.h>

#include "clips.h"

/* CRC-32 of the whole PCM output, from a build of the same decoder sources on the host */
#define HOST_CRC_A	 0x145b0881U	/* clip_a, 2 channels */
#define HOST_CRC_B_MONO	 0x31898848U	/* clip_b, desiredChannels 1 */
#define HOST_CRC_B_STEREO 0x11a83335U	/* clip_b, desiredChannels 2 */

/* AudioSpecificConfig (ISO 14496-3): object type 2 (AAC-LC), rate index, channel configuration */
static const uint8_t asc_a[2] = {0x12, 0x10};	/* 44.1 kHz, stereo */
static const uint8_t asc_b[2] = {0x13, 0x88};	/* 22.05 kHz, mono */

#define MAX_FRAMES 128

/* playback: one block of the I2S stream is one decoded frame, 1024 samples of 2 channels */
#define PLAY_BLOCK	(1024 * 2 * sizeof(int16_t))
#define PLAY_BLOCKS	8
#define PLAY_PREFILL	4	/* blocks queued before the stream is started */
#define PLAY_LOOPS	3	/* clip_a is played this many times */

K_MEM_SLAB_DEFINE_STATIC(play_slab, PLAY_BLOCK, PLAY_BLOCKS, 4);

/* the digital side of the on-chip codec is an I2S device, the analog side an audio codec device */
static const struct device *const codec_i2s = DEVICE_DT_GET(DT_NODELABEL(audio_codec));
static const struct device *const codec_ctl = DEVICE_DT_GET(DT_NODELABEL(codec_analog));

/* one frame of an ADTS stream: where the header starts, its length, the payload behind it */
struct adts_frame {
	uint32_t off;
	uint16_t hdr;
	uint16_t len;
};

struct pcm_stat {
	uint32_t crc;
	uint32_t frames;
	uint32_t samples;
	int peak;
	uint64_t sum_abs;
	int ref_diff;
	int64_t us;
};

static struct adts_frame frames_a[MAX_FRAMES];
static struct adts_frame frames_b[MAX_FRAMES];
static int16_t pcm[2048 * 2];
static uint32_t adts_crc_a;

static const char *err_name(int ret)
{
	switch (ret) {
	case MP4AUDEC_SUCCESS:
		return "MP4AUDEC_SUCCESS";
	case MP4AUDEC_INVALID_FRAME:
		return "MP4AUDEC_INVALID_FRAME";
	case MP4AUDEC_INCOMPLETE_FRAME:
		return "MP4AUDEC_INCOMPLETE_FRAME";
	case MP4AUDEC_LOST_FRAME_SYNC:
		return "MP4AUDEC_LOST_FRAME_SYNC";
	default:
		return "other";
	}
}

/* cut an ADTS stream into frames by the 13 bit length in the header */
static int index_adts(const uint8_t *p, size_t size, struct adts_frame *fr)
{
	size_t pos = 0;
	int n = 0;

	while (pos + 7 <= size && n < MAX_FRAMES) {
		size_t len = ((p[pos + 3] & 3) << 11) | (p[pos + 4] << 3) | (p[pos + 5] >> 5);
		size_t hdr = (p[pos + 1] & 1) ? 7 : 9;

		if (p[pos] != 0xff || (p[pos + 1] & 0xf0) != 0xf0 || len < hdr ||
		    pos + len > size) {
			break;
		}
		fr[n].off = pos;
		fr[n].hdr = hdr;
		fr[n].len = len;
		n++;
		pos += len;
	}
	return n;
}

/* add one decoded frame to the statistics, compare with the ffmpeg reference if it covers it */
static void account(struct pcm_stat *st, int count, const int16_t *ref, int ref_first,
		    int ref_frames)
{
	uint32_t f = st->frames;

	st->crc = crc32_ieee_update(st->crc, (const uint8_t *)pcm, count * sizeof(int16_t));
	for (int i = 0; i < count; i++) {
		int a = abs(pcm[i]);

		st->peak = MAX(st->peak, a);
		st->sum_abs += a;
	}
	if (ref != NULL && f >= (uint32_t)ref_first && f < (uint32_t)(ref_first + ref_frames)) {
		const int16_t *r = ref + (f - ref_first) * count;

		for (int i = 0; i < count; i++) {
			st->ref_diff = MAX(st->ref_diff, abs(pcm[i] - r[i]));
		}
	}
	st->samples += count;
	st->frames++;
}

static void print_stat(const char *tag, const struct pcm_stat *st, uint32_t host_crc)
{
	printk("[%s] %u frames, %u samples, CRC-32 %08x (host %08x, %s)\n", tag, st->frames,
	       st->samples, st->crc, host_crc, st->crc == host_crc ? "match" : "DIFFERENT");
	printk("[%s] peak %d, mean |sample| %u, %lld us in total, %lld us per frame\n", tag,
	       st->peak, (uint32_t)(st->sum_abs / st->samples), st->us,
	       st->frames ? st->us / st->frames : 0);
}

static void *new_decoder(void)
{
	return malloc(PVMP4AudioDecoderGetMemRequirements());
}

/* decode a whole ADTS clip: the decoder finds the headers itself */
static int decode_adts(const uint8_t *data, size_t size, int desired, const int16_t *ref,
		       int ref_first, int ref_frames, struct pcm_stat *st, bool print_first)
{
	tPVMP4AudioDecoderExternal ext = {0};
	void *mem = new_decoder();
	int ret = 0;

	memset(st, 0, sizeof(*st));
	if (mem == NULL) {
		return -ENOMEM;
	}
	ext.desiredChannels = desired;
	ext.outputFormat = OUTPUTFORMAT_16PCM_INTERLEAVED;
	ext.aacPlusEnabled = false;
	PVMP4AudioDecoderInitLibrary(&ext, mem);

	ext.pInputBuffer = (UChar *)data;
	ext.inputBufferCurrentLength = size;
	ext.inputBufferUsedLength = 0;
	ext.remainderBits = 0;
	while (ext.inputBufferUsedLength + 7 < (int)size) {
		uint32_t t0;

		ext.pOutputBuffer = pcm;
		ext.pOutputBuffer_plus = pcm + 2048;
		t0 = k_cycle_get_32();
		ret = PVMP4AudioDecodeFrame(&ext, mem);
		st->us += k_cyc_to_us_near64(k_cycle_get_32() - t0);
		if (ret != MP4AUDEC_SUCCESS) {
			printk("[decode] frame %u: %s (%d)\n", st->frames, err_name(ret), ret);
			break;
		}
		if (st->frames == 0 && print_first) {
			printk("[decode] first frame: %d Hz, %d channels in the stream, %d samples per "
			       "channel, audio object type %d, %d bit/s, used %d bytes\n",
			       (int)ext.samplingRate, ext.encodedChannels, ext.frameLength,
			       ext.audioObjectType, (int)ext.bitRate, ext.inputBufferUsedLength);
		}
		account(st, ext.frameLength * desired, ref, ref_first, ref_frames);
	}
	free(mem);
	return ret;
}

/* decode raw frames (no ADTS header) after the decoder has been configured */
static int decode_raw(tPVMP4AudioDecoderExternal *ext, void *mem, const uint8_t *data,
		      const struct adts_frame *fr, int from, int count, struct pcm_stat *st)
{
	int ret = 0;

	for (int i = from; i < from + count; i++) {
		uint32_t t0;

		ext->pInputBuffer = (UChar *)data + fr[i].off + fr[i].hdr;
		ext->inputBufferCurrentLength = fr[i].len - fr[i].hdr;
		ext->inputBufferUsedLength = 0;
		ext->remainderBits = 0;
		ext->pOutputBuffer = pcm;
		ext->pOutputBuffer_plus = pcm + 2048;
		t0 = k_cycle_get_32();
		ret = PVMP4AudioDecodeFrame(ext, mem);
		st->us += k_cyc_to_us_near64(k_cycle_get_32() - t0);
		if (ret != MP4AUDEC_SUCCESS) {
			printk("[raw] frame %d: %s (%d)\n", i, err_name(ret), ret);
			return ret;
		}
		account(st, ext->frameLength * ext->desiredChannels, NULL, 0, 0);
	}
	return 0;
}

static void setup_ext(tPVMP4AudioDecoderExternal *ext, int desired)
{
	memset(ext, 0, sizeof(*ext));
	ext->desiredChannels = desired;
	ext->outputFormat = OUTPUTFORMAT_16PCM_INTERLEAVED;
	ext->aacPlusEnabled = false;
}

static void demo_get_mem_requirements(void)
{
	uint32_t size = PVMP4AudioDecoderGetMemRequirements();
	void *mem = malloc(size);

	printk("[mem] PVMP4AudioDecoderGetMemRequirements = %u bytes, malloc %s\n", size,
	       mem != NULL ? "ok" : "failed");
	printk("[mem] PVMP4AUDIODECODER_INBUFSIZE = %d bytes\n", PVMP4AUDIODECODER_INBUFSIZE);
	free(mem);
}

static void demo_init_library(void)
{
	tPVMP4AudioDecoderExternal ext = {0};
	void *mem = new_decoder();
	int ret;

	if (mem == NULL) {
		printk("[init] no memory\n");
		return;
	}
	setup_ext(&ext, 2);
	ext.inputBufferUsedLength = 77;	/* the call clears the output fields */
	ext.samplingRate = 1;
	ret = PVMP4AudioDecoderInitLibrary(&ext, mem);
	printk("[init] PVMP4AudioDecoderInitLibrary = %d: samplingRate %d, encodedChannels %d, "
	       "bitRate %d, used %d, upsampling %d\n",
	       ret, (int)ext.samplingRate, ext.encodedChannels, (int)ext.bitRate,
	       ext.inputBufferUsedLength, (int)ext.aacPlusUpsamplingFactor);
	free(mem);
}

static void demo_decode_frame(void)
{
	struct pcm_stat st;
	int ret;

	/* stereo clip, ADTS: no PVMP4AudioDecoderConfig, the decoder reads the ADTS headers */
	ret = decode_adts(clip_a, sizeof(clip_a), 2, ref_a, REF_A_FIRST_FRAME, REF_A_FRAMES, &st,
			  true);
	printk("[decode] clip_a, ADTS, desiredChannels 2: last PVMP4AudioDecodeFrame = %d\n", ret);
	adts_crc_a = st.crc;
	print_stat("decode", &st, HOST_CRC_A);
	printk("[decode] frames %d..%d against ffmpeg: largest difference %d\n", REF_A_FIRST_FRAME,
	       REF_A_FIRST_FRAME + REF_A_FRAMES - 1, st.ref_diff);

	/* mono clip: desiredChannels 1 keeps it mono, 2 writes every sample into both channels */
	ret = decode_adts(clip_b, sizeof(clip_b), 1, ref_b, REF_B_FIRST_FRAME, REF_B_FRAMES, &st,
			  true);
	printk("[decode] clip_b, ADTS, desiredChannels 1: last PVMP4AudioDecodeFrame = %d\n", ret);
	print_stat("decode", &st, HOST_CRC_B_MONO);
	printk("[decode] frames %d..%d against ffmpeg: largest difference %d\n", REF_B_FIRST_FRAME,
	       REF_B_FIRST_FRAME + REF_B_FRAMES - 1, st.ref_diff);

	ret = decode_adts(clip_b, sizeof(clip_b), 2, NULL, 0, 0, &st, false);
	printk("[decode] clip_b, ADTS, desiredChannels 2: last PVMP4AudioDecodeFrame = %d\n", ret);
	print_stat("decode", &st, HOST_CRC_B_STEREO);
}

static void demo_decode_errors(void)
{
	tPVMP4AudioDecoderExternal ext;
	static uint8_t junk[64];
	void *mem = new_decoder();
	int ret;

	if (mem == NULL) {
		return;
	}

	/* bytes that are no ADTS header: the decoder lost the frame sync */
	for (int i = 0; i < sizeof(junk); i++) {
		junk[i] = i * 37 + 11;
	}
	setup_ext(&ext, 2);
	PVMP4AudioDecoderInitLibrary(&ext, mem);
	ext.pInputBuffer = junk;
	ext.inputBufferCurrentLength = sizeof(junk);
	ext.pOutputBuffer = pcm;
	ext.pOutputBuffer_plus = pcm + 2048;
	ret = PVMP4AudioDecodeFrame(&ext, mem);
	printk("[error] ADTS stream with garbage    = %s (%d)\n", err_name(ret), ret);

	/* an ADTS frame cut short */
	setup_ext(&ext, 2);
	PVMP4AudioDecoderInitLibrary(&ext, mem);
	ext.pInputBuffer = (UChar *)clip_a;
	ext.inputBufferCurrentLength = frames_a[0].len / 2;
	ext.pOutputBuffer = pcm;
	ext.pOutputBuffer_plus = pcm + 2048;
	ret = PVMP4AudioDecodeFrame(&ext, mem);
	printk("[error] ADTS frame cut in half      = %s (%d)\n", err_name(ret), ret);

	/* a raw frame cut short, after the AudioSpecificConfig */
	setup_ext(&ext, 2);
	PVMP4AudioDecoderInitLibrary(&ext, mem);
	ext.pInputBuffer = (UChar *)asc_a;
	ext.inputBufferCurrentLength = sizeof(asc_a);
	PVMP4AudioDecoderConfig(&ext, mem);
	ext.pInputBuffer = (UChar *)clip_a + frames_a[30].off + frames_a[30].hdr;
	ext.inputBufferCurrentLength = (frames_a[30].len - frames_a[30].hdr) / 2;
	ext.inputBufferUsedLength = 0;
	ext.remainderBits = 0;
	ext.pOutputBuffer = pcm;
	ext.pOutputBuffer_plus = pcm + 2048;
	ret = PVMP4AudioDecodeFrame(&ext, mem);
	printk("[error] raw frame cut in half       = %s (%d)\n", err_name(ret), ret);

	/* the used length is already past the end of the buffer */
	setup_ext(&ext, 2);
	PVMP4AudioDecoderInitLibrary(&ext, mem);
	ext.pInputBuffer = (UChar *)clip_a;
	ext.inputBufferCurrentLength = 16;
	ext.inputBufferUsedLength = 100;
	ext.pOutputBuffer = pcm;
	ext.pOutputBuffer_plus = pcm + 2048;
	ret = PVMP4AudioDecodeFrame(&ext, mem);
	printk("[error] used length beyond the end  = %s (%d)\n", err_name(ret), ret);
	free(mem);
}

static void demo_config(void)
{
	tPVMP4AudioDecoderExternal ext;
	struct pcm_stat st = {0};
	void *mem = new_decoder();
	int ret;

	if (mem == NULL) {
		return;
	}
	setup_ext(&ext, 2);
	PVMP4AudioDecoderInitLibrary(&ext, mem);

	/* the 2 byte AudioSpecificConfig tells the decoder the layout of the raw frames */
	ext.pInputBuffer = (UChar *)asc_a;
	ext.inputBufferCurrentLength = sizeof(asc_a);
	ext.inputBufferUsedLength = 0;
	ext.remainderBits = 0;
	ret = PVMP4AudioDecoderConfig(&ext, mem);
	printk("[config] PVMP4AudioDecoderConfig = %d: %d Hz, %d channels, %d samples per channel, "
	       "used %d bytes\n",
	       ret, (int)ext.samplingRate, ext.encodedChannels, ext.frameLength,
	       ext.inputBufferUsedLength);

	/* the frames now come without their ADTS headers, as in an MP4 file */
	ret = decode_raw(&ext, mem, clip_a, frames_a, 0, 64, &st);
	printk("[config] raw frames, last PVMP4AudioDecodeFrame = %d\n", ret);
	printk("[config] %u frames, CRC-32 %08x, same as the ADTS decode (%08x): %s\n", st.frames,
	       st.crc, adts_crc_a, st.crc == adts_crc_a ? "yes" : "NO");
	free(mem);

	/* the mono clip, with its own AudioSpecificConfig */
	mem = new_decoder();
	memset(&st, 0, sizeof(st));
	setup_ext(&ext, 1);
	PVMP4AudioDecoderInitLibrary(&ext, mem);
	ext.pInputBuffer = (UChar *)asc_b;
	ext.inputBufferCurrentLength = sizeof(asc_b);
	ext.inputBufferUsedLength = 0;
	ext.remainderBits = 0;
	ret = PVMP4AudioDecoderConfig(&ext, mem);
	printk("[config] PVMP4AudioDecoderConfig = %d: %d Hz, %d channels, %d samples per channel\n",
	       ret, (int)ext.samplingRate, ext.encodedChannels, ext.frameLength);
	ret = decode_raw(&ext, mem, clip_b, frames_b, 0, 26, &st);
	printk("[config] clip_b raw frames: %u frames, CRC-32 %08x, host %08x: %s\n", st.frames,
	       st.crc, HOST_CRC_B_MONO, st.crc == HOST_CRC_B_MONO ? "match" : "DIFFERENT");
	free(mem);

	/* an AudioSpecificConfig that is too short */
	mem = new_decoder();
	setup_ext(&ext, 2);
	PVMP4AudioDecoderInitLibrary(&ext, mem);
	ext.pInputBuffer = (UChar *)asc_a;
	ext.inputBufferCurrentLength = 1;
	ext.inputBufferUsedLength = 0;
	ext.remainderBits = 0;
	ret = PVMP4AudioDecoderConfig(&ext, mem);
	printk("[config] AudioSpecificConfig of 1 byte = %s (%d)\n", err_name(ret), ret);

	/* object type 0 is not an AAC type */
	{
		static const uint8_t bad[2] = {0x00, 0x00};

		ext.pInputBuffer = (UChar *)bad;
		ext.inputBufferCurrentLength = sizeof(bad);
		ext.inputBufferUsedLength = 0;
		ext.remainderBits = 0;
		ret = PVMP4AudioDecoderConfig(&ext, mem);
		printk("[config] AudioSpecificConfig of zeros = %s (%d)\n", err_name(ret), ret);
	}
	free(mem);
}

static void demo_set_audio_config(void)
{
	tPVMP4AudioDecoderExternal ext;
	struct pcm_stat st = {0};
	void *mem = new_decoder();
	int ret;

	if (mem == NULL) {
		return;
	}
	setup_ext(&ext, 2);
	PVMP4AudioDecoderInitLibrary(&ext, mem);

	/* the same as the AudioSpecificConfig, but from numbers the container already parsed */
	ext.pInputBuffer = (UChar *)clip_a;
	ret = PVMP4SetAudioConfig(&ext, mem, 1, 44100, 2, MP4AUDIO_AAC_LC);
	printk("[setcfg] PVMP4SetAudioConfig(44100 Hz, 2 ch, AAC-LC) = %d: %d Hz, %d samples per "
	       "channel\n",
	       ret, (int)ext.samplingRate, ext.frameLength);
	ret = decode_raw(&ext, mem, clip_a, frames_a, 0, 64, &st);
	printk("[setcfg] raw frames, last PVMP4AudioDecodeFrame = %d\n", ret);
	printk("[setcfg] %u frames, CRC-32 %08x, same as the ADTS decode (%08x): %s\n", st.frames,
	       st.crc, adts_crc_a, st.crc == adts_crc_a ? "yes" : "NO");

	/* values the function does not take */
	ret = PVMP4SetAudioConfig(&ext, mem, 1, 44100, 3, MP4AUDIO_AAC_LC);
	printk("[setcfg] 3 channels                  = %d\n", ret);
	ret = PVMP4SetAudioConfig(&ext, mem, 1, 44100, 2, MP4AUDIO_CELP);
	printk("[setcfg] object type CELP            = %d\n", ret);
	free(mem);
}

/* decode frames 0..32 of clip_a, return the CRC-32 of the PCM of frames 30..32 */
static void decode_to_32(void *mem, bool reset_at_30, uint32_t crc[3])
{
	tPVMP4AudioDecoderExternal ext;
	struct pcm_stat st = {0};

	setup_ext(&ext, 2);
	PVMP4AudioDecoderInitLibrary(&ext, mem);
	ext.pInputBuffer = (UChar *)asc_a;
	ext.inputBufferCurrentLength = sizeof(asc_a);
	ext.inputBufferUsedLength = 0;
	ext.remainderBits = 0;
	PVMP4AudioDecoderConfig(&ext, mem);
	decode_raw(&ext, mem, clip_a, frames_a, 0, 30, &st);
	if (reset_at_30) {
		PVMP4AudioDecoderResetBuffer(mem);
	}
	for (int i = 0; i < 3; i++) {
		memset(&st, 0, sizeof(st));
		decode_raw(&ext, mem, clip_a, frames_a, 30 + i, 1, &st);
		crc[i] = st.crc;
	}
}

static void demo_reset_buffer(void)
{
	void *mem = new_decoder();
	uint32_t cont[3], reset[3];

	if (mem == NULL) {
		return;
	}

	/* the overlap of the previous frame is part of the output: clear it before frame 30 */
	decode_to_32(mem, false, cont);
	decode_to_32(mem, true, reset);
	for (int i = 0; i < 3; i++) {
		printk("[reset] frame %d: CRC-32 %08x without reset, %08x with reset before frame 30 "
		       "(%s)\n",
		       30 + i, cont[i], reset[i], cont[i] == reset[i] ? "same" : "different");
	}
	free(mem);
}

static void demo_disable_aac_plus(void)
{
	tPVMP4AudioDecoderExternal ext;
	struct pcm_stat st = {0};
	void *mem = new_decoder();
	int ret;

	if (mem == NULL) {
		return;
	}
	setup_ext(&ext, 2);
	ext.aacPlusEnabled = true;
	PVMP4AudioDecoderInitLibrary(&ext, mem);
	printk("[plus] after PVMP4AudioDecoderInitLibrary: aacPlusEnabled %d\n", ext.aacPlusEnabled);
	PVMP4AudioDecoderDisableAacPlus(&ext, mem);
	printk("[plus] after PVMP4AudioDecoderDisableAacPlus: aacPlusEnabled %d\n",
	       ext.aacPlusEnabled);

	ext.pInputBuffer = (UChar *)asc_a;
	ext.inputBufferCurrentLength = sizeof(asc_a);
	ext.inputBufferUsedLength = 0;
	ext.remainderBits = 0;
	ret = PVMP4AudioDecoderConfig(&ext, mem);
	printk("[plus] PVMP4AudioDecoderConfig = %d, aacPlusUpsamplingFactor %d, %d Hz\n", ret,
	       (int)ext.aacPlusUpsamplingFactor, (int)ext.samplingRate);
	ret = decode_raw(&ext, mem, clip_a, frames_a, 0, 8, &st);
	printk("[plus] 8 frames decoded = %d, %d Hz\n", ret, (int)ext.samplingRate);
	free(mem);
}

/* decode clip_a PLAY_LOOPS times and play the PCM on the headphone output */
static void demo_play(void)
{
	struct i2s_config cfg = {
		.word_size = 16,
		.channels = 2,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
		.frame_clk_freq = 44100,
		.mem_slab = &play_slab,
		.block_size = PLAY_BLOCK,
		.timeout = 2000,
	};
	audio_property_value_t vol = {.vol = 140};
	tPVMP4AudioDecoderExternal ext;
	void *mem = new_decoder();
	int written = 0, write_errors = 0, underruns = 0, queued = 0, ret;
	bool running = false;
	int64_t t0;

	if (mem == NULL || !device_is_ready(codec_i2s) || !device_is_ready(codec_ctl)) {
		printk("[play] device not ready\n");
		free(mem);
		return;
	}
	ret = i2s_configure(codec_i2s, I2S_DIR_TX, &cfg);
	printk("[play] i2s_configure(44100 Hz, 16 bit, 2 channels) = %d\n", ret);
	audio_codec_start_output(codec_ctl);
	audio_codec_set_property(codec_ctl, AUDIO_PROPERTY_OUTPUT_VOLUME, AUDIO_CHANNEL_ALL, vol);

	t0 = k_uptime_get();
	for (int loop = 0; loop < PLAY_LOOPS; loop++) {
		setup_ext(&ext, 2);
		PVMP4AudioDecoderInitLibrary(&ext, mem);
		ext.pInputBuffer = (UChar *)clip_a;
		ext.inputBufferCurrentLength = sizeof(clip_a);
		while (ext.inputBufferUsedLength + 7 < (int)sizeof(clip_a)) {
			void *blk;

			ext.pOutputBuffer = pcm;
			ext.pOutputBuffer_plus = pcm + 2048;
			ret = PVMP4AudioDecodeFrame(&ext, mem);
			if (ret != MP4AUDEC_SUCCESS) {
				printk("[play] decode error %d\n", ret);
				goto out;
			}
			if (k_mem_slab_alloc(&play_slab, &blk, K_SECONDS(2)) != 0) {
				printk("[play] no free block\n");
				goto out;
			}
			memcpy(blk, pcm, PLAY_BLOCK);
			ret = i2s_write(codec_i2s, blk, PLAY_BLOCK);
			if (ret == -EIO) {
				/* the stream ran dry: start it again */
				underruns++;
				running = false;
				queued = 0;
				k_mem_slab_free(&play_slab, blk);
				i2s_trigger(codec_i2s, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
				continue;
			} else if (ret != 0) {
				write_errors++;
				k_mem_slab_free(&play_slab, blk);
				goto out;
			}
			written++;
			if (!running && ++queued >= PLAY_PREFILL) {
				ret = i2s_trigger(codec_i2s, I2S_DIR_TX, I2S_TRIGGER_START);
				printk("[play] i2s_trigger(START) after %d blocks = %d\n", queued, ret);
				running = (ret == 0);
			}
		}
	}
out:
	ret = i2s_trigger(codec_i2s, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
	k_sleep(K_MSEC(300));
	audio_codec_stop_output(codec_ctl);
	printk("[play] i2s_trigger(DRAIN) = %d\n", ret);
	printk("[play] %d blocks written (%d x %d frames), %d i2s_write errors, %d underruns, "
	       "%lld ms since the first block (the audio is %d ms long)\n",
	       written, PLAY_LOOPS, 64, write_errors, underruns, k_uptime_get() - t0,
	       PLAY_LOOPS * 64 * 1024 * 1000 / 44100);
	free(mem);
}

int main(void)
{
	int na = index_adts(clip_a, sizeof(clip_a), frames_a);
	int nb = index_adts(clip_b, sizeof(clip_b), frames_b);

	printk("AAC decoder demo: clip_a %u bytes, %d ADTS frames; clip_b %u bytes, %d ADTS frames\n",
	       (unsigned int)sizeof(clip_a), na, (unsigned int)sizeof(clip_b), nb);
	demo_get_mem_requirements();
	demo_init_library();
	demo_decode_frame();
	demo_decode_errors();
	demo_config();
	demo_set_audio_config();
	demo_reset_buffer();
	demo_disable_aac_plus();
	demo_play();
	printk("done\n");
	k_sleep(K_FOREVER);

	return 0;
}
