/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <aic8800/p2p.h>
#include <miracast/player.h>

#include "features/dlna/cast.h"
#include "features/miracast/session.h"
#include "features/net/net.h"
#include "features/video/player.h"

_Atomic(int64_t) test_clock_offset;
static pthread_t ui_thread;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;

enum call { SUSPEND, RESUME, GROUP_START, GROUP_STOP, ACCEPT, PLAYER_START,
	    PLAYER_STOP, RECEIVER_START, RECEIVER_STOP, DLNA_ON, DLNA_OFF, LOCAL_CLOSE, CALLS };
enum gate { SUSPENDING, STARTING_GROUP, STOPPING_GROUP, GATES };
static struct {
	bool reserved, group, player, receiver, peer, dlna, download_idle;
	bool blocked[GATES], entered[GATES];
	int suspend_error, group_error, peer_error, player_error, receiver_error;
	unsigned calls[CALLS];
	struct miracast_receiver_config config;
} mock;

static bool on_ui(void) { return pthread_equal(pthread_self(), ui_thread); }

static void gate_locked(enum gate gate)
{
	mock.entered[gate] = true;
	pthread_cond_broadcast(&changed);
	while (mock.blocked[gate]) pthread_cond_wait(&changed, &lock);
}

int app_net_suspend(void)
{
	assert(!on_ui());
	pthread_mutex_lock(&lock);
	assert(!mock.reserved && !mock.group && !mock.player && !mock.receiver);
	assert(mock.download_idle);
	mock.calls[SUSPEND]++;
	gate_locked(SUSPENDING);
	int ret = mock.suspend_error;

	if (!ret) mock.reserved = true;
	pthread_mutex_unlock(&lock);
	return ret;
}

void app_net_resume(void)
{
	assert(!on_ui());
	pthread_mutex_lock(&lock);
	assert(mock.reserved && !mock.group && !mock.player && !mock.receiver);
	mock.reserved = false;
	mock.calls[RESUME]++;
	pthread_mutex_unlock(&lock);
}

int aic8800_p2p_start(const char *name, uint16_t port)
{
	assert(!on_ui() && !strcmp(name, "NekoBox Miracast") && port == 7236);
	pthread_mutex_lock(&lock);
	assert(mock.reserved && !mock.group);
	mock.calls[GROUP_START]++;
	gate_locked(STARTING_GROUP);
	int ret = mock.group_error;

	if (!ret) mock.group = true;
	pthread_mutex_unlock(&lock);
	return ret;
}

int aic8800_p2p_stop(void)
{
	assert(!on_ui());
	pthread_mutex_lock(&lock);
	assert(mock.reserved && mock.group && !mock.player && !mock.receiver);
	mock.calls[GROUP_STOP]++;
	gate_locked(STOPPING_GROUP);
	mock.group = mock.peer = false;
	pthread_mutex_unlock(&lock);
	return 0;
}

int aic8800_p2p_accept(void)
{
	assert(!on_ui());
	pthread_mutex_lock(&lock);
	assert(mock.reserved && mock.group);
	mock.calls[ACCEPT]++;
	pthread_mutex_unlock(&lock);
	return 0;
}

int aic8800_p2p_peer(char *address, size_t size)
{
	assert(!on_ui() && size >= 16);
	pthread_mutex_lock(&lock);
	assert(mock.reserved && mock.group);
	int ret = mock.peer_error ? mock.peer_error : mock.peer ? 0 : -EAGAIN;

	if (!ret) strcpy(address, "192.168.49.2");
	pthread_mutex_unlock(&lock);
	return ret;
}

int miracast_player_start(unsigned volume)
{
	assert(on_ui());
#ifdef CONFIG_NEKOBOX_FEATURE_VIDEO
	assert(volume == 37);
#else
	assert(volume == 70);
#endif
	pthread_mutex_lock(&lock);
	assert(mock.reserved && mock.group && !mock.player && !mock.receiver);
	mock.calls[PLAYER_START]++;
	int ret = mock.player_error;

	if (!ret) mock.player = true;
	pthread_mutex_unlock(&lock);
	return ret;
}

