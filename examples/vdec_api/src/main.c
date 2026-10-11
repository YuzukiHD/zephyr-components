/*
 * Calls every function of <zephyr/drivers/vdec.h> with JPEG, PNG, MJPEG and
 * H.264 data and prints what each call returns.
 */
#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/vdec.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>

#include "test_jpeg.h"
#include "test_png.h"
#include "test_h264.h"
#include "test_mjpeg.h"

static const struct device *const dev = DEVICE_DT_GET(DT_NODELABEL(ve));

static const char *err(int ret)
{
	switch (ret) {
	case 0:		return "0";
	case -EAGAIN:	return "-EAGAIN";
	case -EBUSY:	return "-EBUSY";
	case -ENODATA:	return "-ENODATA";
	case -EINVAL:	return "-EINVAL";
	case -ENOTSUP:	return "-ENOTSUP";
	case -ENOMEM:	return "-ENOMEM";
	case -EIO:	return "-EIO";
	default:	return "?";
	}
}

static void show_frame(const char *tag, const struct vdec_frame *f)
{
	printk("%s frame: format %d, %ux%u, stride %u/%u, pts %lld\n", tag, f->format, f->width,
	       f->height, f->stride[0], f->stride[1], f->pts);
}

static uint32_t luma_crc(const struct vdec_frame *f)
{
	uint32_t crc = 0;

	for (int y = 0; y < f->height; y++) {
		crc = crc32_ieee_update(crc, f->plane[0] + y * f->stride[0], f->width);
	}

	return crc;
}

/* ---- JPEG: one picture --------------------------------------------------------------- */

static void demo_jpeg(void)
{
	static const enum vdec_format formats[] = {VDEC_FORMAT_NV21, VDEC_FORMAT_NV12, VDEC_FORMAT_NV21};
	struct vdec_frame frame;
	int64_t t;
	int ret;

	for (int i = 0; i < ARRAY_SIZE(formats); i++) {
		t = k_uptime_get();
		ret = vdec_decode_image(dev, VDEC_CODEC_JPEG, test_jpeg, sizeof(test_jpeg),
					formats[i], &frame);
		printk("[jpeg] vdec_decode_image(format %d) = %s, %lld ms\n", formats[i], err(ret),
		       k_uptime_get() - t);
		if (ret == 0) {
			show_frame("[jpeg]", &frame);
			printk("[jpeg] luma crc %08x\n", luma_crc(&frame));
			vdec_frame_release(dev, &frame);
			printk("[jpeg] vdec_frame_release done\n");
		}
	}
}

/* ---- PNG: one picture ---------------------------------------------------------------- */

static void demo_png(void)
{
	struct vdec_frame frame;
	int64_t t = k_uptime_get();
	int ret;

	ret = vdec_decode_image(dev, VDEC_CODEC_PNG, test_png, sizeof(test_png),
				VDEC_FORMAT_RGBA8888, &frame);
	printk("[png] vdec_decode_image(RGBA8888) = %s, %lld ms\n", err(ret), k_uptime_get() - t);
	if (ret == 0) {
		const uint8_t *p = frame.plane[0];

		show_frame("[png]", &frame);
		printk("[png] first pixel bytes (A, B, G, R): %02x %02x %02x %02x\n", p[0], p[1],
		       p[2], p[3]);
		vdec_frame_release(dev, &frame);
		printk("[png] vdec_frame_release done\n");
	}
}

/* ---- errors of vdec_decode_image() --------------------------------------------------- */

