/*
 * MP4 demultiplexer demo: the clip is linked into the firmware, mp4_open()
 * reads it through a callback, then every function of <zephyr/mp4.h> is shown.
 */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/mp4.h>
#include <zephyr/sys/printk.h>

static const uint8_t clip[] = {
#include "clip.inc"
};

static struct mp4 mp4;

/* Read callback: the file is an array in memory */
static int mem_read(void *ctx, uint64_t offset, void *buf, size_t len)
{
	const uint8_t *data = ctx;

	if (offset + len > sizeof(clip)) {
		return -EIO;
	}
	memcpy(buf, data + offset, len);

	return 0;
}

/* A read callback that always fails */
static int bad_read(void *ctx, uint64_t offset, void *buf, size_t len)
{
	ARG_UNUSED(ctx);
	ARG_UNUSED(offset);
	ARG_UNUSED(buf);
	ARG_UNUSED(len);

	return -EIO;
}

/* Track time units to milliseconds */
static int32_t to_ms(int64_t ticks, uint32_t timescale)
{
	return (int32_t)(ticks * 1000 / timescale);
}

static const char *codec_name(enum mp4_codec c)
{
	switch (c) {
	case MP4_CODEC_H264:
		return "H264";
	case MP4_CODEC_AAC:
		return "AAC";
	default:
		return "NONE";
	}
}

static void print_track(const char *name, const struct mp4_track *t)
{
	printk("%s: codec=%s timescale=%u duration=%u (%d ms) media_start=%d\n", name,
	       codec_name(t->codec), t->timescale, (uint32_t)t->duration,
	       to_ms(t->duration, t->timescale ? t->timescale : 1), (int32_t)t->media_start);
	printk("  samples=%u max_sample=%u chunks=%u stsc=%u stts=%u ctts=%u stss=%u\n",
	       t->sample_count, t->max_sample, t->chunk_count, t->stsc_count, t->stts_count,
	       t->ctts_count, t->stss_count);
	if (t->codec == MP4_CODEC_H264) {
		printk("  %ux%u avcC(%u bytes): profile_idc=%u level_idc=%u\n", t->width,
		       t->height, t->extra_len, t->extra[1], t->extra[3]);
	} else if (t->codec == MP4_CODEC_AAC) {
		printk("  %u ch %u Hz AudioSpecificConfig(%u bytes): %02x %02x\n", t->channels,
		       t->sample_rate, t->extra_len, t->extra[0], t->extra[1]);
	}
}

/* mp4_open(): parse the movie box of the clip */
static int demo_open(void)
{
	int ret = mp4_open(&mp4, mem_read, (void *)clip, sizeof(clip));

	if (ret != 0) {
		printk("mp4_open failed: %d\n", ret);
		return ret;
	}
	printk("file size %u bytes\n", (uint32_t)mp4.size);
	print_track("video", &mp4.video);
	print_track("audio", &mp4.audio);

	return 0;
}

/* mp4_open() on input it cannot use: the error codes */
static void demo_open_errors(void)
{
	struct mp4 m;
	int ret;

	/* too short to hold a box header: no movie box */
	ret = mp4_open(&m, mem_read, (void *)clip, 4);
	printk("size 4: ret=%d\n", ret);

	/* the first box (ftyp) claims to be longer than the file */
	ret = mp4_open(&m, mem_read, (void *)clip, 16);
	printk("size 16: ret=%d\n", ret);

	/* a read callback that fails: its error is passed through */
	ret = mp4_open(&m, bad_read, NULL, sizeof(clip));
	printk("bad_read: ret=%d\n", ret);

	/* not an MP4 at all: the 4 byte box size is 0x00000000 so it spans the file,
	 * but there is no moov in it
	 */
	static const uint8_t zeros[64];

	ret = mp4_open(&m, mem_read, (void *)zeros, sizeof(zeros));
	printk("zeros: ret=%d\n", ret);
}

/* mp4_close(): free the tables, the struct is zeroed and can be closed again */
static void demo_close(void)
{
	struct mp4 m;
	int ret = mp4_open(&m, mem_read, (void *)clip, sizeof(clip));

	if (ret != 0) {
		printk("mp4_open failed: %d\n", ret);
		return;
	}
	printk("before close: video sizes=%s samples=%u\n", m.video.sizes ? "set" : "NULL",
	       m.video.sample_count);
	mp4_close(&m);
	printk("after close: video sizes=%s samples=%u codec=%s\n",
	       m.video.sizes ? "set" : "NULL", m.video.sample_count, codec_name(m.video.codec));
	mp4_close(&m);
	printk("closed twice without trouble\n");
}

/* mp4_iter_init(): an iterator starts at the first sample of a track */
static void demo_iter_init(const struct mp4_track *t)
{
	struct mp4_iter it;
	int ret = mp4_iter_init(t, &it);

	if (ret != 0) {
		printk("mp4_iter_init: %d\n", ret);
		return;
	}
	printk("next=%u chunk=%u in_chunk=%u chunk_pos=%u stts_left=%u dts=%d\n", it.next,
	       it.chunk, it.in_chunk, (uint32_t)it.chunk_pos, it.stts_left, (int32_t)it.dts);
}

