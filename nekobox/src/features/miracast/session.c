/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <aic8800/p2p.h>
#include <miracast/miracast.h>
#include <miracast/player.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "features/net/net.h"
#ifdef CONFIG_NEKOBOX_FEATURE_DLNA
#include "features/dlna/cast.h"
#endif
#ifdef CONFIG_NEKOBOX_FEATURE_VIDEO
#include "features/video/player.h"
#endif
#include "session.h"

enum radio_state { RADIO_OFF, RADIO_STARTING, RADIO_WAITING, RADIO_PEER, RADIO_FAILED };
static atomic_t radio_state, wanted, reset_group;
static K_MUTEX_DEFINE(radio_lock);
static K_SEM_DEFINE(radio_wake, 0, 1);
K_MSGQ_DEFINE(mirror_events, sizeof(enum miracast_state), 8, 4);
static char peer[16];
static enum mirror_state current;
static bool restore_dlna, session_up, played, restarting, failed;
static unsigned attempts;
static int64_t retry_at;

static void radio_main(void *a, void *b, void *c)
{
	bool reserved = false, group = false;
	int64_t last_pbc = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	for (;;) {
		k_sem_take(&radio_wake, K_MSEC(100));
		int ret = 0;

		if (!atomic_get(&wanted)) {
			if (group) aic8800_p2p_stop();
			if (reserved) app_net_resume();
			group = reserved = false;
			/* Do not acknowledge an idle poll after the UI has requested a new group. */
			k_mutex_lock(&radio_lock, K_FOREVER);
			if (!atomic_get(&wanted)) {
				atomic_clear(&reset_group);
				if (atomic_get(&radio_state) != RADIO_FAILED) {
					atomic_set(&radio_state, RADIO_OFF);
				}
			}
			k_mutex_unlock(&radio_lock);
			continue;
		}
		if (!reserved) {
			atomic_set(&radio_state, RADIO_STARTING);
			ret = app_net_suspend();
			if (ret) goto error;
			reserved = true;
			if (!atomic_get(&wanted)) continue;
		}
		if (atomic_get(&reset_group)) {
			atomic_set(&radio_state, RADIO_STARTING);
			atomic_clear(&reset_group);
			if (group) aic8800_p2p_stop();
			group = false;
			if (!atomic_get(&wanted)) continue;
		}
		if (!group) {
			ret = aic8800_p2p_start(CONFIG_NEKOBOX_MIRACAST_NAME, 7236);
			if (ret) goto error;
			group = true;
			if (!atomic_get(&wanted)) continue;
			last_pbc = k_uptime_get();
			atomic_set(&radio_state, RADIO_WAITING);
			printk("miracast: discoverable as %s\n", CONFIG_NEKOBOX_MIRACAST_NAME);
		}
		if (atomic_get(&radio_state) == RADIO_WAITING) {
			ret = aic8800_p2p_peer(peer, sizeof(peer));
			if (!ret) {
				atomic_set(&radio_state, RADIO_PEER);
			} else if (ret != -EAGAIN) {
				goto error;
			} else if (k_uptime_get() - last_pbc >= 100000) {
				ret = aic8800_p2p_accept();
				if (ret) goto error;
				last_pbc = k_uptime_get();
			}
		}
		continue;
error:
		printk("miracast: radio setup failed (%d)\n", ret);
		if (group) aic8800_p2p_stop();
		if (reserved) app_net_resume();
		group = reserved = false;
		atomic_clear(&wanted);
		atomic_set(&radio_state, RADIO_FAILED);
	}
}

K_THREAD_DEFINE(mirror_radio_thread, 4096, radio_main, NULL, NULL, NULL, 12, 0, 0);

static void receiver_state(void *ctx, enum miracast_state state)
{
	ARG_UNUSED(ctx);
	k_msgq_put(&mirror_events, &state, K_NO_WAIT);
}

static void stop_session(void)
{
	miracast_receiver_stop();
	miracast_player_stop();
	session_up = false;
}

