/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The two services an AirPlay receiver announces (_raop._tcp for the audio side where iOS finds
 * it, _airplay._tcp), on the DNS-SD records of the Zephyr mDNS responder. The records live in
 * the image, so this holds one instance; the names and TXT data are filled in at registration.
 */

#include <stdlib.h>
#include <string.h>
#include <zephyr/net/dns_sd.h>
#include <zephyr/sys/byteorder.h>

#include "dnssd.h"
#include "dnssdint.h"
#include "global.h"
#include "utils.h"

#define TXT_MAX		512
#define INSTANCE_MAX	(DNS_SD_INSTANCE_MAX_SIZE + 1)

static char raop_instance[INSTANCE_MAX];
static char airplay_instance[INSTANCE_MAX];
static uint8_t raop_txt[TXT_MAX];
static uint8_t airplay_txt[TXT_MAX];
static uint16_t raop_port;
static uint16_t airplay_port;

static STRUCT_SECTION_ITERABLE(dns_sd_rec, ios_raop) = {
	.instance = raop_instance,
	.service = "_raop",
	.proto = "_tcp",
	.domain = "local",
	.text = (const char *)raop_txt,
	.port = &raop_port,
};

static STRUCT_SECTION_ITERABLE(dns_sd_rec, ios_airplay) = {
	.instance = airplay_instance,
	.service = "_airplay",
	.proto = "_tcp",
	.domain = "local",
	.text = (const char *)airplay_txt,
	.port = &airplay_port,
};

struct dnssd_s {
	char *name;
	int name_len;
	char *hw_addr;
	int hw_addr_len;
	int airplay_txt_len;
};

/* appends "key=value" as a length prefixed TXT string */
static size_t txt_add(uint8_t *txt, size_t pos, const char *key, const char *value)
{
	size_t len = strlen(key) + 1 + strlen(value);

	if (len > 255U || pos + 1 + len > TXT_MAX) {
		return pos;
	}
	txt[pos] = len;
	memcpy(txt + pos + 1, key, strlen(key));
	txt[pos + 1 + strlen(key)] = '=';
	memcpy(txt + pos + 2 + strlen(key), value, strlen(value));

	return pos + 1 + len;
}

dnssd_t *dnssd_init(const char *name, int name_len, const char *hw_addr, int hw_addr_len,
		    int *error)
{
	dnssd_t *dnssd;

	if (error != NULL) {
		*error = DNSSD_ERROR_NOERROR;
	}
	dnssd = calloc(1, sizeof(*dnssd));
	if (dnssd == NULL) {
		goto nomem;
	}
	dnssd->name_len = name_len;
	dnssd->name = calloc(1, name_len + 1);
	dnssd->hw_addr_len = hw_addr_len;
	dnssd->hw_addr = calloc(1, hw_addr_len);
	if (dnssd->name == NULL || dnssd->hw_addr == NULL) {
		free(dnssd->name);
		free(dnssd->hw_addr);
		free(dnssd);
		goto nomem;
	}
	memcpy(dnssd->name, name, name_len);
	memcpy(dnssd->hw_addr, hw_addr, hw_addr_len);

	return dnssd;
nomem:
	if (error != NULL) {
		*error = DNSSD_ERROR_OUTOFMEM;
	}

	return NULL;
}

int dnssd_register_raop(dnssd_t *dnssd, unsigned short port)
{
	size_t n = 0;
	char servname[DNS_SD_INSTANCE_MAX_SIZE + 1];

	if (utils_hwaddr_raop(servname, sizeof(servname), dnssd->hw_addr, dnssd->hw_addr_len) < 0 ||
	    strlen(servname) + 1 + dnssd->name_len >= sizeof(servname)) {
		return -1;
	}
	strcat(servname, "@");
	strcat(servname, dnssd->name);

	n = txt_add(raop_txt, n, "ch", RAOP_CH);
	n = txt_add(raop_txt, n, "cn", RAOP_CN);
	n = txt_add(raop_txt, n, "da", RAOP_DA);
	n = txt_add(raop_txt, n, "et", RAOP_ET);
	n = txt_add(raop_txt, n, "vv", RAOP_VV);
	n = txt_add(raop_txt, n, "ft", RAOP_FT);
	n = txt_add(raop_txt, n, "am", GLOBAL_MODEL);
	n = txt_add(raop_txt, n, "md", RAOP_MD);
	n = txt_add(raop_txt, n, "rhd", RAOP_RHD);
	n = txt_add(raop_txt, n, "pw", "false");
	n = txt_add(raop_txt, n, "sr", RAOP_SR);
	n = txt_add(raop_txt, n, "ss", RAOP_SS);
	n = txt_add(raop_txt, n, "sv", RAOP_SV);
	n = txt_add(raop_txt, n, "tp", RAOP_TP);
	n = txt_add(raop_txt, n, "txtvers", RAOP_TXTVERS);
	n = txt_add(raop_txt, n, "sf", RAOP_SF);
	n = txt_add(raop_txt, n, "vs", RAOP_VS);
	n = txt_add(raop_txt, n, "vn", RAOP_VN);
	n = txt_add(raop_txt, n, "pk", RAOP_PK);
	ios_raop.text_size = n;

	strcpy(raop_instance, servname);
	raop_port = sys_cpu_to_be16(port);

	return 1;
}

int dnssd_register_airplay(dnssd_t *dnssd, unsigned short port)
{
	size_t n = 0;
	char device_id[3 * MAX_HWADDR_LEN];

	if (utils_hwaddr_airplay(device_id, sizeof(device_id), dnssd->hw_addr,
				 dnssd->hw_addr_len) < 0 ||
	    dnssd->name_len > DNS_SD_INSTANCE_MAX_SIZE) {
		return -1;
	}
	n = txt_add(airplay_txt, n, "deviceid", device_id);
	n = txt_add(airplay_txt, n, "features", AIRPLAY_FEATURES);
	n = txt_add(airplay_txt, n, "flags", AIRPLAY_FLAGS);
	n = txt_add(airplay_txt, n, "model", GLOBAL_MODEL);
	n = txt_add(airplay_txt, n, "pk", AIRPLAY_PK);
	n = txt_add(airplay_txt, n, "pi", AIRPLAY_PI);
	n = txt_add(airplay_txt, n, "srcvers", AIRPLAY_SRCVERS);
	n = txt_add(airplay_txt, n, "vv", AIRPLAY_VV);
	ios_airplay.text_size = n;
	dnssd->airplay_txt_len = n;

	strcpy(airplay_instance, dnssd->name);
	airplay_port = sys_cpu_to_be16(port);

	return 1;
}

const char *dnssd_get_airplay_txt(dnssd_t *dnssd, int *length)
{
	*length = dnssd->airplay_txt_len;

	return (const char *)airplay_txt;
}

const char *dnssd_get_name(dnssd_t *dnssd, int *length)
{
	*length = dnssd->name_len;

	return dnssd->name;
}

const char *dnssd_get_hw_addr(dnssd_t *dnssd, int *length)
{
	*length = dnssd->hw_addr_len;

	return dnssd->hw_addr;
}

void dnssd_unregister_raop(dnssd_t *dnssd)
{
	ARG_UNUSED(dnssd);
	/* a port of zero takes the service off the air */
	raop_port = 0;
}

void dnssd_unregister_airplay(dnssd_t *dnssd)
{
	ARG_UNUSED(dnssd);
	airplay_port = 0;
}

void dnssd_destroy(dnssd_t *dnssd)
{
	if (dnssd != NULL) {
		free(dnssd->name);
		free(dnssd->hw_addr);
		free(dnssd);
	}
}