void miracast_player_stop(void)
{
	assert(on_ui());
	pthread_mutex_lock(&lock);
	assert(!mock.receiver);
	mock.player = false;
	mock.calls[PLAYER_STOP]++;
	pthread_mutex_unlock(&lock);
}

int miracast_player_media(void *ctx, enum miracast_codec codec, const void *data,
			  size_t size, int64_t pts, bool gap)
{
	ARG_UNUSED(ctx);
	ARG_UNUSED(codec);
	ARG_UNUSED(data);
	ARG_UNUSED(size);
	ARG_UNUSED(pts);
	ARG_UNUSED(gap);
	return 0;
}

int miracast_receiver_start(const struct miracast_receiver_config *config)
{
	assert(on_ui() && !strcmp(config->peer, "192.168.49.2"));
	assert(config->stream.rtp_port == 19000 && config->stream.cea_modes == 1);
	assert(!config->stream.aac && config->ops.media == miracast_player_media);
	pthread_mutex_lock(&lock);
	assert(mock.reserved && mock.group && mock.player && !mock.receiver);
	mock.calls[RECEIVER_START]++;
	mock.config = *config;
	int ret = mock.receiver_error;

	if (!ret) mock.receiver = true;
	pthread_mutex_unlock(&lock);
	return ret;
}

void miracast_receiver_stop(void)
{
	assert(on_ui());
	pthread_mutex_lock(&lock);
	mock.receiver = false;
	mock.calls[RECEIVER_STOP]++;
	pthread_mutex_unlock(&lock);
}

bool miracast_receiver_running(void)
{
	assert(on_ui());
	pthread_mutex_lock(&lock);
	bool running = mock.receiver;

	pthread_mutex_unlock(&lock);
	return running;
}

void player_close(void)
{
	assert(on_ui());
	pthread_mutex_lock(&lock);
	assert(!mock.player);
	mock.calls[LOCAL_CLOSE]++;
	pthread_mutex_unlock(&lock);
}

int player_volume(void) { assert(on_ui()); return 37; }

void cast_enable(bool enabled)
{
	assert(on_ui());
	pthread_mutex_lock(&lock);
	if (enabled) assert(!mock.reserved && !mock.group && !mock.player && !mock.receiver);
	mock.dlna = enabled;
	mock.calls[enabled ? DLNA_ON : DLNA_OFF]++;
	pthread_mutex_unlock(&lock);
}

bool cast_enabled(void) { assert(on_ui()); return mock.dlna; }
bool cast_idle(void) { assert(on_ui()); return !mock.dlna && mock.download_idle; }

static unsigned calls(enum call call)
{
	pthread_mutex_lock(&lock);
	unsigned count = mock.calls[call];

	pthread_mutex_unlock(&lock);
	return count;
}

static void block(enum gate gate, bool blocked)
{
	pthread_mutex_lock(&lock);
	mock.blocked[gate] = blocked;
	if (blocked) mock.entered[gate] = false;
	pthread_cond_broadcast(&changed);
	pthread_mutex_unlock(&lock);
}

static void wait_gate(enum gate gate)
{
	for (unsigned i = 0; i < 5000; i++) {
		pthread_mutex_lock(&lock);
		bool entered = mock.entered[gate];

		pthread_mutex_unlock(&lock);
		if (entered) return;
		usleep(1000);
	}
	assert(!"radio did not reach the blocked operation");
}

static void wait_state(enum mirror_state state)
{
	for (unsigned i = 0; i < 5000; i++) {
		mirror_tick();
		if (mirror_state() == state) return;
		usleep(1000);
	}
	fprintf(stderr, "expected state %d, got %d\n", state, mirror_state());
	assert(!"session transition timed out");
}

