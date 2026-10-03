/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <stdarg.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/gba/core.h>
#include <mgba/gb/core.h>
#include <mgba/gba/interface.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gba/gba.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/audio-resampler.h>
#include <mgba-util/vfs.h>

#include <mgba_zephyr/gba.h>

LOG_MODULE_REGISTER(gba, LOG_LEVEL_INF);

#define MAX_ROM_BYTES (CONFIG_MGBA_MAX_ROM_SIZE_MB * 1024 * 1024)
/* the largest save memory of a cartridge is 128 KiB (flash) */
#define SAVE_BYTES    (128 * 1024)

static struct {
	struct mCore *core;
	void *rom;
	uint16_t *frame;
	struct VFile *save_vf;
	const char *save_path;
	uint32_t save_crc;
	struct mAudioBuffer out;
	struct mAudioResampler rs;
	unsigned int out_rate;
	bool is_gb;
	bool own_frame;
} g;

/* the core is chatty about guest behaviour; only real errors reach the console */
static void core_log(struct mLogger *logger, int category, enum mLogLevel level,
		     const char *fmt, va_list args)
{
	if (level & (mLOG_FATAL | mLOG_ERROR)) {
		printk("mgba: %s: ", mLogCategoryName(category));
		vprintk(fmt, args);
		printk("\n");
	}
}

static struct mLogger core_logger = {.log = core_log};

static int read_file(const char *path, void **data, size_t *len)
{
	struct fs_dirent st;
	struct fs_file_t f;
	uint8_t *buf;
	size_t done = 0;
	int ret;

	ret = fs_stat(path, &st);
	if (ret < 0) {
		return ret;
	}
	if (st.size == 0 || st.size > MAX_ROM_BYTES) {
		return -EFBIG;
	}
	buf = malloc(st.size);
	if (buf == NULL) {
		return -ENOMEM;
	}
	fs_file_t_init(&f);
	ret = fs_open(&f, path, FS_O_READ);
	if (ret < 0) {
		free(buf);
		return ret;
	}
	while (done < st.size) {
		ssize_t n = fs_read(&f, buf + done, MIN(st.size - done, 64 * 1024));

		if (n <= 0) {
			ret = n < 0 ? n : -EIO;
			break;
		}
		done += n;
	}
	fs_close(&f);
	if (done != st.size) {
		free(buf);
		return ret;
	}
	*data = buf;
	*len = st.size;

	return 0;
}

static int write_file(const char *path, const void *data, size_t len)
{
	struct fs_file_t f;
	int ret;

	fs_file_t_init(&f);
	ret = fs_open(&f, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret < 0) {
		return ret;
	}
	ret = fs_write(&f, data, len);
	fs_close(&f);

	return ret == (int)len ? 0 : (ret < 0 ? ret : -EIO);
}

int gba_open(const char *rom_path, const char *save_path, unsigned int out_rate)
{
	void *sav = NULL;
	size_t rom_len, sav_len = 0;
	int ret;

	if (g.core != NULL) {
		return -EALREADY;
	}
	ret = read_file(rom_path, &g.rom, &rom_len);
	if (ret < 0) {
		LOG_ERR("read %s: %d", rom_path, ret);
		return ret;
	}
	LOG_INF("ROM %s, %u bytes", rom_path, (unsigned int)rom_len);

	mLogSetDefaultLogger(&core_logger);
	{
		size_t n = strlen(rom_path);
		bool gb = n > 3 && (strcmp(rom_path + n - 3, ".gb") == 0 ||
				    strcmp(rom_path + n - 3, ".GB") == 0 ||
				    (n > 4 && (strcmp(rom_path + n - 4, ".gbc") == 0 ||
					       strcmp(rom_path + n - 4, ".GBC") == 0)));

		g.is_gb = gb;
		g.core = gb ? GBCoreCreate() : GBACoreCreate();
	}
	if (g.core != NULL) {
		mCoreInitConfig(g.core, NULL);
	}
	if (g.core == NULL || !g.core->init(g.core)) {
		ret = -ENOMEM;
		goto fail;
	}
	g.own_frame = true;
	g.frame = aligned_alloc(64, GBA_WIDTH * GBA_HEIGHT * sizeof(uint16_t));
	if (g.frame == NULL) {
		ret = -ENOMEM;
		goto fail;
	}
	g.core->setVideoBuffer(g.core, g.frame, GBA_WIDTH);

	if (!g.core->loadROM(g.core, VFileFromConstMemory(g.rom, rom_len))) {
		LOG_ERR("not a Game Boy (Advance) ROM");
		ret = -EINVAL;
		goto fail;
	}
	if (save_path != NULL) {
		if (read_file(save_path, &sav, &sav_len) < 0) {
			sav = NULL;
			sav_len = 0;
		}
		g.save_path = save_path;
		g.save_vf = VFileMemChunk(sav, sav_len);
		/* what is on the card now: written back only when the game changes it */
		g.save_crc = sav_len > 0 ? crc32_ieee(sav, sav_len) : 0;
		free(sav);
		g.core->loadSave(g.core, g.save_vf);
	}
	g.core->reset(g.core);

	/* the core's own audio buffer is filled by the emulation, we resample it */
	g.out_rate = out_rate;
	mAudioBufferInit(&g.out, 4096, 2);
	mAudioResamplerInit(&g.rs, mINTERPOLATOR_COSINE);
	mAudioResamplerSetSource(&g.rs, g.core->getAudioBuffer(g.core),
				 g.core->audioSampleRate(g.core), true);
	mAudioResamplerSetDestination(&g.rs, &g.out, out_rate);

	return 0;
fail:
	gba_close();
	return ret;
}

