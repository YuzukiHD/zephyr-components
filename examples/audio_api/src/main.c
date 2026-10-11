/*
 * Calls the functions of <zephyr/drivers/i2s.h>, <zephyr/audio/codec.h> and <zephyr/drivers/i2s_sunxi.h> on the
 * on-chip audio codec of the YuzukiNeko (a 440 Hz tone on the headphone output, the level of
 * the microphone input), then runs a loopback on the I2S0 and S/PDIF controllers.
 */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/i2s_sunxi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* the digital side of the codec is an I2S device, the analog side an audio codec device */
static const struct device *const codec_i2s = DEVICE_DT_GET(DT_NODELABEL(audio_codec));
static const struct device *const analog = DEVICE_DT_GET(DT_NODELABEL(codec_analog));
static const struct device *const i2s0 = DEVICE_DT_GET(DT_NODELABEL(i2s0));
static const struct device *const owa = DEVICE_DT_GET(DT_NODELABEL(owa));

#define RATE		48000
#define FRAMES		(RATE / 50)				/* 20 ms blocks */
#define BLOCK_SIZE	(FRAMES * 2 * sizeof(int16_t))
#define BLOCKS		6
#define TONE_HZ		440

K_MEM_SLAB_DEFINE_STATIC(slab, BLOCK_SIZE, BLOCKS, 4);

static const char *err(int ret)
{
	switch (ret) {
	case 0:		return "0";
	case -EINVAL:	return "-EINVAL";
	case -EIO:	return "-EIO";
	case -EAGAIN:	return "-EAGAIN";
	case -ENOSYS:	return "-ENOSYS";
	case -ENOTSUP:	return "-ENOTSUP";
	case -ENOMEM:	return "-ENOMEM";
	default:	return "?";
	}
}

/* a stereo 16 bit configuration of the codec, a field changed here and there by the examples */
static struct i2s_config base_cfg(void)
{
	return (struct i2s_config){
		.word_size = 16,
		.channels = 2,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
		.frame_clk_freq = RATE,
		.mem_slab = &slab,
		.block_size = BLOCK_SIZE,
		.timeout = 1000,
	};
}

/* One period of a 440 Hz sine in 16 bit, filled into stereo blocks. */
static int16_t table[RATE / TONE_HZ + 1];
static uint32_t phase;

static void make_table(void)
{
	uint32_t period = RATE / TONE_HZ;

	for (uint32_t i = 0; i < period; i++) {
		/* Bhaskara's sine approximation on each half wave, good to 0.2 percent */
		double x = (double)(i % (period / 2)) / (period / 2) * 3.14159265358979;
		double y = 16.0 * x * (3.14159265358979 - x) /
			   (5.0 * 3.14159265358979 * 3.14159265358979 - 4.0 * x * (3.14159265358979 - x));

		table[i] = (int16_t)(12000.0 * (i < period / 2 ? y : -y));
	}
}

static void fill_tone(int16_t *out)
{
	uint32_t period = RATE / TONE_HZ;

	for (int i = 0; i < FRAMES; i++) {
		out[2 * i] = table[phase];
		out[2 * i + 1] = table[phase];
		phase = (phase + 1) % period;
	}
}

/* i2s_configure: the format of a stream. A stream is configured before it starts, in the states
 * not-ready and ready only. The codec plays 8, 12, 16, 24, 32, 48, 96 and 192 kHz and the 44.1 kHz
 * family, 16, 24 and 32 bit words, mono or stereo for playback and mono for capture.
 */