static void demo_image_errors(void)
{
	static const uint8_t garbage[16] = {0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
					    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
	struct vdec_frame frame;
	int ret;

	ret = vdec_decode_image(dev, VDEC_CODEC_PNG, test_png, sizeof(test_png), VDEC_FORMAT_NV12,
				&frame);
	printk("[error] PNG as NV12             = %s\n", err(ret));
	ret = vdec_decode_image(dev, VDEC_CODEC_JPEG, test_jpeg, sizeof(test_jpeg),
				VDEC_FORMAT_RGBA8888, &frame);
	printk("[error] JPEG as RGBA8888        = %s\n", err(ret));
	ret = vdec_decode_image(dev, VDEC_CODEC_H264, test_jpeg, sizeof(test_jpeg),
				VDEC_FORMAT_NV12, &frame);
	printk("[error] H.264 to decode_image   = %s\n", err(ret));
	ret = vdec_decode_image(dev, VDEC_CODEC_JPEG, garbage, sizeof(garbage), VDEC_FORMAT_NV12,
				&frame);
	printk("[error] 16 bytes of 0x55 (JPEG) = %s\n", err(ret));
	ret = vdec_decode_image(dev, VDEC_CODEC_JPEG, test_jpeg, sizeof(test_jpeg) / 2,
				VDEC_FORMAT_NV12, &frame);
	printk("[error] half a JPEG             = %s\n", err(ret));
	if (ret == 0) {
		vdec_frame_release(dev, &frame);
	}
	/* the decoder must still work after the errors */
	ret = vdec_decode_image(dev, VDEC_CODEC_JPEG, test_jpeg, sizeof(test_jpeg),
				VDEC_FORMAT_NV12, &frame);
	printk("[error] a good JPEG afterwards  = %s\n", err(ret));
	if (ret == 0) {
		vdec_frame_release(dev, &frame);
	}
}

/* ---- vdec_stream_get_frame: MJPEG -------------------------------------------------- */

static void demo_stream_get_frame_mjpeg(void)
{
	const struct vdec_stream_config cfg = {
		.codec = VDEC_CODEC_JPEG,
		.format = VDEC_FORMAT_NV12,
		.width = 320,
		.height = 240,
	};
	struct vdec_stream *stream;
	struct vdec_frame frame;
	size_t used;
	int ret;

	ret = vdec_stream_open(dev, &cfg, &stream);
	printk("[mjpeg] vdec_stream_open = %s\n", err(ret));
	if (ret != 0) {
		return;
	}
	for (int i = 0; i < ARRAY_SIZE(test_mjpeg); i++) {
		ret = vdec_stream_feed(dev, stream, test_mjpeg[i].data, test_mjpeg[i].len, i * 100,
				       &used);
		printk("[mjpeg] feed #%d (%u bytes, pts %d) = %s, used %zu\n", i, test_mjpeg[i].len,
		       i * 100, err(ret), used);
		ret = vdec_stream_get_frame(dev, stream, &frame);
		printk("[mjpeg] get_frame = %s\n", err(ret));
		if (ret == 0) {
			show_frame("[mjpeg]", &frame);
			vdec_frame_release(dev, &frame);
		}
	}
	ret = vdec_stream_flush(dev, stream);
	printk("[mjpeg] flush = %s\n", err(ret));
	ret = vdec_stream_get_frame(dev, stream, &frame);
	printk("[mjpeg] get_frame after flush = %s\n", err(ret));
	vdec_stream_close(dev, stream);
	printk("[mjpeg] vdec_stream_close done\n");
}

/* ---- vdec_stream_get_frame: H.264 -------------------------------------------------- */

static size_t next_start_code(const uint8_t *d, size_t len, size_t from)
{
	for (size_t i = from; i + 4 <= len; i++) {
		if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 0 && d[i + 3] == 1) {
			return i;
		}
	}

	return len;
}