static void restore_services(void)
{
#ifdef CONFIG_NEKOBOX_FEATURE_DLNA
	if (restore_dlna) cast_enable(true);
#endif
	restore_dlna = false;
}

void mirror_start(void)
{
	if (current == MIRROR_STOPPING) {
		restarting = true;
		return;
	}
	if (current != MIRROR_OFF && current != MIRROR_FAILED) return;
#ifdef CONFIG_NEKOBOX_FEATURE_DLNA
	restore_dlna = cast_enabled();
	cast_enable(false);
#endif
#ifdef CONFIG_NEKOBOX_FEATURE_VIDEO
	player_close();
#endif
	k_msgq_purge(&mirror_events);
	failed = played = false;
	attempts = 0;
	retry_at = 0;
	current = MIRROR_PREPARING;
}

void mirror_stop(void)
{
	restarting = false;
	if (current == MIRROR_OFF) return;
	stop_session();
	atomic_clear(&wanted);
	k_sem_give(&radio_wake);
	current = MIRROR_STOPPING;
}

void mirror_tick(void)
{
	enum miracast_state state;

	if (current == MIRROR_OFF || current == MIRROR_FAILED) return;
	if (current == MIRROR_STOPPING) {
		int radio = atomic_get(&radio_state);

		if (radio != RADIO_OFF && radio != RADIO_FAILED) return;
		restore_services();
		current = failed ? MIRROR_FAILED : MIRROR_OFF;
		if (restarting) {
			restarting = false;
			mirror_start();
		}
		return;
	}
	if (current == MIRROR_PREPARING) {
#ifdef CONFIG_NEKOBOX_FEATURE_DLNA
		if (!cast_idle()) return;
#endif
		k_mutex_lock(&radio_lock, K_FOREVER);
		atomic_set(&radio_state, RADIO_STARTING);
		atomic_set(&wanted, 1);
		k_mutex_unlock(&radio_lock);
		k_sem_give(&radio_wake);
		current = MIRROR_STARTING;
		return;
	}
	while (k_msgq_get(&mirror_events, &state, K_NO_WAIT) == 0) {
		if (state == MIRACAST_PLAYING) {
			played = true;
			current = MIRROR_PLAYING;
		}
	}
	int radio = atomic_get(&radio_state);

	if (radio == RADIO_FAILED) {
		stop_session();
		restore_services();
		current = MIRROR_FAILED;
		return;
	}
	if (radio == RADIO_STARTING) {
		current = MIRROR_STARTING;
		return;
	}
	if (radio == RADIO_WAITING) {
		current = MIRROR_WAITING;
		return;
	}
	if (session_up && !miracast_receiver_running()) {
		stop_session();
		current = MIRROR_CONNECTING;
		retry_at = k_uptime_get() + 500;
		if (played || attempts >= 10) {
			atomic_set(&reset_group, 1);
			k_sem_give(&radio_wake);
			played = false;
			attempts = 0;
		}
	}
	if (radio != RADIO_PEER || session_up || atomic_get(&reset_group) ||
	    k_uptime_get() < retry_at) return;
	struct miracast_receiver_config config = {
		.peer = peer,
		.stream = {.rtp_port = 19000, .cea_modes = 1},
		.ops = {.media = miracast_player_media, .state = receiver_state},
	};
	unsigned volume = 70;

#ifdef CONFIG_NEKOBOX_FEATURE_VIDEO
	volume = player_volume();
#endif
	k_msgq_purge(&mirror_events);
	int ret = miracast_player_start(volume);

	if (!ret) ret = miracast_receiver_start(&config);
	if (ret) {
		printk("miracast: receiver setup failed (%d)\n", ret);
		failed = true;
		mirror_stop();
		return;
	}
	attempts++;
	session_up = true;
	current = MIRROR_CONNECTING;
}

enum mirror_state mirror_state(void)
{
	return current;
}