static void demo_i2s_configure(void)
{
	struct i2s_config cfg;
	int ret;

	cfg = base_cfg();
	ret = i2s_configure(codec_i2s, I2S_DIR_TX, &cfg);
	printk("[configure] 48000 Hz, 16 bit, stereo            = %s\n", err(ret));

	cfg = base_cfg();
	cfg.frame_clk_freq = 44100;
	ret = i2s_configure(codec_i2s, I2S_DIR_TX, &cfg);
	printk("[configure] 44100 Hz                            = %s\n", err(ret));

	cfg = base_cfg();
	cfg.frame_clk_freq = 8000;
	cfg.channels = 1;
	ret = i2s_configure(codec_i2s, I2S_DIR_TX, &cfg);
	printk("[configure] 8000 Hz, mono                       = %s\n", err(ret));

	cfg = base_cfg();
	cfg.word_size = 24;
	ret = i2s_configure(codec_i2s, I2S_DIR_TX, &cfg);
	printk("[configure] 24 bit words                        = %s\n", err(ret));

	cfg = base_cfg();
	cfg.frame_clk_freq = 50000;
	ret = i2s_configure(codec_i2s, I2S_DIR_TX, &cfg);
	printk("[configure] 50000 Hz (not a rate of the codec)  = %s\n", err(ret));

	cfg = base_cfg();
	cfg.word_size = 8;
	ret = i2s_configure(codec_i2s, I2S_DIR_TX, &cfg);
	printk("[configure] 8 bit words                         = %s\n", err(ret));

	cfg = base_cfg();
	cfg.channels = 3;
	ret = i2s_configure(codec_i2s, I2S_DIR_TX, &cfg);
	printk("[configure] 3 channels                          = %s\n", err(ret));

	cfg = base_cfg();
	cfg.channels = 2;
	ret = i2s_configure(codec_i2s, I2S_DIR_RX, &cfg);
	printk("[configure] 2 channels for capture              = %s\n", err(ret));

	cfg = base_cfg();
	cfg.block_size = BLOCK_SIZE - 2;
	ret = i2s_configure(codec_i2s, I2S_DIR_TX, &cfg);
	printk("[configure] block size not a multiple of 4      = %s\n", err(ret));

	cfg = base_cfg();
	cfg.mem_slab = NULL;
	ret = i2s_configure(codec_i2s, I2S_DIR_TX, &cfg);
	printk("[configure] no memory slab                      = %s\n", err(ret));

	cfg = base_cfg();
	cfg.block_size = BLOCK_SIZE * 2;
	ret = i2s_configure(codec_i2s, I2S_DIR_TX, &cfg);
	printk("[configure] block larger than the slab blocks   = %s\n", err(ret));

	cfg = base_cfg();
	ret = i2s_configure(codec_i2s, I2S_DIR_BOTH, &cfg);
	printk("[configure] I2S_DIR_BOTH                        = %s\n", err(ret));
}

/* i2s_config_get: the configuration of a stream, NULL while it is not configured */
static void demo_i2s_config_get(void)
{
	struct i2s_config cfg = base_cfg();
	const struct i2s_config *got;

	cfg.frame_clk_freq = 0;			/* frame_clk_freq 0 releases the stream */
	i2s_configure(codec_i2s, I2S_DIR_TX, &cfg);
	got = i2s_config_get(codec_i2s, I2S_DIR_TX);
	printk("[config_get] after releasing the stream        = %p\n", got);

	cfg = base_cfg();
	i2s_configure(codec_i2s, I2S_DIR_TX, &cfg);
	got = i2s_config_get(codec_i2s, I2S_DIR_TX);
	printk("[config_get] after configuring                 = %u Hz, %u bit, %u channels, block %u\n",
	       got->frame_clk_freq, got->word_size, got->channels, (unsigned int)got->block_size);
}

/* audio_codec_configure: the stream format and the clocks belong to the I2S device, so this call
 * only checks its argument
 */
static void demo_audio_codec_configure(void)
{
	struct audio_codec_cfg cfg = { 0 };
	int ret = audio_codec_configure(analog, &cfg);

	printk("[codec_configure] audio_codec_configure         = %s\n", err(ret));
}

/* audio_codec_set_property: output volume 0..255 of one or both channels, mute, and the same for the input */
static void demo_audio_codec_set_property(void)
{
	audio_property_value_t v;
	int ret;

	v.vol = 130;
	ret = audio_codec_set_property(analog, AUDIO_PROPERTY_OUTPUT_VOLUME, AUDIO_CHANNEL_ALL, v);
	printk("[property] output volume 130, both channels     = %s\n", err(ret));

	v.vol = 200;
	ret = audio_codec_set_property(analog, AUDIO_PROPERTY_OUTPUT_VOLUME, AUDIO_CHANNEL_FRONT_LEFT, v);
	printk("[property] output volume 200, left only         = %s\n", err(ret));

	v.vol = 256;
	ret = audio_codec_set_property(analog, AUDIO_PROPERTY_OUTPUT_VOLUME, AUDIO_CHANNEL_ALL, v);
	printk("[property] output volume 256                    = %s\n", err(ret));

	v.mute = true;
	ret = audio_codec_set_property(analog, AUDIO_PROPERTY_OUTPUT_MUTE, AUDIO_CHANNEL_FRONT_RIGHT, v);
	printk("[property] mute the right channel               = %s\n", err(ret));

	v.mute = false;
	ret = audio_codec_set_property(analog, AUDIO_PROPERTY_OUTPUT_MUTE, AUDIO_CHANNEL_ALL, v);
	printk("[property] unmute both channels                 = %s\n", err(ret));

	v.vol = 160;
	ret = audio_codec_set_property(analog, AUDIO_PROPERTY_INPUT_VOLUME, AUDIO_CHANNEL_ALL, v);
	printk("[property] input volume 160                     = %s\n", err(ret));

	v.mute = false;
	ret = audio_codec_set_property(analog, AUDIO_PROPERTY_INPUT_MUTE, AUDIO_CHANNEL_ALL, v);
	printk("[property] input unmuted                        = %s\n", err(ret));

	v.vol = 130;
	ret = audio_codec_set_property(analog, AUDIO_PROPERTY_OUTPUT_VOLUME, AUDIO_CHANNEL_ALL, v);
}

