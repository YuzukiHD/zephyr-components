/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <signal.h>
/* Include the server to obtain its ephemeral listen port without a test-only product API. */
#include "../../src/http_srv.c"

static int volume = 50;
static enum dlna_state state = DLNA_NO_MEDIA;
static int set_uri(void *ctx, const char *uri, const char *title)
{
	(void)ctx; (void)uri; (void)title;
	state = DLNA_STOPPED;
	return 0;
}
static int play(void *ctx) { (void)ctx; state = DLNA_PLAYING; return 0; }
static int get_volume(void *ctx) { (void)ctx; return volume; }
static int set_volume(void *ctx, int v) { (void)ctx; volume = v; return 0; }
static enum dlna_state get_state(void *ctx) { (void)ctx; return state; }
static const struct dlna_ops ops = {
	.set_uri = set_uri, .play = play, .state = get_state,
	.get_volume = get_volume, .set_volume = set_volume,
};
static const struct dlna_config cfg = {
	.name = "HTTP test", .uuid = "12345678-1234-5678-1234-567812345678", .ops = &ops,
};
int main(void)
{
	struct sockaddr_in a;
	socklen_t len = sizeof(a);

	signal(SIGPIPE, SIG_IGN);
	upnp.cfg = &cfg;
	assert(http_srv_start() == 0);
	assert(getsockname(listen_fd, (struct sockaddr *)&a, &len) == 0);
	printf("%u\n", ntohs(a.sin_port));
	fflush(stdout);
	getchar();
	http_srv_stop();
	return 0;
}
