/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The sound of the player: AAC decoded in software, played by the codec, the master clock */

#include "player/player_priv.h"

K_MEM_SLAB_DEFINE_STATIC(pcm_slab, PCM_BLOCK, PCM_BLOCKS, 4);

/* Position of the sound in microseconds, -1 while it is not running */
int64_t pl_audio_clock_us(void)
{
	uint32_t blocks, cycle;

	if (!pl_audio_running) {
		return -1;
	}
	i2s_sunxi_codec_tx_position(pl_codec_i2s, &blocks, &cycle);

	return pl_audio_base_us + (int64_t)blocks * AAC_FRAMES * 1000000 / pl_mp4.audio.sample_rate +
	       (int64_t)k_cyc_to_us_floor64(k_cycle_get_32() - cycle);
}

/* Decodes and plays the session that starts at r.pos_us until the request changes or the file ends */
static void audio_session(const struct request *r, tPVMP4AudioDecoderExternal *ext, void *dec,
			  int16_t *pcm, uint8_t *buf)
{
	const struct mp4_track *t = &pl_mp4.audio;
	struct i2s_config cfg = {
		.word_size = 16,
		.channels = 2,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
		.frame_clk_freq = t->sample_rate,
		.mem_slab = &pcm_slab,
		.block_size = PCM_BLOCK,
		.timeout = 2000,
	};
	struct mp4_iter it;
	struct mp4_sample s;
	bool priming = true;
	int queued = 0, ret;

	if (i2s_configure(pl_codec_i2s, I2S_DIR_TX, &cfg) != 0) {
		printk("audio: i2s_configure failed\n");
		return;
	}
	if (mp4_seek_time(t, &it, r->pos_us * (int64_t)t->timescale / 1000000) < 0) {
		/* past the end */
		pl_audio_eof = true;
		return;
	}
	/* the decoder needs the sample before as well, its output is thrown away */
	if (it.next > 0U) {
		mp4_seek_sync(t, &it, it.next - 1U);
	}
	pl_audio_running = false;

	while (!pl_req_changed(r->seq) && mp4_next(t, &it, &s) == 0) {
		void *blk;

		if (pl_file_read(&pl_afile, s.offset, buf, s.size) != 0) {
			printk("audio: read error at sample %u\n", s.index);
			break;
		}
		ext->pInputBuffer = buf;
		ext->inputBufferCurrentLength = s.size;
		ext->inputBufferUsedLength = 0;
		ext->remainderBits = 0;
		ret = PVMP4AudioDecodeFrame(ext, dec);
		if (ret != MP4AUDEC_SUCCESS) {
			continue;
		}
		if (priming) {
			priming = false;
			pl_audio_base_us = (s.pts - t->media_start) * 1000000 / t->timescale +
					(int64_t)AAC_FRAMES * 1000000 / t->sample_rate;
			continue;
		}
		if (k_mem_slab_alloc(&pcm_slab, &blk, K_SECONDS(2)) != 0) {
			break;
		}
		memcpy(blk, pcm, PCM_BLOCK);
		ret = i2s_write(pl_codec_i2s, blk, PCM_BLOCK);
		if (ret == -EIO) {
			/* the stream ran dry: start it again where it was */
			int64_t at = pl_audio_clock_us();

			if (at >= 0) {
				pl_audio_base_us = at;
			}
			pl_audio_running = false;
			k_mem_slab_free(&pcm_slab, blk);
			i2s_trigger(pl_codec_i2s, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
			queued = 0;
			continue;
		} else if (ret != 0) {
			k_mem_slab_free(&pcm_slab, blk);
			break;
		}
		if (!pl_audio_running && ++queued >= PREFILL) {
			/* the video has to get from its key frame to the position first, the sound
			 * must not run away from it */
			while (!pl_req_changed(r->seq) && pl_video_ready_seq != r->seq) {
				k_msleep(1);
			}
			if (pl_req_changed(r->seq)) {
				break;
			}
			if (i2s_trigger(pl_codec_i2s, I2S_DIR_TX, I2S_TRIGGER_START) == 0) {
				pl_audio_running = true;
			}
		}
	}

	if (pl_req_changed(r->seq)) {
		pl_audio_running = false;
		i2s_trigger(pl_codec_i2s, I2S_DIR_TX, I2S_TRIGGER_DROP);
		return;
	}
	/* the end of the file: let what is queued play out */
	pl_audio_end_us = pl_audio_clock_us();
	pl_audio_end_ms = k_uptime_get();
	i2s_trigger(pl_codec_i2s, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
	k_sleep(K_MSEC(300));
	pl_audio_running = false;
	pl_audio_eof = true;
}

void pl_audio_main(void *a, void *b, void *c)
{
	const struct mp4_track *t = &pl_mp4.audio;
	tPVMP4AudioDecoderExternal ext = {0};
	void *dec = malloc(PVMP4AudioDecoderGetMemRequirements());
	int16_t *pcm = malloc(sizeof(int16_t) * AAC_FRAMES * 2 * 2);
	uint8_t *buf = malloc(t->max_sample);
	uint32_t done = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	ext.desiredChannels = 2;
	ext.outputFormat = OUTPUTFORMAT_16PCM_INTERLEAVED;
	ext.aacPlusEnabled = false;
	ext.pOutputBuffer = pcm;
	ext.pOutputBuffer_plus = pcm + AAC_FRAMES * 2;
	if (dec == NULL || pcm == NULL || buf == NULL ||
	    PVMP4AudioDecoderInitLibrary(&ext, dec) != 0) {
		printk("audio: cannot start the AAC decoder\n");
		pl_audio_eof = true;
		goto out;
	}
	/* the AudioSpecificConfig of the track tells the decoder the stream layout */
	ext.pInputBuffer = t->extra;
	ext.inputBufferCurrentLength = t->extra_len;
	ext.inputBufferUsedLength = 0;
	ext.remainderBits = 0;
	if (PVMP4AudioDecoderConfig(&ext, dec) != MP4AUDEC_SUCCESS) {
		printk("audio: bad AudioSpecificConfig\n");
		pl_audio_eof = true;
		goto out;
	}
	audio_codec_start_output(pl_codec_ctl);
	pl_volume_apply();

	while (!pl_quit) {
		struct request r = pl_req_get();

		if (r.seq == done) {
			k_msleep(5);
			continue;
		}
		done = r.seq;
		pl_audio_eof = false;
		pl_audio_running = false;
		if (r.play) {
			audio_session(&r, &ext, dec, pcm, buf);
		}
	}
	audio_codec_stop_output(pl_codec_ctl);
out:
	free(dec);
	free(pcm);
	free(buf);
}

