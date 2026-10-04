/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * A thread asks the engine for the mixed sound effects block by block and
 * hands them to the codec; the codec consumes at a fixed rate, which paces it.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <doom_zephyr/doom.h>

#include "app.h"

#define BLOCK_FRAMES	512
#define BLOCK_BYTES	(BLOCK_FRAMES * 2 * sizeof(int16_t))
#define SLAB_BLOCKS	4
#define PREFILL		2

static const struct device *const i2s = DEVICE_DT_GET(DT_NODELABEL(audio_codec));
static const struct device *const ctl = DEVICE_DT_GET(DT_NODELABEL(codec_analog));

K_MEM_SLAB_DEFINE_STATIC(slab, BLOCK_BYTES, SLAB_BLOCKS, 4);
K_THREAD_STACK_DEFINE(audio_stack, 4096);
static struct k_thread thread;

static unsigned int rate;
static volatile unsigned int underruns, slab_waits;

static void audio_main(void *a, void *b, void *c)
{
	struct i2s_config cfg = {
		.word_size = 16,
		.channels = 2,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
		.frame_clk_freq = rate,
		.mem_slab = &slab,
		.block_size = BLOCK_BYTES,
		.timeout = 2000,
	};
	bool running = false;
	int queued = 0;

	if (i2s_configure(i2s, I2S_DIR_TX, &cfg) != 0) {
		printk("audio: i2s_configure failed\n");
		return;
	}
	audio_codec_start_output(ctl);

	while (true) {
		void *blk;
		int ret;

		if (k_mem_slab_alloc(&slab, &blk, K_NO_WAIT) != 0) {
			slab_waits++;
			if (k_mem_slab_alloc(&slab, &blk, K_SECONDS(2)) != 0) {
				continue;
			}
		}
		doom_audio_read(blk, BLOCK_FRAMES);
		ret = i2s_write(i2s, blk, BLOCK_BYTES);
		if (ret == -EIO) {
			/* the stream ran dry: start it again */
			underruns++;
			running = false;
			queued = 0;
			k_mem_slab_free(&slab, blk);
			i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
			continue;
		} else if (ret != 0) {
			k_mem_slab_free(&slab, blk);
			continue;
		}
		if (!running && ++queued >= PREFILL &&
		    i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_START) == 0) {
			running = true;
		}
	}
}

int audio_init(unsigned int out_rate)
{
	if (!device_is_ready(i2s) || !device_is_ready(ctl)) {
		return -ENODEV;
	}
	rate = out_rate;
	k_thread_create(&thread, audio_stack, K_THREAD_STACK_SIZEOF(audio_stack), audio_main, NULL, NULL, NULL,
			3, 0, K_NO_WAIT);
	k_thread_name_set(&thread, "audio");

	return 0;
}

unsigned int audio_underruns(void)
{
	return underruns;
}