/* mp4_iter_init() on a track the file does not have */
static void demo_iter_init_errors(void)
{
	struct mp4 m;
	struct mp4_iter it;
	struct mp4_track none = {0};

	/* a track after mp4_close(), and one that was never filled in */
	mp4_open(&m, mem_read, (void *)clip, sizeof(clip));
	mp4_close(&m);
	printk("track closed: ret=%d\n", mp4_iter_init(&m.audio, &it));
	printk("empty track: ret=%d\n", mp4_iter_init(&none, &it));
	printk("NULL track: ret=%d\n", mp4_iter_init(NULL, &it));
}

/* mp4_next(): list the first samples of a track and read the first one */
static void demo_next(const struct mp4_track *t, const char *name, int count)
{
	struct mp4_iter it;
	struct mp4_sample s;
	uint8_t head[8];
	int ret;

	mp4_iter_init(t, &it);
	for (int i = 0; i < count; i++) {
		ret = mp4_next(t, &it, &s);
		if (ret != 0) {
			printk("mp4_next: %d\n", ret);
			return;
		}
		printk("%s #%u off=%u size=%u dts=%d pts=%d (%d ms) sync=%u\n", name, s.index,
		       (uint32_t)s.offset, s.size, (int32_t)s.dts, (int32_t)s.pts,
		       to_ms(s.pts - t->media_start, t->timescale), s.sync);
		if (i == 0) {
			ret = mp4.read(mp4.ctx, s.offset, head, sizeof(head));
			if (ret != 0) {
				printk("read sample: %d\n", ret);
				return;
			}
			printk("  first bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n", head[0],
			       head[1], head[2], head[3], head[4], head[5], head[6], head[7]);
		}
	}
}

/* mp4_next() until it returns -ENODATA: totals of a whole track */
static void demo_next_all(const struct mp4_track *t, const char *name)
{
	struct mp4_iter it;
	struct mp4_sample s = {0};
	uint32_t n = 0, sync = 0;
	uint64_t bytes = 0;
	int ret;

	mp4_iter_init(t, &it);
	while ((ret = mp4_next(t, &it, &s)) == 0) {
		n++;
		bytes += s.size;
		sync += s.sync;
	}
	printk("%s: ret=%d samples=%u sync=%u bytes=%u last dts=%d pts=%d\n", name, ret, n, sync,
	       (uint32_t)bytes, (int32_t)s.dts, (int32_t)s.pts);
}

/* mp4_seek_sync(): the key frame at or before a sample, then read on from there */
static void demo_seek_sync(const struct mp4_track *t, uint32_t index)
{
	struct mp4_iter it;
	struct mp4_sample s;
	int ret = mp4_seek_sync(t, &it, index);

	printk("seek sample %u: ret=%d, restart from sample %u\n", index, ret, it.next);
	for (int i = 0; i < 3 && mp4_next(t, &it, &s) == 0; i++) {
		printk("  #%u off=%u size=%u pts=%d (%d ms) sync=%u\n", s.index,
		       (uint32_t)s.offset, s.size, (int32_t)s.pts,
		       to_ms(s.pts - t->media_start, t->timescale), s.sync);
	}
}

/* mp4_seek_time(): the key frame to start from to show the frame at a playback time */
static void demo_seek_time(const struct mp4_track *t, int32_t target_ms)
{
	struct mp4_iter it;
	struct mp4_sample s;
	int64_t ticks = (int64_t)target_ms * t->timescale / 1000;
	int ret = mp4_seek_time(t, &it, ticks);

	printk("seek %d ms (%d ticks): ret=%d, restart from sample %u\n", target_ms,
	       (int32_t)ticks, ret, it.next);
	for (int i = 0; i < 2 && mp4_next(t, &it, &s) == 0; i++) {
		printk("  #%u off=%u size=%u pts=%d (%d ms, ffprobe pts %d) sync=%u\n", s.index,
		       (uint32_t)s.offset, s.size, (int32_t)s.pts,
		       to_ms(s.pts - t->media_start, t->timescale),
		       (int32_t)(s.pts - t->media_start), s.sync);
	}
}

int main(void)
{
	printk("\n== mp4_open ==\n");
	if (demo_open() != 0) {
		return 0;
	}
	printk("\n== mp4_open errors ==\n");
	demo_open_errors();
	printk("\n== mp4_close ==\n");
	demo_close();
	printk("\n== mp4_iter_init ==\n");
	demo_iter_init(&mp4.video);
	demo_iter_init_errors();
	printk("\n== mp4_next ==\n");
	demo_next(&mp4.video, "video", 8);
	demo_next(&mp4.audio, "audio", 4);
	demo_next_all(&mp4.video, "video");
	demo_next_all(&mp4.audio, "audio");
	printk("\n== mp4_seek_sync ==\n");
	demo_seek_sync(&mp4.video, 0);
	demo_seek_sync(&mp4.video, 47);
	demo_seek_sync(&mp4.video, 89);
	demo_seek_sync(&mp4.video, 1000);
	demo_seek_sync(&mp4.audio, 66);
	printk("\n== mp4_seek_time ==\n");
	demo_seek_time(&mp4.video, 0);
	demo_seek_time(&mp4.video, 1500);
	demo_seek_time(&mp4.video, 2000);
	demo_seek_time(&mp4.video, 2900);
	demo_seek_time(&mp4.video, 5000);
	demo_seek_time(&mp4.audio, 1500);
	mp4_close(&mp4);
	printk("\nmp4_api done\n");

	return 0;
}
