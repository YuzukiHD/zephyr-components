/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Device and service descriptions and the SOAP actions of the renderer */

#ifndef DLNA_UPNP_H_
#define DLNA_UPNP_H_

#include <stdbool.h>
#include <stdint.h>

#include <dlna/dlna.h>

#include "util.h"

enum upnp_svc { SVC_AVT, SVC_RCS, SVC_CM, SVC_COUNT };

struct upnp_service {
	const char *name;	/**< "AVTransport" */
	const char *id;		/**< serviceId suffix */
	const char *path;	/**< "/AVTransport" -> /AVTransport/scpd.xml, /control, /event */
};

extern const struct upnp_service upnp_services[SVC_COUNT];

struct upnp_state {
	const struct dlna_config *cfg;
	char uri[DLNA_URI_SIZE];	/**< current AVTransportURI, "" when none */
	char title[128];
	bool uri_rejected;	/**< a failed replacement must not replay the previous address */
	bool muted;
	int volume_before_mute;
};

extern struct upnp_state upnp;

/* Two URI attributes, escaped first into LastChange and then into the event XML. */
#define UPNP_EVENT_SIZE (DLNA_URI_SIZE * 20 + 4096)

/** Device type and service type URNs */
#define UPNP_DEVICE_TYPE "urn:schemas-upnp-org:device:MediaRenderer:1"

/** "urn:schemas-upnp-org:service:<name>:1" into @p out */
void upnp_service_type(enum upnp_svc s, char *out, size_t size);

/** UPnP error codes of the actions */
#define UPNP_ERR_INVALID_ACTION	401
#define UPNP_ERR_INVALID_ARGS	402
#define UPNP_ERR_FAILED		501
#define UPNP_ERR_NOT_AVAILABLE	701
#define UPNP_ERR_SEEK_MODE	710
#define UPNP_ERR_SEEK_TARGET	711
#define UPNP_ERR_NO_CONTENT	714
#define UPNP_ERR_BAD_INSTANCE	718

void upnp_description(struct sb *b);
void upnp_scpd(enum upnp_svc s, struct sb *b);

/** Current evented variables, including the LastChange aggregate for AVT/RCS. */
void upnp_event(enum upnp_svc s, struct sb *b);

/**
 * Runs one SOAP action. On success the <u:...Response> element is appended to @p resp and 0 is
 * returned; otherwise the UPnP error code (the caller answers with HTTP 500 and a fault).
 */
int upnp_action(enum upnp_svc s, const char *action, const char *body, struct sb *resp);

/** TransportState text of the current state ("PLAYING", ...) and the status ("OK"/"ERROR_OCCURRED") */
const char *upnp_transport_state(const char **status);

#endif /* DLNA_UPNP_H_ */
