/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * UPnP AV media renderer ("DLNA cast target"). The module speaks the protocol; the application
 * plays: it hands a table of callbacks to dlna_start() and the module turns the control requests
 * of the phone or PC into calls of that table.
 */

#ifndef DLNA_DLNA_H_
#define DLNA_DLNA_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Storage for a media address, including the terminating NUL. */
#define DLNA_URI_SIZE 4096

enum dlna_state {
	DLNA_NO_MEDIA,		/**< nothing was set */
	DLNA_STOPPED,
	DLNA_PLAYING,
	DLNA_PAUSED,
	DLNA_TRANSITIONING,	/**< the media is being fetched or opened */
	DLNA_ERROR,		/**< the media could not be played; reported as STOPPED + ERROR_OCCURRED */
};

/**
 * Callbacks of the player. All of them run in the threads of the module and must not block for
 * long: set_uri() only starts the work and returns, state() then reports DLNA_TRANSITIONING until
 * the media plays. A function that is not needed may be NULL; a request for it is refused.
 */
struct dlna_ops {
	/** New media; @p title is the title from the metadata or "". Replaces what is open. */
	int (*set_uri)(void *ctx, const char *uri, const char *title);
	/** Optional check of the selected resource's DIDL dimensions, before starting a download. */
	int (*check_resolution)(void *ctx, uint32_t width, uint32_t height);
	int (*play)(void *ctx);
	int (*pause)(void *ctx);
	int (*stop)(void *ctx);
	int (*seek)(void *ctx, uint32_t ms);
	enum dlna_state (*state)(void *ctx);
	uint32_t (*position_ms)(void *ctx);
	uint32_t (*duration_ms)(void *ctx);	/**< 0 when not known */
	int (*get_volume)(void *ctx);		/**< percent */
	int (*set_volume)(void *ctx, int percent);
	void *ctx;
};

struct dlna_config {
	const char *name;		/**< friendly name shown by the controller */
	const char *uuid;		/**< "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx", see dlna_uuid_from_id() */
	const struct dlna_ops *ops;	/**< must stay valid while running */
};

/** Starts the SSDP and the HTTP thread; they wait for an IPv4 address by themselves */
int dlna_start(const struct dlna_config *cfg);

/** Sends the byebye notices and stops both threads */
void dlna_stop(void);

bool dlna_running(void);

/** Makes a version 5 style name based UUID text from a device id (stable for a board) */
void dlna_uuid_from_id(const uint8_t *id, size_t len, char out[37]);

#endif /* DLNA_DLNA_H_ */
