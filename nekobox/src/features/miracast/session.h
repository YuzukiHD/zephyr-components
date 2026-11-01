/* SPDX-License-Identifier: Apache-2.0 */
#ifndef NEKOBOX_MIRACAST_SESSION_H_
#define NEKOBOX_MIRACAST_SESSION_H_

enum mirror_state {
	MIRROR_OFF,
	MIRROR_PREPARING,
	MIRROR_STARTING,
	MIRROR_WAITING,
	MIRROR_CONNECTING,
	MIRROR_PLAYING,
	MIRROR_STOPPING,
	MIRROR_FAILED,
};

/* Session and player control belong to the UI thread. Radio operations run on a worker. */
void mirror_start(void);
void mirror_stop(void);
void mirror_tick(void);
enum mirror_state mirror_state(void);

#endif