static void reset(void)
{
	assert(mirror_state() == MIRROR_OFF || mirror_state() == MIRROR_FAILED);
	pthread_mutex_lock(&lock);
	assert(!mock.reserved && !mock.group && !mock.player && !mock.receiver);
	memset(&mock, 0, sizeof(mock));
	mock.download_idle = true;
#ifdef CONFIG_NEKOBOX_FEATURE_DLNA
	mock.dlna = true;
#endif
	pthread_mutex_unlock(&lock);
}

static void start(void)
{
	mirror_start();
	wait_state(MIRROR_WAITING);
}

static void connect_phone(void)
{
	pthread_mutex_lock(&lock);
	mock.peer = true;
	pthread_mutex_unlock(&lock);
	wait_state(MIRROR_CONNECTING);
	assert(miracast_receiver_running());
}

static void *network_event(void *arg)
{
	assert(!on_ui());
	mock.config.ops.state(mock.config.ops.ctx, *(enum miracast_state *)arg);
	return NULL;
}

static void emit(enum miracast_state state)
{
	pthread_t thread;

	assert(pthread_create(&thread, NULL, network_event, &state) == 0);
	assert(pthread_join(thread, NULL) == 0);
}

static void playing(void)
{
	emit(MIRACAST_PLAYING);
	assert(mirror_state() == MIRROR_CONNECTING);
	wait_state(MIRROR_PLAYING);
}

static void disconnect_phone(void)
{
	pthread_mutex_lock(&lock);
	mock.receiver = false;
	pthread_mutex_unlock(&lock);
	emit(MIRACAST_CLOSED);
	mirror_tick();
}

static void stop(void)
{
	mirror_stop();
	wait_state(MIRROR_OFF);
	pthread_mutex_lock(&lock);
	assert(!mock.reserved && !mock.group && !mock.player && !mock.receiver);
#ifdef CONFIG_NEKOBOX_FEATURE_DLNA
	assert(mock.dlna);
#endif
	pthread_mutex_unlock(&lock);
}