static void demo_stream_get_frame_h264(void)
{
	const struct vdec_stream_config cfg = {
		.codec = VDEC_CODEC_H264,
		.format = VDEC_FORMAT_NV12,
	};
	struct vdec_stream *stream;
	struct vdec_frame frame;
	size_t off = 0, used, nal_end;
	int frames = 0, calls = 0, nal = 0;
	bool flushed = false;
	int ret;

	ret = vdec_stream_open(dev, &cfg, &stream);
	printk("[h264] vdec_stream_open = %s\n", err(ret));
	if (ret != 0) {
		return;
	}
	while (true) {
		ret = vdec_stream_get_frame(dev, stream, &frame);
		if (calls++ < 12) {
			printk("[h264] get_frame = %s\n", err(ret));
		}
		if (ret == 0) {
			if (frames < 4) {
				show_frame("[h264]", &frame);
			}
			if (frames < ARRAY_SIZE(test_h264_crc)) {
				/* the same CRC the vdec h264 sample checks */
				uint32_t crc = 0;

				for (int y = 0; y < frame.height; y++) {
					crc = crc32_ieee_update(crc, frame.plane[0] +
							      y * frame.stride[0], frame.width);
				}
				for (int y = 0; y < frame.height / 2; y++) {
					crc = crc32_ieee_update(crc, frame.plane[1] +
							      y * frame.stride[1], frame.width);
				}
				if (crc != test_h264_crc[frames]) {
					printk("[h264] frame %d: bad crc\n", frames);
				}
			}
			frames++;
			vdec_frame_release(dev, &frame);
		} else if (ret == -ENODATA) {
			break;
		} else if (ret == -EAGAIN) {
			if (off < sizeof(test_h264)) {
				/* one NAL unit per call, so every call has a time stamp */
				nal_end = next_start_code(test_h264, sizeof(test_h264), off + 4);
				ret = vdec_stream_feed(dev, stream, test_h264 + off, nal_end - off,
						       nal, &used);
				if (nal < 6) {
					printk("[h264] feed NAL #%d (type %d, %zu bytes, pts %d) = %s, used %zu\n",
					       nal, test_h264[off + 4] & 0x1f, nal_end - off, nal,
					       err(ret), used);
				}
				if (ret == 0) {
					off += used;
					nal++;
				}
			} else if (!flushed) {
				ret = vdec_stream_flush(dev, stream);
				printk("[h264] flush = %s (after %d NAL units)\n", err(ret), nal);
				flushed = true;
			}
		} else {
			printk("[h264] get_frame failed: %s\n", err(ret));
			break;
		}
	}
	printk("[h264] %d frames decoded, all CRCs checked\n", frames);
	vdec_stream_close(dev, stream);
	printk("[h264] vdec_stream_close done\n");
}

/* ---- vdec_stream_get_frame: -EBUSY -------------------------------------------------- */

static void demo_hold(void)
{
	const struct vdec_stream_config cfg = {
		.codec = VDEC_CODEC_H264,
		.format = VDEC_FORMAT_NV12,
		.holding_frames = 2,
	};
	struct vdec_frame held[8], frame;
	struct vdec_stream *stream;
	size_t off = 0, used, nal_end;
	int n = 0, ret;

	ret = vdec_stream_open(dev, &cfg, &stream);
	printk("[hold] vdec_stream_open(holding_frames 2) = %s\n", err(ret));
	if (ret != 0) {
		return;
	}
	/* take frames and never give them back */
	while (n < ARRAY_SIZE(held)) {
		ret = vdec_stream_get_frame(dev, stream, &held[n]);
		if (ret == 0) {
			n++;
			continue;
		}
		if (ret != -EAGAIN || off >= sizeof(test_h264)) {
			break;
		}
		nal_end = next_start_code(test_h264, sizeof(test_h264), off + 4);
		if (vdec_stream_feed(dev, stream, test_h264 + off, nal_end - off, -1, &used) == 0) {
			off += used;
		}
	}
	printk("[hold] %d frames held, get_frame = %s\n", n, err(ret));
	for (int i = 0; i < n; i++) {
		vdec_frame_release(dev, &held[i]);
	}
	ret = vdec_stream_get_frame(dev, stream, &frame);
	printk("[hold] after releasing them, get_frame = %s\n", err(ret));
	if (ret == 0) {
		vdec_frame_release(dev, &frame);
	}
	vdec_stream_close(dev, stream);
	printk("[hold] vdec_stream_close done\n");
}

/* ---- vdec_frame_release ------------------------------------------------------------- */

