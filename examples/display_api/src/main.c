/*
 * Calls the functions of <zephyr/drivers/display.h> that the sunxi display driver implements, and the
 * three functions of <zephyr/drivers/display/display_sunxi.h> for the scaling video plane. The panel
 * is the one named by zephyr,display in the board file: the MIPI DSI panel of the carrier board.
 */
#include <errno.h>
#include <string.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static struct display_capabilities caps;

static const char *err(int ret)
{
	switch (ret) {
	case 0:		return "0";
	case -EINVAL:	return "-EINVAL";
	case -ENOTSUP:	return "-ENOTSUP";
	case -ENOSYS:	return "-ENOSYS";
	default:	return "?";
	}
}

/* RGB565 of an 8 bit colour */
#define RGB565(r, g, b)	((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* display_get_capabilities: the size of the frame buffer and its pixel format */
static void demo_get_capabilities(void)
{
	display_get_capabilities(disp, &caps);
	printk("[caps] %ux%u, supported pixel formats 0x%x, current 0x%x (RGB_565 is 0x%x), orientation %d\n",
	       caps.x_resolution, caps.y_resolution, caps.supported_pixel_formats,
	       caps.current_pixel_format, PIXEL_FORMAT_RGB_565, caps.current_orientation);
}

/* display_write: copy a rectangle of pixels into the frame buffer. Eight vertical colour bars over the
 * whole screen, one band of rows at a time.
 */
static uint16_t band[1024 * 16];

static void demo_write(void)
{
	static const uint16_t bars[8] = {
		RGB565(255, 255, 255), RGB565(255, 255, 0), RGB565(0, 255, 255), RGB565(0, 255, 0),
		RGB565(255, 0, 255), RGB565(255, 0, 0), RGB565(0, 0, 255), RGB565(0, 0, 0),
	};
	uint16_t w = caps.x_resolution, rows = 16;
	struct display_buffer_descriptor desc = {
		.buf_size = w * rows * 2,
		.width = w,
		.height = rows,
		.pitch = w,
	};
	int ret = 0;

	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < w; x++) {
			band[y * w + x] = bars[x * 8 / w];
		}
	}
	for (uint16_t y = 0; y < caps.y_resolution && ret == 0; y += rows) {
		ret = display_write(disp, 0, y, &desc, band);
	}
	printk("[write] colour bars over the whole %ux%u frame buffer = %s\n", w, caps.y_resolution,
	       err(ret));
}

/* display_write with a window: a 200x100 white block at (412, 334), with a pitch wider than the block */
static void demo_write_window(void)
{
	static uint16_t block[256 * 100];
	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(block),
		.width = 200,
		.height = 100,
		.pitch = 256,
	};
	int ret;

	for (int i = 0; i < 256 * 100; i++) {
		block[i] = 0xffff;
	}
	ret = display_write(disp, 412, 334, &desc, block);
	printk("[write] 200x100 block at (412,334), pitch 256         = %s\n", err(ret));

	desc.width = 300;
	ret = display_write(disp, 412, 334, &desc, block);
	printk("[write] a width larger than the pitch                 = %s\n", err(ret));

	desc.width = 200;
	ret = display_write(disp, caps.x_resolution - 100, 0, &desc, block);
	printk("[write] a block that sticks out on the right          = %s\n", err(ret));

	ret = display_write(disp, 0, caps.y_resolution - 50, &desc, block);
	printk("[write] a block that sticks out at the bottom         = %s\n", err(ret));
}

/* display_blanking_on and display_blanking_off: switch the picture off and on again */
static void demo_blanking_on(void)
{
	int ret = display_blanking_on(disp);

	printk("[blank] display_blanking_on                    = %s\n", err(ret));
	k_msleep(1000);
}

static void demo_blanking_off(void)
{
	int ret = display_blanking_off(disp);

	printk("[blank] display_blanking_off                   = %s\n", err(ret));
	k_msleep(1000);
}

/* display_set_brightness: the backlight, 0..255 */
static void demo_set_brightness(void)
{
	static const uint8_t levels[] = { 255, 128, 32, 0, 96 };

	for (int i = 0; i < ARRAY_SIZE(levels); i++) {
		int ret = display_set_brightness(disp, levels[i]);

		printk("[brightness] display_set_brightness(%3u)         = %s\n", levels[i], err(ret));
		k_msleep(700);
	}
}

