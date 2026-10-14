/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "core/settings.h"
#include "player/player.h"

#define FILE_PATH	CONFIG_BILI_CACHE_DIR "/settings.cfg"

static int volume = 70;
static int brightness = 200;

int settings_volume(void)
{
	return volume;
}

int settings_brightness(void)
{
	return brightness;
}

void settings_set_volume(int percent)
{
	volume = CLAMP(percent, 0, 100);
	player_set_volume(volume);
}

void settings_set_brightness(int value)
{
	brightness = CLAMP(value, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
	display_set_brightness(DEVICE_DT_GET(DT_CHOSEN(zephyr_display)), (uint8_t)brightness);
}

void settings_load(void)
{
	struct fs_file_t f;
	char buf[96];
	ssize_t n;

	fs_file_t_init(&f);
	if (fs_open(&f, FILE_PATH, FS_O_READ) != 0) {
		return;
	}
	n = fs_read(&f, buf, sizeof(buf) - 1);
	fs_close(&f);
	if (n <= 0) {
		return;
	}
	buf[n] = '\0';
	for (char *line = buf; line != NULL && *line != '\0';) {
		char *nl = strchr(line, '\n');

		if (nl != NULL) {
			*nl++ = '\0';
		}
		if (strncmp(line, "volume=", 7) == 0) {
			settings_set_volume(atoi(line + 7));
		} else if (strncmp(line, "brightness=", 11) == 0) {
			settings_set_brightness(atoi(line + 11));
		}
		line = nl;
	}
	printk("settings: volume %d, brightness %d\n", volume, brightness);
}

void settings_save(void)
{
	struct fs_file_t f;
	char buf[64];
	int n = snprintf(buf, sizeof(buf), "volume=%d\nbrightness=%d\n", volume, brightness);

	fs_mkdir(CONFIG_BILI_CACHE_DIR);
	fs_file_t_init(&f);
	if (fs_open(&f, FILE_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC) != 0) {
		printk("settings: %s cannot be written\n", FILE_PATH);
		return;
	}
	if (fs_write(&f, buf, n) != n) {
		printk("settings: write failed\n");
	}
	fs_close(&f);
}
