/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLATFORM_ALLWIN_FREERTOS
#define os_free free
#define os_strlen strlen
#define os_strcmp strcmp
#define os_strncmp strncmp
#define os_memcpy memcpy
#define wpa_printf(...) ((void)0)

struct wpa_config { int p2p_go_vht, p2p_go_ht40, p2p_go_he; };
struct wpa_global { int p2p_disabled; void *p2p, *add_psk; };
struct wpa_supplicant {
	struct wpa_config *conf;
	struct wpa_global *global;
	int p2p_go_do_acs;
};
struct p2p_go_neg_results { char passphrase[64]; int persistent_group; };
static char started_key[64];
static unsigned started;

static char *str_token(char *text, const char *delimiters, char **context)
{
	return strtok_r(*context ? NULL : text, delimiters, context);
}

static int parse_freq(int width, int freq2) { return 0; }
static void wpas_p2p_stop_find_oper(struct wpa_supplicant *wpa) {}
static int wpas_p2p_select_go_freq(struct wpa_supplicant *wpa, int freq) { return freq; }
static int wpas_p2p_init_go_params(struct wpa_supplicant *wpa,
	struct p2p_go_neg_results *params, int freq, int freq2, int ht40, int vht,
	int width, int he, void *extra)
{
	memset(params, 0, sizeof(*params));
	return 0;
}
static void p2p_go_params(void *p2p, struct p2p_go_neg_results *params)
{
	strcpy(params->passphrase, "generated-key");
}
static struct wpa_supplicant *wpas_p2p_get_group_iface(struct wpa_supplicant *wpa,
	int allocated, int group)
{
	return wpa;
}
static void wpas_start_wps_go(struct wpa_supplicant *wpa,
	struct p2p_go_neg_results *params, int formation)
{
	strcpy(started_key, params->passphrase);
	started++;
}
static int p2p_ctrl_group_add_persistent(struct wpa_supplicant *wpa, int id,
	int freq, int freq2, int ht40, int vht, int width, int he)
{
	return -1;
}

#include "functions.inc"

int main(void)
{
	struct wpa_config config = {0};
	struct wpa_global global = {.p2p = &config};
	struct wpa_supplicant wpa = {.conf = &config, .global = &global};
	char command[4200], key[4097];
	const size_t valid[] = {8, 15, 16, 32, 63};
	const size_t invalid[] = {0, 7, 64, 4096};

	strcpy(command, "freq=2437");
	assert(!p2p_ctrl_group_add(&wpa, command));
	assert(started == 1 && !strcmp(started_key, "generated-key"));
	for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
		memset(key, 'a', valid[i]);
		key[valid[i]] = 0;
		snprintf(command, sizeof(command), "pass=%s freq=2437", key);
		assert(!p2p_ctrl_group_add(&wpa, command));
		assert(!strcmp(started_key, key));
	}
	unsigned count = started;

	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		memset(key, 'b', invalid[i]);
		key[invalid[i]] = 0;
		snprintf(command, sizeof(command), "pass=%s freq=2437", key);
		assert(p2p_ctrl_group_add(&wpa, command) < 0);
		assert(wpas_p2p_group_add(&wpa, 0, 2437, 0, 0, 0, 0, 0, key) < 0);
		assert(started == count);
	}
	strcpy(command, "pass=12345678 pass=abcdefgh freq=2437");
	assert(p2p_ctrl_group_add(&wpa, command) < 0 && started == count);
	puts("P2P group passphrase bounds and generated default: PASS");
	return 0;
}
