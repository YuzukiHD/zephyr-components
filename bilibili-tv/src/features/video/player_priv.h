/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * What the parts of the player share: the requests, the files of the session, the state of the
 * threads. Only the files player*.c include it.
 */

#ifndef NEKOBOX_FEATURES_VIDEO_PLAYER_PRIV_H_
#define NEKOBOX_FEATURES_VIDEO_PLAYER_PRIV_H_

#include <errno.h>
#include <pvmp4audiodecoder_api.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/i2s_sunxi.h>
#include <zephyr/drivers/vdec.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/mp4.h>
#include <zephyr/sys/printk.h>

#include "features/video/player.h"


/* room for the compressed pictures waiting for the decoder; what a stream asks for in the memory budget */
#ifndef CONFIG_NEKOBOX_VIDEO_STREAM_KB
#define CONFIG_NEKOBOX_VIDEO_STREAM_KB	1024
#endif

#define AAC_FRAMES	1024
#define PCM_BLOCK	(AAC_FRAMES * 2 * sizeof(int16_t))
#define PCM_BLOCKS	12
#define PREFILL		6
/*
 * Pictures later than this behind the sound are not shown. Dropping one does not make the
 * decoder faster; a wider limit (8 frames) was tried and left the picture a quarter of a second
 * behind the sound for the rest of the film, which is worse than a missing picture.
 */
#define DROP_US		(2 * 33367)
/* pictures are put up this much before their time, the display waits for a refresh */
#define LEAD_US		8000
#define VOLUME_MAX_REG	200

/*
 * A file with a read window: the samples of a track are small and spread over the file, a seek in
 * FAT walks the cluster chain, so a window is read at a time and the samples are cut out of it.
 */
struct reader {
	struct fs_file_t f;
	uint8_t *win;
	size_t win_size;
	uint64_t win_off;
	size_t win_len;
	/* a file that is still being written: bytes the handle knows of (UINT64_MAX: the file is complete) */
	uint64_t known;
};

/* A request for a session: where to start and whether to play */
struct request {
	uint32_t seq;
	int64_t pos_us;
	bool play;
};

enum wait_result {
	WAIT_ON_TIME,
	WAIT_LATE,
	WAIT_ABORT,
};

extern struct mp4 pl_mp4;
extern struct reader pl_vfile, pl_afile;
extern bool pl_opened;
extern struct k_thread pl_audio_thread, pl_video_thread;
extern struct k_msgq disp_q, done_q;
extern struct k_sem disp_flushed;
extern const struct device *const pl_codec_i2s, *const pl_codec_ctl, *const pl_vdec_dev, *const pl_disp;
extern struct k_spinlock pl_req_lock;
extern struct request pl_req;
extern volatile uint32_t pl_pos_ms, pl_video_ready_seq;
extern volatile bool pl_quit;
extern volatile enum player_state pl_state;
extern volatile int pl_volume_pct;
extern volatile bool pl_audio_present, pl_audio_running, pl_audio_eof;
extern volatile int64_t pl_audio_base_us, pl_audio_end_us, pl_audio_end_ms;
extern bool pl_raw_mode;
extern uint64_t pl_raw_size, pl_raw_duration_us;
extern uint32_t pl_raw_period_us;
extern uint16_t pl_raw_w, pl_raw_h;

/* requests, files, volume (player.c) */
struct request pl_req_get(void);
void pl_req_post(int64_t pos_us, bool play);
bool pl_pos_publish(uint32_t seq, uint32_t ms);
bool pl_req_changed(uint32_t seq);
int pl_file_read(void *ctx, uint64_t off, void *buf, size_t len);
void pl_reader_close(struct reader *r);
int pl_reader_open(struct reader *r, const char *path, size_t win_size);
void pl_volume_apply(void);

/* the sound (player_audio.c) */
int64_t pl_audio_clock_us(void);
void pl_audio_main(void *a, void *b, void *c);

/* the picture of an MP4 (player_video.c) */
void pl_video_main(void *a, void *b, void *c);
void pl_release_done(void);
enum wait_result pl_wait_for(uint32_t seq, int64_t pts_us, int64_t start_us, int64_t start_ms);
int pl_avcc_parameter_sets(const uint8_t *e, uint32_t len);

/* H.264 elementary streams (player_raw.c) */
int pl_raw_probe(uint64_t file_size);
void pl_raw_main(void *a, void *b, void *c);
bool pl_is_raw_name(const char *path);

#endif /* NEKOBOX_FEATURES_VIDEO_PLAYER_PRIV_H_ */