static void demo_frame_release(void)
{
	struct vdec_frame frame;
	int ret;

	ret = vdec_decode_image(dev, VDEC_CODEC_JPEG, test_jpeg, sizeof(test_jpeg),
				VDEC_FORMAT_NV12, &frame);
	printk("[release] first decode = %s, plane %p\n", err(ret), frame.plane[0]);
	if (ret != 0) {
		return;
	}
	vdec_frame_release(dev, &frame);
	printk("[release] vdec_frame_release done\n");

	/* the decoder is free again: the next picture can be decoded at once */
	ret = vdec_decode_image(dev, VDEC_CODEC_JPEG, test_jpeg, sizeof(test_jpeg),
				VDEC_FORMAT_NV12, &frame);
	printk("[release] second decode = %s, plane %p\n", err(ret), frame.plane[0]);
	if (ret == 0) {
		vdec_frame_release(dev, &frame);
		printk("[release] vdec_frame_release done\n");
	}
}

/* ---- vdec_stream_open ---------------------------------------------------------------- */

static void open_and_close(const char *what, const struct vdec_stream_config *cfg)
{
	struct vdec_stream *stream;
	int ret = vdec_stream_open(dev, cfg, &stream);

	printk("[open] %-34s = %s\n", what, err(ret));
	if (ret == 0) {
		vdec_stream_close(dev, stream);
	}
}

static void demo_stream_open(void)
{
	const struct vdec_stream_config mjpeg = {
		.codec = VDEC_CODEC_JPEG,
		.format = VDEC_FORMAT_NV12,
		.width = 320,
		.height = 240,
	};
	const struct vdec_stream_config h264 = {
		.codec = VDEC_CODEC_H264,
		.format = VDEC_FORMAT_NV12,
	};
	const struct vdec_stream_config h264_big = {
		.codec = VDEC_CODEC_H264,
		.format = VDEC_FORMAT_NV12,
		.buffer_size = 1024 * 1024,
		.no_cache_ops = true,
		.holding_frames = 2,
	};
	const struct vdec_stream_config jpeg_no_size = {
		.codec = VDEC_CODEC_JPEG,
		.format = VDEC_FORMAT_NV12,
	};
	const struct vdec_stream_config png = {
		.codec = VDEC_CODEC_PNG,
		.format = VDEC_FORMAT_RGBA8888,
	};
	const struct vdec_stream_config h264_rgba = {
		.codec = VDEC_CODEC_H264,
		.format = VDEC_FORMAT_RGBA8888,
	};

	open_and_close("MJPEG 320x240", &mjpeg);
	open_and_close("H.264", &h264);
	open_and_close("H.264, 1 MiB buffer, hold 2", &h264_big);
	open_and_close("JPEG without width/height", &jpeg_no_size);
	open_and_close("PNG as a stream", &png);
	open_and_close("H.264 as RGBA8888", &h264_rgba);
}

/* ---- vdec_stream_feed ---------------------------------------------------------------- */