/* audio_codec_apply_properties: every property is written as soon as it is set, so there is nothing to apply */
static void demo_audio_codec_apply_properties(void)
{
	int ret = audio_codec_apply_properties(analog);

	printk("[apply] audio_codec_apply_properties            = %s\n", err(ret));
}

/* audio_codec_start_output: power up the headphone driver (and enable the speaker amplifier on pa-gpios) */
static void demo_audio_codec_start_output(void)
{
	int64_t t0 = k_uptime_get();

	audio_codec_start_output(analog);
	printk("[start_output] headphone driver and amplifier on, %lld ms\n", k_uptime_get() - t0);
}

/* i2s_write: queue a block for playback. The block comes from the memory slab of the configuration,
 * is no longer than block_size, and is freed by the driver when it has been played.
 */
static void demo_i2s_write(void)
{
	void *block;
	int ret;

	for (int i = 0; i < 3; i++) {
		k_mem_slab_alloc(&slab, &block, K_FOREVER);
		fill_tone(block);
		ret = i2s_write(codec_i2s, block, BLOCK_SIZE);
		printk("[write] block %d of %u bytes                       = %s\n", i, (unsigned int)BLOCK_SIZE,
		       err(ret));
	}

	k_mem_slab_alloc(&slab, &block, K_FOREVER);
	ret = i2s_write(codec_i2s, block, BLOCK_SIZE + 4);
	printk("[write] a block longer than block_size          = %s\n", err(ret));
	k_mem_slab_free(&slab, block);
}

/* i2s_trigger START: begin the DMA transfer of the blocks that are queued */
static void demo_i2s_trigger_start(void)
{
	int ret = i2s_trigger(codec_i2s, I2S_DIR_TX, I2S_TRIGGER_START);

	printk("[trigger] I2S_TRIGGER_START                     = %s\n", err(ret));

	ret = i2s_trigger(codec_i2s, I2S_DIR_TX, I2S_TRIGGER_START);
	printk("[trigger] START again while running             = %s\n", err(ret));
}

/* keep the stream fed for a while: a block every 20 ms, and measure the real frame rate */
static void play_tone(int seconds)
{
	int64_t t0 = k_uptime_get(), mark = 0;
	int blocks = 0, mark_blocks = 0;

	while (k_uptime_get() - t0 < seconds * 1000) {
		void *block;

		if (k_mem_slab_alloc(&slab, &block, K_MSEC(1000)) != 0) {
			printk("[play] no block\n");
			return;
		}
		fill_tone(block);
		if (i2s_write(codec_i2s, block, BLOCK_SIZE) != 0) {
			k_mem_slab_free(&slab, block);
			printk("[play] write failed after %d blocks\n", blocks);
			return;
		}
		blocks++;
		if (mark == 0 && k_uptime_get() - t0 > 1000) {
			mark = k_uptime_get();
			mark_blocks = blocks;
		}
	}
	printk("[play] %d blocks in %d s, steady state %d Hz\n", blocks, seconds,
	       (int)((blocks - mark_blocks) * FRAMES * 1000LL / (k_uptime_get() - mark)));
}

/* i2s_sunxi_codec_tx_position: how many blocks the DMA has played, and the cycle counter at the last block */
static void demo_tx_position(void)
{
	uint32_t blocks = 0, cycle = 0;
	int ret = i2s_sunxi_codec_tx_position(codec_i2s, &blocks, &cycle);

	printk("[position] i2s_sunxi_codec_tx_position          = %s: %u blocks played\n", err(ret), blocks);
}

/* i2s_trigger DRAIN: play what is queued, then stop. STOP stops after the current block, DROP at once and throws away what is queued. */
static void demo_i2s_trigger_drain(void)
{
	int ret = i2s_trigger(codec_i2s, I2S_DIR_TX, I2S_TRIGGER_DRAIN);

	printk("[trigger] I2S_TRIGGER_DRAIN                     = %s\n", err(ret));
	k_msleep(300);
}