int main(void)
{
	ui_thread = pthread_self();
	reset();
#ifdef CONFIG_NEKOBOX_FEATURE_DLNA
	/* A cancelled DLNA download must relinquish its sockets before radio handoff. */
	mock.download_idle = false;
	mirror_start();
	for (unsigned i = 0; i < 20; i++) mirror_tick();
	assert(mirror_state() == MIRROR_PREPARING && !calls(SUSPEND));
	mock.download_idle = true;
	wait_state(MIRROR_WAITING);
#else
	start();
#endif
	connect_phone();
	playing();
	stop();
	assert(calls(SUSPEND) == 1 && calls(RESUME) == 1);

#ifdef CONFIG_NEKOBOX_FEATURE_DLNA
	reset();
	mock.dlna = false;
	start();
	mirror_stop();
	wait_state(MIRROR_OFF);
	assert(!mock.dlna && !calls(DLNA_ON));
#endif

	/* Leaving before the radio worker starts must not start Wi-Fi Direct. */
	reset();
	mirror_start();
	stop();
	assert(!calls(SUSPEND) && !calls(GROUP_START));

	reset();
	block(SUSPENDING, true);
	mirror_start();
	mirror_tick();
	wait_gate(SUSPENDING);
	mirror_stop();
	mirror_tick();
	assert(mirror_state() == MIRROR_STOPPING);
	block(SUSPENDING, false);
	wait_state(MIRROR_OFF);
	assert(!calls(GROUP_START) && calls(RESUME) == 1);

	reset();
	block(STARTING_GROUP, true);
	mirror_start();
	mirror_tick();
	wait_gate(STARTING_GROUP);
	mirror_stop();
	mirror_tick();
	assert(mirror_state() == MIRROR_STOPPING);
	block(STARTING_GROUP, false);
	wait_state(MIRROR_OFF);
	assert(calls(GROUP_STOP) == 1 && calls(RESUME) == 1);

	/* Reentering during shutdown waits for cleanup before taking the radio again. */
	reset();
	start();
	block(STOPPING_GROUP, true);
	mirror_stop();
	wait_gate(STOPPING_GROUP);
	mirror_start();
	mirror_tick();
	assert(mirror_state() == MIRROR_STOPPING);
	block(STOPPING_GROUP, false);
	wait_state(MIRROR_WAITING);
	assert(calls(SUSPEND) == 2 && calls(RESUME) == 1);
	stop();

	reset();
	start();
	connect_phone();
	playing();
	disconnect_phone();
	wait_state(MIRROR_WAITING);
	assert(calls(GROUP_STOP) == 1 && calls(SUSPEND) == 1 && !calls(RESUME));
	connect_phone();
	playing();
	stop();

	reset();
	start();
	connect_phone();
	playing();
	block(STOPPING_GROUP, true);
	disconnect_phone();
	wait_gate(STOPPING_GROUP);
	mirror_stop();
	block(STOPPING_GROUP, false);
	wait_state(MIRROR_OFF);
	assert(calls(GROUP_START) == 1 && calls(RESUME) == 1);

	/* An unconsumed PLAY event from a stopped receiver must not skip later retries. */
	reset();
	start();
	connect_phone();
	emit(MIRACAST_PLAYING);
	stop();
	start();
	connect_phone();
	disconnect_phone();
	atomic_fetch_add(&test_clock_offset, 501);
	wait_state(MIRROR_CONNECTING);
	assert(miracast_receiver_running() && calls(GROUP_START) == 2);
	playing();
	stop();

	/* A source may start listening after DHCP: retry RTSP without cycling Wi-Fi. */
	reset();
	start();
	connect_phone();
	for (unsigned i = 0; i < 10; i++) {
		disconnect_phone();
		if (i == 9) break;
		assert(!calls(GROUP_STOP));
		atomic_fetch_add(&test_clock_offset, 501);
		wait_state(MIRROR_CONNECTING);
		assert(miracast_receiver_running());
	}
	wait_state(MIRROR_WAITING);
	assert(calls(RECEIVER_START) == 10 && calls(GROUP_STOP) == 1 && !calls(RESUME));
	stop();

	/* Setup failures release resources and allow the same page to retry. */
	for (unsigned failure = 0; failure < 5; failure++) {
		reset();
		pthread_mutex_lock(&lock);
		switch (failure) {
		case 0: mock.suspend_error = -ENODEV; break;
		case 1: mock.group_error = -EIO; break;
		case 2: mock.peer_error = -ENETDOWN; break;
		case 3: mock.player_error = -ENODEV; mock.peer = true; break;
		case 4: mock.receiver_error = -EIO; mock.peer = true; break;
		}
		pthread_mutex_unlock(&lock);
		mirror_start();
		wait_state(MIRROR_FAILED);
		assert(calls(RESUME) == (failure == 0 ? 0U : 1U));
#ifdef CONFIG_NEKOBOX_FEATURE_DLNA
		assert(mock.dlna);
#endif
		reset();
		start();
		stop();
	}

	reset();
	start();
	atomic_fetch_add(&test_clock_offset, 100001);
	for (unsigned i = 0; i < 5000 && !calls(ACCEPT); i++) usleep(1000);
	assert(calls(ACCEPT) == 1);
	stop();

	reset();
	for (unsigned i = 0; i < 20; i++) {
		start();
		if (i & 1) {
			connect_phone();
			playing();
		}
		stop();
	}
	assert(calls(SUSPEND) == 20 && calls(GROUP_START) == 20 &&
	       calls(GROUP_STOP) == 20 && calls(RESUME) == 20);
	reset();
	for (unsigned i = 0; i < 20; i++) {
		mirror_start();
		mirror_tick();
		stop();
	}
	assert(calls(SUSPEND) == calls(RESUME) && calls(GROUP_START) == calls(GROUP_STOP));
	puts("NekoBox Miracast lifecycle, thread ownership and radio handoff: PASS");
	return 0;
}