void gba_close(void)
{
	if (g.core != NULL) {
		gba_save_flush();
		g.core->unloadROM(g.core);
		g.core->deinit(g.core);
		mCoreConfigDeinit(&g.core->config);
		mAudioResamplerDeinit(&g.rs);
		mAudioBufferDeinit(&g.out);
	}
	free(g.rom);
	if (g.own_frame) {
		free(g.frame);
	}
	memset(&g, 0, sizeof(g));
}

void gba_set_frame_buffer(uint16_t *buf)
{
	if (g.own_frame) {
		free(g.frame);
		g.own_frame = false;
	}
	g.frame = buf;
	g.core->setVideoBuffer(g.core, buf, GBA_WIDTH);
}

void gba_set_frameskip(int skip)
{
	if (g.is_gb) {
		((struct GB *)g.core->board)->video.frameskip = skip ? 1 : 0;
	} else {
		((struct GBA *)g.core->board)->video.frameskip = skip ? 1 : 0;
	}
}

int gba_run_frame(void)
{
	/* the frame is drawn when the skip counter has run out at its start */
	int drawn = g.is_gb ? ((struct GB *)g.core->board)->video.frameskipCounter <= 0
			    : ((struct GBA *)g.core->board)->video.frameskipCounter <= 0;

	g.core->runFrame(g.core);
	/* a game can change the sound resolution, the source rate follows it */
	g.rs.sourceRate = g.core->audioSampleRate(g.core);
	mAudioResamplerProcess(&g.rs);

	return drawn;
}

const uint16_t *gba_frame(unsigned int *width, unsigned int *height)
{
	g.core->currentVideoSize(g.core, width, height);

	return g.frame;
}

void gba_set_keys(uint32_t keys)
{
	g.core->setKeys(g.core, keys);
}

size_t gba_audio_read(int16_t *out, size_t frames)
{
	return mAudioBufferRead(&g.out, out, frames);
}

size_t gba_audio_available(void)
{
	return mAudioBufferAvailable(&g.out);
}

int gba_save_flush(void)
{
	uint8_t *mem;
	ssize_t len;
	uint32_t crc;

	if (g.save_vf == NULL) {
		return 0;
	}
	len = g.save_vf->size(g.save_vf);
	if (len <= 0 || len > SAVE_BYTES) {
		return 0;
	}
	mem = g.save_vf->map(g.save_vf, len, MAP_READ);
	if (mem == NULL) {
		return 0;
	}
	crc = crc32_ieee(mem, len);
	if (crc == g.save_crc) {
		g.save_vf->unmap(g.save_vf, mem, len);
		return 0;
	}
	if (write_file(g.save_path, mem, len) == 0) {
		g.save_crc = crc;
		LOG_INF("save written, %d bytes", (int)len);
		g.save_vf->unmap(g.save_vf, mem, len);
		return 1;
	}
	LOG_WRN("save write failed");
	g.save_vf->unmap(g.save_vf, mem, len);

	return 0;
}