/* audio_codec_stop_output: power the headphone driver down again */
static void demo_audio_codec_stop_output(void)
{
	audio_codec_stop_output(analog);
	printk("[stop_output] headphone driver and amplifier off\n");
}

/* i2s_trigger with a stream that is not running */
static void demo_i2s_trigger_errors(void)
{
	int ret;

	ret = i2s_trigger(codec_i2s, I2S_DIR_TX, I2S_TRIGGER_STOP);
	printk("[trigger] STOP while not running                = %s\n", err(ret));

	ret = i2s_trigger(codec_i2s, I2S_DIR_TX, I2S_TRIGGER_DROP);
	printk("[trigger] I2S_TRIGGER_DROP                      = %s\n", err(ret));

	ret = i2s_trigger(codec_i2s, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
	printk("[trigger] PREPARE while not in the error state  = %s\n", err(ret));
}

/* audio_codec_route_input: choose what the ADC listens to: 1 microphone, 2 FM input, 3 line input */
static void demo_audio_codec_route_input(void)
{
	int ret;

	ret = audio_codec_route_input(analog, AUDIO_CHANNEL_ALL, 1);
	printk("[route] input 1 (microphone)                    = %s\n", err(ret));

	ret = audio_codec_route_input(analog, AUDIO_CHANNEL_ALL, 4);
	printk("[route] input 4                                 = %s\n", err(ret));
}

/* i2s_read: take a captured block out of the receive queue. The block belongs to the caller
 * and goes back to the memory slab when it has been looked at.
 */
K_MEM_SLAB_DEFINE_STATIC(rx_slab, RATE / 5 * sizeof(int16_t), 6, 4);

static void demo_i2s_read(void)
{
	struct i2s_config rx = base_cfg();
	int ret;

	rx.channels = 1;
	rx.block_size = RATE / 5 * sizeof(int16_t);			/* 200 ms */
	rx.mem_slab = &rx_slab;
	ret = i2s_configure(codec_i2s, I2S_DIR_RX, &rx);
	printk("[read] configure the capture stream             = %s\n", err(ret));
	ret = i2s_trigger(codec_i2s, I2S_DIR_RX, I2S_TRIGGER_START);
	printk("[read] start the capture                        = %s\n", err(ret));

	for (int n = 0; n < 5 && ret == 0; n++) {
		void *block;
		size_t size;
		int32_t min = 32767, max = -32768;
		int64_t sum = 0;
		int16_t *p;

		ret = i2s_read(codec_i2s, &block, &size);
		if (ret != 0) {
			printk("[read] i2s_read                                 = %s\n", err(ret));
			break;
		}
		p = block;
		for (size_t i = 0; i < size / 2; i++) {
			min = MIN(min, p[i]);
			max = MAX(max, p[i]);
			sum += p[i];
		}
		printk("[read] block %d: %zu bytes, min %d, max %d, mean %d\n", n, size, min, max,
		       (int)(sum / (int)(size / 2)));
		k_mem_slab_free(&rx_slab, block);
	}
	i2s_trigger(codec_i2s, I2S_DIR_RX, I2S_TRIGGER_DROP);
}

/* audio_codec functions the analog front end does not implement */
static void demo_audio_codec_unsupported(void)
{
	int ret;

	ret = audio_codec_route_output(analog, AUDIO_CHANNEL_ALL, 0);
	printk("[unsupported] audio_codec_route_output          = %s\n", err(ret));

	ret = audio_codec_clear_errors(analog);
	printk("[unsupported] audio_codec_clear_errors          = %s\n", err(ret));

	ret = audio_codec_register_error_callback(analog, NULL);
	printk("[unsupported] audio_codec_register_error_callback = %s\n", err(ret));
}

/* i2s_configure with frame_clk_freq 0: release the stream and give its DMA ring back to the heap */
static void demo_i2s_release(void)
{
	struct i2s_config cfg = base_cfg();
	int ret;

	cfg.frame_clk_freq = 0;
	ret = i2s_configure(codec_i2s, I2S_DIR_TX, &cfg);
	printk("[release] playback stream                       = %s\n", err(ret));
	ret = i2s_configure(codec_i2s, I2S_DIR_RX, &cfg);
	printk("[release] capture stream                        = %s\n", err(ret));
}

/*
 * The same API on the I2S0 controller and on the S/PDIF transmitter and receiver, with the
 * internal loopback: the receiver gets what the transmitter sends. A counting pattern, 100 blocks,
 * and the received frames are compared with the sent ones.
 */
#define LB_FRAMES	480
#define LB_BLOCK	(LB_FRAMES * 2 * sizeof(int16_t))

K_MEM_SLAB_DEFINE_STATIC(lb_tx_slab, LB_BLOCK, 8, 4);
K_MEM_SLAB_DEFINE_STATIC(lb_rx_slab, LB_BLOCK, 8, 4);

static void loopback(const struct device *dev, const char *name)
{
	struct i2s_config cfg = {
		.word_size = 16,
		.channels = 2,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER | I2S_OPT_LOOPBACK,
		.frame_clk_freq = RATE,
		.block_size = LB_BLOCK,
		.timeout = 1000,
	};
	uint16_t counter = 0;
	uint32_t frames = 0, errors = 0;
	int expect = -1, ret;

	if (!device_is_ready(dev)) {
		printk("[%s] not ready\n", name);
		return;
	}
	cfg.mem_slab = &lb_tx_slab;
	ret = i2s_configure(dev, I2S_DIR_TX, &cfg);
	printk("[%s] configure the transmitter                  = %s\n", name, err(ret));
	cfg.mem_slab = &lb_rx_slab;
	ret |= i2s_configure(dev, I2S_DIR_RX, &cfg);
	printk("[%s] configure the receiver                     = %s\n", name, err(ret));
	if (ret != 0) {
		return;
	}

	for (int i = 0; i < 7; i++) {
		void *block;
		int16_t *p;

		k_mem_slab_alloc(&lb_tx_slab, &block, K_FOREVER);
		p = block;
		for (int f = 0; f < LB_FRAMES; f++) {
			p[2 * f] = (int16_t)counter;
			p[2 * f + 1] = (int16_t)~counter;
			counter++;
		}
		i2s_write(dev, block, LB_BLOCK);
	}
	ret = i2s_trigger(dev, I2S_DIR_RX, I2S_TRIGGER_START);
	ret |= i2s_trigger(dev, I2S_DIR_TX, I2S_TRIGGER_START);
	printk("[%s] start                                      = %s\n", name, err(ret));

	for (int b = 0; b < 100 && ret == 0; b++) {
		void *block;
		size_t size;
		const int16_t *p;
		int16_t *q;

		if (k_mem_slab_alloc(&lb_tx_slab, &block, K_MSEC(500)) != 0) {
			break;
		}
		q = block;
		for (int f = 0; f < LB_FRAMES; f++) {
			q[2 * f] = (int16_t)counter;
			q[2 * f + 1] = (int16_t)~counter;
			counter++;
		}
		if (i2s_write(dev, block, LB_BLOCK) != 0) {
			break;
		}
		ret = i2s_read(dev, &block, &size);
		if (ret != 0) {
			printk("[%s] i2s_read                                   = %s\n", name, err(ret));
			break;
		}
		p = block;
		for (size_t i = 0; i < size / 4; i++) {
			uint16_t l = (uint16_t)p[2 * i], r = (uint16_t)p[2 * i + 1];

			if (expect < 0) {
				if (r == (uint16_t)~l && l != 0U) {
					expect = l;	/* the first frame of the pattern */
				} else {
					continue;
				}
			}
			if (l != (uint16_t)expect || r != (uint16_t)~expect) {
				errors++;
				if (r == (uint16_t)~l) {
					expect = l;
				}
			}
			expect = (expect + 1) & 0xffff;
			frames++;
		}
		k_mem_slab_free(&lb_rx_slab, block);
	}
	i2s_trigger(dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
	i2s_trigger(dev, I2S_DIR_RX, I2S_TRIGGER_DROP);
	printk("[%s] %u frames compared, %u mismatches: %s\n", name, frames, errors,
	       errors == 0U && frames > 0U ? "PASS" : "FAIL");
}

static void demo_i2s0_loopback(void)
{
	loopback(i2s0, "i2s0");
}

static void demo_owa_loopback(void)
{
	loopback(owa, "owa");
}

int main(void)
{
	if (!device_is_ready(codec_i2s) || !device_is_ready(analog)) {
		printk("codec not ready\n");
		return 0;
	}
	make_table();

	demo_i2s_configure();
	demo_i2s_config_get();
	demo_audio_codec_configure();
	demo_audio_codec_set_property();
	demo_audio_codec_apply_properties();
	demo_audio_codec_start_output();
	demo_i2s_write();
	demo_i2s_trigger_start();
	play_tone(3);
	demo_tx_position();
	demo_i2s_trigger_drain();
	demo_audio_codec_stop_output();
	demo_i2s_trigger_errors();
	demo_audio_codec_route_input();
	demo_i2s_read();
	demo_audio_codec_unsupported();
	demo_i2s_release();
	demo_owa_loopback();
	demo_i2s0_loopback();

	printk("audio_api done\n");
	return 0;
}