/* display_read: the frame buffer of this driver is write only */
static void demo_read(void)
{
	uint8_t pixel[4];
	struct display_buffer_descriptor desc = { .buf_size = 4, .width = 1, .height = 1, .pitch = 1 };
	int ret = display_read(disp, 0, 0, &desc, pixel);

	printk("[unsupported] display_read                         = %s\n", err(ret));
}

/* display_set_pixel_format: the format is chosen at build time (CONFIG_DISPLAY_SUNXI_RGB565 or ARGB8888) */
static void demo_set_pixel_format(void)
{
	int ret = display_set_pixel_format(disp, PIXEL_FORMAT_ARGB_8888);

	printk("[unsupported] display_set_pixel_format             = %s\n", err(ret));
}

/* display_set_orientation: not implemented, rotate in software (or with the G2D) */
static void demo_set_orientation(void)
{
	int ret = display_set_orientation(disp, DISPLAY_ORIENTATION_ROTATED_90);

	printk("[unsupported] display_set_orientation              = %s\n", err(ret));
}

/* display_get_framebuffer: the driver does not hand out its frame buffer, it returns NULL */
static void demo_get_framebuffer(void)
{
	printk("[unsupported] display_get_framebuffer              = %p\n", display_get_framebuffer(disp));
}

/* display_sunxi_show_rgb: put a picture on the video plane under the frame buffer, scaled to fit the
 * screen. A 64x48 RGB565 picture of four coloured quarters, drawn on the screen four times as big as
 * the width of the panel allows.
 */
static uint16_t small[64 * 48] __aligned(64);

static void demo_show_rgb(void)
{
	struct display_sunxi_rgb img = {
		.data = small,
		.width = 64,
		.height = 48,
		.stride = 64 * 2,
		.xrgb8888 = false,
		.nonblock = false,
	};
	int ret;

	for (int y = 0; y < 48; y++) {
		for (int x = 0; x < 64; x++) {
			small[y * 64 + x] = y < 24 ? (x < 32 ? RGB565(255, 0, 0) : RGB565(0, 255, 0))
						   : (x < 32 ? RGB565(0, 0, 255) : RGB565(255, 255, 0));
		}
	}
	sys_cache_data_flush_range(small, sizeof(small));

	ret = display_sunxi_show_rgb(disp, &img);
	printk("[rgb] display_sunxi_show_rgb 64x48                 = %s\n", err(ret));
	k_msleep(2000);
}

/* display_sunxi_show_yuv: the same for a YCbCr 4:2:0 picture (NV12: a luma plane and an interleaved
 * chroma plane). A grey ramp from black to white, 128x96.
 */
static uint8_t luma[128 * 96] __aligned(64);
static uint8_t chroma[128 * 48] __aligned(64);

static void demo_show_yuv(void)
{
	struct display_sunxi_yuv img = {
		.y = luma,
		.uv = chroma,
		.nv21 = false,
		.width = 128,
		.height = 96,
		.stride_y = 128,
		.stride_uv = 128,
		.full_range = false,
		.bt709 = false,
		.nonblock = false,
	};
	int ret;

	for (int y = 0; y < 96; y++) {
		for (int x = 0; x < 128; x++) {
			luma[y * 128 + x] = 16 + x * 219 / 127;
		}
	}
	memset(chroma, 128, sizeof(chroma));		/* no colour */
	sys_cache_data_flush_range(luma, sizeof(luma));
	sys_cache_data_flush_range(chroma, sizeof(chroma));

	ret = display_sunxi_show_yuv(disp, &img);
	printk("[yuv] display_sunxi_show_yuv 128x96                = %s\n", err(ret));
	k_msleep(2000);
}

/* display_sunxi_hide_yuv: stop the video plane and bring the frame buffer back */
static void demo_hide_yuv(void)
{
	int ret = display_sunxi_hide_yuv(disp);

	printk("[hide] display_sunxi_hide_yuv                      = %s\n", err(ret));
	k_msleep(1000);
}

int main(void)
{
	if (!device_is_ready(disp)) {
		printk("display not ready\n");
		return 0;
	}

	demo_get_capabilities();
	demo_write();
	k_msleep(1500);
	demo_write_window();
	k_msleep(1500);
	demo_blanking_on();
	demo_blanking_off();
	demo_set_brightness();
	demo_read();
	demo_set_pixel_format();
	demo_set_orientation();
	demo_get_framebuffer();
	demo_show_rgb();
	demo_show_yuv();
	demo_hide_yuv();

	printk("display_api done\n");
	return 0;
}