static void demo_stream_feed(void)
{
	const struct vdec_stream_config cfg = {
		.codec = VDEC_CODEC_H264,
		.format = VDEC_FORMAT_NV12,
	};
	static const uint8_t garbage[16] = {0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
					    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
	struct vdec_stream *stream;
	size_t used = 0;
	int ret;

	ret = vdec_stream_open(dev, &cfg, &stream);
	printk("[feed] vdec_stream_open = %s\n", err(ret));
	if (ret != 0) {
		return;
	}

	/* data with no NAL unit in it */
	ret = vdec_stream_feed(dev, stream, garbage, sizeof(garbage), -1, &used);
	printk("[feed] 16 bytes of 0x55        = %s, used %zu\n", err(ret), used);

	/* no data at all */
	ret = vdec_stream_feed(dev, stream, test_h264, 0, -1, &used);
	printk("[feed] 0 bytes                 = %s, used %zu\n", err(ret), used);

	/* the whole clip, in Annex B form, in one call */
	ret = vdec_stream_feed(dev, stream, test_h264, sizeof(test_h264), 0, &used);
	printk("[feed] whole clip (%zu bytes) = %s, used %zu\n", sizeof(test_h264), err(ret),
	       used);

	vdec_stream_close(dev, stream);
	printk("[feed] vdec_stream_close done\n");
}

/* ---- vdec_stream_flush --------------------------------------------------------------- */

static void demo_stream_flush(void)
{
	const struct vdec_stream_config cfg = {
		.codec = VDEC_CODEC_JPEG,
		.format = VDEC_FORMAT_NV12,
		.width = 320,
		.height = 240,
	};
	struct vdec_stream *stream;
	struct vdec_frame frame;
	size_t used;
	int taken = 0, ret;

	ret = vdec_stream_open(dev, &cfg, &stream);
	printk("[flush] vdec_stream_open = %s\n", err(ret));
	if (ret != 0) {
		return;
	}

	/* three pictures go in, none is taken out yet */
	for (int i = 0; i < 3; i++) {
		ret = vdec_stream_feed(dev, stream, test_mjpeg[i].data, test_mjpeg[i].len, i * 100,
				       &used);
		printk("[flush] feed #%d = %s\n", i, err(ret));
	}

	ret = vdec_stream_flush(dev, stream);
	printk("[flush] vdec_stream_flush = %s\n", err(ret));

	/* from now on get_frame hands out what is left, then returns -ENODATA */
	while ((ret = vdec_stream_get_frame(dev, stream, &frame)) == 0) {
		printk("[flush] get_frame = 0, pts %lld\n", frame.pts);
		vdec_frame_release(dev, &frame);
		taken++;
	}
	printk("[flush] get_frame = %s after %d frames\n", err(ret), taken);

	vdec_stream_close(dev, stream);
	printk("[flush] vdec_stream_close done\n");
}

/* ---- vdec_stream_close --------------------------------------------------------------- */

static void demo_stream_close(void)
{
	const struct vdec_stream_config mjpeg = {
		.codec = VDEC_CODEC_JPEG,
		.format = VDEC_FORMAT_NV12,
		.width = 320,
		.height = 240,
	};
	const struct vdec_stream_config h264 = {
		.codec = VDEC_CODEC_H264,
		.format = VDEC_FORMAT_NV12,
	};
	struct vdec_stream *stream;
	struct vdec_frame frame;
	size_t used;
	int ret;

	ret = vdec_stream_open(dev, &mjpeg, &stream);
	printk("[close] open the MJPEG stream = %s\n", err(ret));
	if (ret != 0) {
		return;
	}
	vdec_stream_feed(dev, stream, test_mjpeg[0].data, test_mjpeg[0].len, 0, &used);
	ret = vdec_stream_get_frame(dev, stream, &frame);
	printk("[close] get_frame = %s\n", err(ret));
	if (ret == 0) {
		/* every frame has to be released before the stream is closed */
		vdec_frame_release(dev, &frame);
	}
	vdec_stream_close(dev, stream);
	printk("[close] vdec_stream_close done\n");

	/* the decoder belongs to nobody now: another stream can be opened */
	ret = vdec_stream_open(dev, &h264, &stream);
	printk("[close] open an H.264 stream afterwards = %s\n", err(ret));
	if (ret == 0) {
		vdec_stream_close(dev, stream);
		printk("[close] vdec_stream_close done\n");
	}
}

int main(void)
{
	if (!device_is_ready(dev)) {
		printk("decoder not ready\n");
		return 0;
	}
	demo_jpeg();
	demo_png();
	demo_image_errors();
	demo_frame_release();
	demo_stream_open();
	demo_stream_feed();
	demo_stream_get_frame_mjpeg();
	demo_stream_get_frame_h264();
	demo_hold();
	demo_stream_flush();
	demo_stream_close();
	printk("vdec_api done\n");
	k_sleep(K_FOREVER);

	return 0;
}
