/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MIRACAST_H_
#define MIRACAST_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum miracast_state {
	MIRACAST_NEGOTIATING,
	MIRACAST_READY,
	MIRACAST_PLAYING,
	MIRACAST_PAUSED,
	MIRACAST_CLOSED,
};

enum miracast_codec {
	MIRACAST_H264,
	MIRACAST_LPCM,
	MIRACAST_AAC,
};

struct miracast_session;

struct miracast_config {
	uint16_t rtp_port;  /* Even UDP port, RTCP uses the following port. */
	uint32_t cea_modes; /* WFD CEA bitmap: 1 = 640x480p60, 0x20 = 720p30. 0 selects 1. */
	bool aac;          /* Also offer AAC stereo at 48 kHz; LPCM is always offered. */
	size_t max_video;  /* Largest video PES, 0 selects 256 KiB. */
};

struct miracast_ops {
	/** Send a complete RTSP message; return 0 or a negative errno. */
	int (*send)(void *ctx, const void *data, size_t len);
	/**
	 * One complete elementary PES payload, with a 90 kHz PTS or -1 when absent.
	 * Data is borrowed until this callback returns. Copy or queue it without blocking.
	 * LPCM includes its four-byte audio header; AAC is ADTS framed.
	 * A discontinuity means a packet was lost, or a stream changed. Negative return
	 * values drop this payload; -EAGAIN/-ENOBUFS permit the session to continue.
	 */
	int (*media)(void *ctx, enum miracast_codec codec, const void *data, size_t len,
		     int64_t pts, bool discontinuity);
	void (*state)(void *ctx, enum miracast_state state);
	void *ctx;
};

/** The core is caller driven and single threaded; feed complete network data to it. */
struct miracast_session *miracast_create(const struct miracast_config *config,
				       const struct miracast_ops *ops);
void miracast_destroy(struct miracast_session *session);
/** Arbitrarily split or coalesced RTSP/TCP bytes. */
int miracast_rtsp(struct miracast_session *session, const void *data, size_t len);
/** One RTP/MP2T datagram (payload type 33). Late/duplicate packets are ignored. */
int miracast_rtp(struct miracast_session *session, const void *data, size_t len);
/** Emit an unbounded final PES at a clean end of a test stream, never on packet loss. */
int miracast_flush(struct miracast_session *session);
int miracast_pause(struct miracast_session *session, bool paused);
int miracast_teardown(struct miracast_session *session);
int miracast_request_idr(struct miracast_session *session);
/** Send an empty GET_PARAMETER with the negotiated Session header. */
int miracast_keepalive(struct miracast_session *session);
enum miracast_state miracast_state(const struct miracast_session *session);

/**
 * Convert one WFD LPCM payload (including its four-byte header) to stereo S16LE.
 * Only 16-bit, 48 kHz stereo is advertised. Returns output bytes or a negative
 * errno. Output can overlap input. This is WFD LPCM, not Blu-ray LPCM framing.
 */
int miracast_lpcm_decode(const void *data, size_t len, void *pcm, size_t capacity);

/** Zephyr transport: one receiver at a time, after the IP link is established. */
struct miracast_receiver_config {
	const char *peer; /* Numeric IPv4 address of the source's RTSP server. */
	uint16_t rtsp_port; /* 0 = 7236. */
	struct miracast_config stream;
	/* Callbacks run on the network thread. Queue work; call stop from another thread. */
	struct miracast_ops ops; /* send is supplied by the transport; ctx is retained. */
};
int miracast_receiver_start(const struct miracast_receiver_config *config);
void miracast_receiver_stop(void);
bool miracast_receiver_running(void);
/** Thread-safe request for a new video keyframe after an asynchronous decoder error. */
void miracast_receiver_request_idr(void);

#ifdef __cplusplus
}
#endif
#endif
