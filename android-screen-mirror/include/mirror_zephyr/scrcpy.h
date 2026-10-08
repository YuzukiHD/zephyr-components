/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Client of the scrcpy server: pushes the server to the phone, starts it, and gives the H.264
 * packets of the screen and an input channel. The version of the server and of this client have
 * to be equal.
 */

#ifndef MIRROR_ZEPHYR_SCRCPY_H_
#define MIRROR_ZEPHYR_SCRCPY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <mirror_zephyr/adb.h>

#ifdef __cplusplus
extern "C" {
#endif

struct scrcpy_config {
	const char *host;
	uint16_t port;
	/** Connect to the TLS service of wireless debugging (the key has to be paired) */
	bool tls;
	const char *key_path;
	/** Local file of the server jar; NULL when it is on the phone already */
	const char *server_file;
	/** Version of the server, as the server wants it ("3.3.1") */
	const char *version;
	unsigned int max_size;
	unsigned int max_fps;
	unsigned int bit_rate;
	/** Reports what happens (NULL for nothing) */
	void (*status)(const char *text);
};

struct scrcpy {
	struct adb_conn *adb;
	struct adb_stream *shell;
	struct adb_stream *video;
	struct adb_stream *control;
	char device_name[65];
	/** Size of the picture the server starts with */
	uint16_t width;
	uint16_t height;
};

/** Packet flags */
#define SCRCPY_PACKET_CONFIG	BIT(0)
#define SCRCPY_PACKET_KEY	BIT(1)

enum scrcpy_touch_action {
	SCRCPY_TOUCH_DOWN = 0,
	SCRCPY_TOUCH_UP = 1,
	SCRCPY_TOUCH_MOVE = 2,
};

struct scrcpy_touch {
	uint8_t action;
	uint8_t id;
	/** Position in the picture of the phone */
	int32_t x;
	int32_t y;
};

/** Android key codes the client sends */
#define SCRCPY_KEY_HOME		3
#define SCRCPY_KEY_BACK		4
#define SCRCPY_KEY_APP_SWITCH	187

int scrcpy_start(struct scrcpy *s, const struct scrcpy_config *cfg);
void scrcpy_stop(struct scrcpy *s);
/** Makes scrcpy_read_packet() of another thread return -ECONNRESET; scrcpy_stop() still follows */
void scrcpy_abort(struct scrcpy *s);

/**
 * @brief Read the next H.264 packet (Annex B)
 *
 * @param size in: capacity of @p buf, out: bytes of the packet
 * @retval 0 a packet
 * @retval -EMSGSIZE the packet is larger than the buffer (it is skipped)
 * @retval -ECONNRESET the stream ended
 */
int scrcpy_read_packet(struct scrcpy *s, uint8_t *buf, size_t *size, int64_t *pts_us,
		       unsigned int *flags);

/** Number of video bytes queued */
size_t scrcpy_video_backlog(struct scrcpy *s);

/** @brief Send touch events as one message; @p w and @p h are the size of the picture */
int scrcpy_send_touch(struct scrcpy *s, const struct scrcpy_touch *t, unsigned int count,
		      uint16_t w, uint16_t h);

/** @brief Press or release a key */
int scrcpy_send_key(struct scrcpy *s, bool down, uint32_t keycode);

#ifdef __cplusplus
}
#endif

#endif
