/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MIRACAST_INTERNAL_H_
#define MIRACAST_INTERNAL_H_

#include <miracast/miracast.h>

#define MC_RTSP_MAX 8192
#define MC_URI_MAX 512
#define MC_AUDIO_MAX (32 * 1024)

enum mc_request {
	MC_NONE, MC_OPTIONS, MC_SETUP, MC_PLAY, MC_PAUSE, MC_TEARDOWN, MC_IDR, MC_KEEPALIVE
};

struct mc_pes {
	uint8_t *data;
	size_t len, cap, expected;
	int64_t pts;
	uint16_t pid;
	uint8_t cc, type;
	bool cc_valid, active, damaged, discontinuity;
};

struct mc_psi {
	uint8_t data[1024];
	size_t len, expected;
	uint8_t cc;
	bool cc_valid;
};

struct miracast_session {
	struct miracast_config config;
	struct miracast_ops ops;
	enum miracast_state state;
	char rx[MC_RTSP_MAX + 1];
	size_t rx_len;
	char uri[MC_URI_MAX], session[128];
	uint32_t cseq, pending_seq;
	uint32_t timeout_ms;
	uint16_t server_rtp, server_rtcp;
	enum mc_request pending;
	bool options_sent, negotiated;
	uint16_t pmt_pid, rtp_seq;
	uint16_t program;
	uint32_t ssrc;
	bool rtp_valid;
	bool need_idr;
	struct mc_psi pat, pmt;
	struct mc_pes video, audio;
};

void mc_state(struct miracast_session *s, enum miracast_state state);
void mc_media_reset(struct miracast_session *s);

#endif
