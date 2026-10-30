/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "soap.h"
#include "upnp.h"

#define NS_SVC "urn:schemas-upnp-org:service:"
#define SINK_PROTOCOLS "http-get:*:video/mp4:*,http-get:*:video/x-m4v:*"

struct upnp_state upnp;

const struct upnp_service upnp_services[SVC_COUNT] = {
	[SVC_AVT] = {"AVTransport", "AVTransport", "/AVTransport"},
	[SVC_RCS] = {"RenderingControl", "RenderingControl", "/RenderingControl"},
	[SVC_CM] = {"ConnectionManager", "ConnectionManager", "/ConnectionManager"},
};

void upnp_service_type(enum upnp_svc s, char *out, size_t size)
{
	snprintf(out, size, NS_SVC "%s:1", upnp_services[s].name);
}

/* ---- descriptions --------------------------------------------------------------------------- */

void upnp_description(struct sb *b)
{
	char type[80];

	sb_puts(b, "<?xml version=\"1.0\"?>\r\n"
		   "<root xmlns=\"urn:schemas-upnp-org:device-1-0\" "
		   "xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\">"
		   "<specVersion><major>1</major><minor>0</minor></specVersion><device>"
		   "<dlna:X_DLNADOC>DMR-1.50</dlna:X_DLNADOC>"
		   "<deviceType>" UPNP_DEVICE_TYPE "</deviceType><friendlyName>");
	sb_puts_escaped(b, upnp.cfg->name);
	sb_puts(b, "</friendlyName><manufacturer>YuzukiHD</manufacturer>"
		   "<modelName>F101 Media Renderer</modelName><modelNumber>1</modelNumber>"
		   "<UDN>uuid:");
	sb_puts(b, upnp.cfg->uuid);
	sb_puts(b, "</UDN><serviceList>");
	for (int i = 0; i < SVC_COUNT; i++) {
		const struct upnp_service *s = &upnp_services[i];

		upnp_service_type(i, type, sizeof(type));
		sb_printf(b, "<service><serviceType>%s</serviceType>"
			     "<serviceId>urn:upnp-org:serviceId:%s</serviceId>"
			     "<SCPDURL>%s/scpd.xml</SCPDURL><controlURL>%s/control</controlURL>"
			     "<eventSubURL>%s/event</eventSubURL></service>",
			  type, s->id, s->path, s->path, s->path);
	}
	sb_puts(b, "</serviceList></device></root>");
}

struct svar {
	const char *name;
	const char *type;
	bool events;
	const char *allowed;	/**< comma separated allowed values or NULL */
	const char *range;	/**< "min,max,step" or NULL */
};

struct sact {
	const char *name;
	const char *args;	/**< "+InIn=Var -Out=Var": + in, - out */
};

struct sdesc {
	const struct svar *vars;
	int nvars;
	const struct sact *acts;
	int nacts;
};

#define IID "+InstanceID=A_ARG_TYPE_InstanceID"

static const struct svar avt_vars[] = {
	{"TransportState", "string", false,
	 "STOPPED,PLAYING,PAUSED_PLAYBACK,TRANSITIONING,NO_MEDIA_PRESENT", NULL},
	{"TransportStatus", "string", false, "OK,ERROR_OCCURRED", NULL},
	{"PlaybackStorageMedium", "string", false, NULL, NULL},
	{"RecordStorageMedium", "string", false, NULL, NULL},
	{"PossiblePlaybackStorageMedia", "string", false, NULL, NULL},
	{"PossibleRecordStorageMedia", "string", false, NULL, NULL},
	{"CurrentPlayMode", "string", false, "NORMAL", NULL},
	{"TransportPlaySpeed", "string", false, "1", NULL},
	{"RecordMediumWriteStatus", "string", false, NULL, NULL},
	{"CurrentRecordQualityMode", "string", false, NULL, NULL},
	{"PossibleRecordQualityModes", "string", false, NULL, NULL},
	{"NumberOfTracks", "ui4", false, NULL, "0,1,1"},
	{"CurrentTrack", "ui4", false, NULL, "0,1,1"},
	{"CurrentTrackDuration", "string", false, NULL, NULL},
	{"CurrentMediaDuration", "string", false, NULL, NULL},
	{"CurrentTrackMetaData", "string", false, NULL, NULL},
	{"CurrentTrackURI", "string", false, NULL, NULL},
	{"AVTransportURI", "string", false, NULL, NULL},
	{"AVTransportURIMetaData", "string", false, NULL, NULL},
	{"NextAVTransportURI", "string", false, NULL, NULL},
	{"NextAVTransportURIMetaData", "string", false, NULL, NULL},
	{"RelativeTimePosition", "string", false, NULL, NULL},
	{"AbsoluteTimePosition", "string", false, NULL, NULL},
	{"RelativeCounterPosition", "i4", false, NULL, NULL},
	{"AbsoluteCounterPosition", "ui4", false, NULL, NULL},
	{"CurrentTransportActions", "string", false, NULL, NULL},
	{"LastChange", "string", true, NULL, NULL},
	{"A_ARG_TYPE_SeekMode", "string", false, "REL_TIME,ABS_TIME,TRACK_NR", NULL},
	{"A_ARG_TYPE_SeekTarget", "string", false, NULL, NULL},
	{"A_ARG_TYPE_InstanceID", "ui4", false, NULL, NULL},
};

static const struct sact avt_acts[] = {
	{"SetAVTransportURI", IID " +CurrentURI=AVTransportURI "
			      "+CurrentURIMetaData=AVTransportURIMetaData"},
	{"GetMediaInfo", IID " -NrTracks=NumberOfTracks -MediaDuration=CurrentMediaDuration "
			 "-CurrentURI=AVTransportURI -CurrentURIMetaData=AVTransportURIMetaData "
			 "-NextURI=NextAVTransportURI -NextURIMetaData=NextAVTransportURIMetaData "
			 "-PlayMedium=PlaybackStorageMedium -RecordMedium=RecordStorageMedium "
			 "-WriteStatus=RecordMediumWriteStatus"},
	{"GetTransportInfo", IID " -CurrentTransportState=TransportState "
			     "-CurrentTransportStatus=TransportStatus -CurrentSpeed=TransportPlaySpeed"},
	{"GetPositionInfo", IID " -Track=CurrentTrack -TrackDuration=CurrentTrackDuration "
			    "-TrackMetaData=CurrentTrackMetaData -TrackURI=CurrentTrackURI "
			    "-RelTime=RelativeTimePosition -AbsTime=AbsoluteTimePosition "
			    "-RelCount=RelativeCounterPosition -AbsCount=AbsoluteCounterPosition"},
	{"GetDeviceCapabilities", IID " -PlayMedia=PossiblePlaybackStorageMedia "
				  "-RecMedia=PossibleRecordStorageMedia "
				  "-RecQualityModes=PossibleRecordQualityModes"},
	{"GetTransportSettings", IID " -PlayMode=CurrentPlayMode "
				 "-RecQualityMode=CurrentRecordQualityMode"},
	{"Stop", IID},
	{"Play", IID " +Speed=TransportPlaySpeed"},
	{"Pause", IID},
	{"Seek", IID " +Unit=A_ARG_TYPE_SeekMode +Target=A_ARG_TYPE_SeekTarget"},
	{"Next", IID},
	{"Previous", IID},
	{"SetPlayMode", IID " +NewPlayMode=CurrentPlayMode"},
	{"GetCurrentTransportActions", IID " -Actions=CurrentTransportActions"},
};

static const struct svar rcs_vars[] = {
	{"LastChange", "string", true, NULL, NULL},
	{"PresetNameList", "string", false, NULL, NULL},
	{"Mute", "boolean", false, NULL, NULL},
	{"Volume", "ui2", false, NULL, "0,100,1"},
	{"A_ARG_TYPE_Channel", "string", false, "Master", NULL},
	{"A_ARG_TYPE_InstanceID", "ui4", false, NULL, NULL},
	{"A_ARG_TYPE_PresetName", "string", false, "FactoryDefaults", NULL},
};

static const struct sact rcs_acts[] = {
	{"ListPresets", IID " -CurrentPresetNameList=PresetNameList"},
	{"SelectPreset", IID " +PresetName=A_ARG_TYPE_PresetName"},
	{"GetMute", IID " +Channel=A_ARG_TYPE_Channel -CurrentMute=Mute"},
	{"SetMute", IID " +Channel=A_ARG_TYPE_Channel +DesiredMute=Mute"},
	{"GetVolume", IID " +Channel=A_ARG_TYPE_Channel -CurrentVolume=Volume"},
	{"SetVolume", IID " +Channel=A_ARG_TYPE_Channel +DesiredVolume=Volume"},
};

static const struct svar cm_vars[] = {
	{"SourceProtocolInfo", "string", true, NULL, NULL},
	{"SinkProtocolInfo", "string", true, NULL, NULL},
	{"CurrentConnectionIDs", "string", true, NULL, NULL},
	{"A_ARG_TYPE_ConnectionStatus", "string", false,
	 "OK,ContentFormatMismatch,InsufficientBandwidth,UnreliableChannel,Unknown", NULL},
	{"A_ARG_TYPE_ConnectionManager", "string", false, NULL, NULL},
	{"A_ARG_TYPE_Direction", "string", false, "Input,Output", NULL},
	{"A_ARG_TYPE_ProtocolInfo", "string", false, NULL, NULL},
	{"A_ARG_TYPE_ConnectionID", "i4", false, NULL, NULL},
	{"A_ARG_TYPE_AVTransportID", "i4", false, NULL, NULL},
	{"A_ARG_TYPE_RcsID", "i4", false, NULL, NULL},
};

static const struct sact cm_acts[] = {
	{"GetProtocolInfo", "-Source=SourceProtocolInfo -Sink=SinkProtocolInfo"},
	{"GetCurrentConnectionIDs", "-ConnectionIDs=CurrentConnectionIDs"},
	{"GetCurrentConnectionInfo",
	 "+ConnectionID=A_ARG_TYPE_ConnectionID -RcsID=A_ARG_TYPE_RcsID "
	 "-AVTransportID=A_ARG_TYPE_AVTransportID -ProtocolInfo=A_ARG_TYPE_ProtocolInfo "
	 "-PeerConnectionManager=A_ARG_TYPE_ConnectionManager "
	 "-PeerConnectionID=A_ARG_TYPE_ConnectionID -Direction=A_ARG_TYPE_Direction "
	 "-Status=A_ARG_TYPE_ConnectionStatus"},
};

#define DESC(v, a) {v, ARRAY_SIZE_(v), a, ARRAY_SIZE_(a)}
#define ARRAY_SIZE_(x) ((int)(sizeof(x) / sizeof((x)[0])))

static const struct sdesc sdescs[SVC_COUNT] = {
	[SVC_AVT] = DESC(avt_vars, avt_acts),
	[SVC_RCS] = DESC(rcs_vars, rcs_acts),
	[SVC_CM] = DESC(cm_vars, cm_acts),
};

void upnp_scpd(enum upnp_svc s, struct sb *b)
{
	const struct sdesc *d = &sdescs[s];

	sb_puts(b, "<?xml version=\"1.0\"?>\r\n<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">"
		   "<specVersion><major>1</major><minor>0</minor></specVersion><actionList>");
	for (int i = 0; i < d->nacts; i++) {
		const char *p = d->acts[i].args;

		sb_printf(b, "<action><name>%s</name>", d->acts[i].name);
		if (*p) {
			sb_puts(b, "<argumentList>");
		}
		while (*p) {
			char dir = *p++;
			const char *eq = strchr(p, '=');
			const char *end = strchr(p, ' ');

			if (!end) {
				end = p + strlen(p);
			}
			sb_printf(b, "<argument><name>%.*s</name><direction>%s</direction>"
				     "<relatedStateVariable>%.*s</relatedStateVariable></argument>",
				  (int)(eq - p), p, dir == '+' ? "in" : "out",
				  (int)(end - eq - 1), eq + 1);
			p = *end ? end + 1 : end;
		}
		sb_puts(b, d->acts[i].args[0] ? "</argumentList></action>" : "</action>");
	}
	sb_puts(b, "</actionList><serviceStateTable>");
	for (int i = 0; i < d->nvars; i++) {
		const struct svar *v = &d->vars[i];

		sb_printf(b, "<stateVariable sendEvents=\"%s\"><name>%s</name><dataType>%s</dataType>",
			  v->events ? "yes" : "no", v->name, v->type);
		if (v->allowed) {
			const char *p = v->allowed;

			sb_puts(b, "<allowedValueList>");
			while (*p) {
				const char *c = strchr(p, ',');
				int n = c ? (int)(c - p) : (int)strlen(p);

				sb_printf(b, "<allowedValue>%.*s</allowedValue>", n, p);
				p += n + (c ? 1 : 0);
			}
			sb_puts(b, "</allowedValueList>");
		}
		if (v->range) {
			char *e1, *e2;
			unsigned long lo = strtoul(v->range, &e1, 10);
			unsigned long hi = strtoul(e1 + 1, &e2, 10);
			unsigned long st = strtoul(e2 + 1, NULL, 10);

			sb_printf(b, "<allowedValueRange><minimum>%lu</minimum><maximum>%lu</maximum>"
				     "<step>%lu</step></allowedValueRange>", lo, hi, st);
		}
		sb_puts(b, "</stateVariable>");
	}
	sb_puts(b, "</serviceStateTable></scpd>");
}

/* ---- actions -------------------------------------------------------------------------------- */

static void out(struct sb *r, const char *name, const char *value)
{
	sb_printf(r, "<%s>", name);
	sb_puts_escaped(r, value);
	sb_printf(r, "</%s>", name);
}

static void out_int(struct sb *r, const char *name, long value)
{
	sb_printf(r, "<%s>%ld</%s>", name, value, name);
}

const char *upnp_transport_state(const char **status)
{
	const struct dlna_ops *o = upnp.cfg->ops;
	enum dlna_state st = o->state ? o->state(o->ctx) : DLNA_NO_MEDIA;

	if (upnp.uri_rejected) {
		st = DLNA_ERROR;
	}
	if (status) {
		*status = st == DLNA_ERROR ? "ERROR_OCCURRED" : "OK";
	}
	if (st == DLNA_NO_MEDIA && upnp.uri[0]) {
		st = DLNA_STOPPED;
	}
	switch (st) {
	case DLNA_PLAYING:
		return "PLAYING";
	case DLNA_PAUSED:
		return "PAUSED_PLAYBACK";
	case DLNA_TRANSITIONING:
		return "TRANSITIONING";
	case DLNA_NO_MEDIA:
		return "NO_MEDIA_PRESENT";
	default:
		return "STOPPED";
	}
}

static const char *transport_actions(void)
{
	const char *s = upnp_transport_state(NULL);

	if (upnp.uri_rejected) {
		return "";
	}
	if (!strcmp(s, "PLAYING")) {
		return "Pause,Stop,Seek";
	}
	if (!strcmp(s, "PAUSED_PLAYBACK")) {
		return "Play,Stop,Seek";
	}
	if (!strcmp(s, "TRANSITIONING")) {
		return "Stop";
	}
	return upnp.uri[0] ? "Play,Seek" : "";
}

static bool instance_ok(const char *body)
{
	char v[16];

	/* a single instance, number 0; controllers that leave it out are accepted */
	return !soap_arg(body, "InstanceID", v, sizeof(v)) || atoi(v) == 0;
}

static int result(int ret)
{
	return ret < 0 ? UPNP_ERR_FAILED : 0;
}

static int avt_action(const char *a, const char *body, struct sb *r)
{
	const struct dlna_ops *o = upnp.cfg->ops;
	char t[16], dur[16], pos[16];
	uint32_t d = o->duration_ms ? o->duration_ms(o->ctx) : 0;
	uint32_t p = o->position_ms ? o->position_ms(o->ctx) : 0;
	const char *status;
	const char *state;

	if (!instance_ok(body)) {
		return UPNP_ERR_BAD_INSTANCE;
	}
	fmt_time(d, dur, sizeof(dur));
	fmt_time(p, pos, sizeof(pos));

	if (!strcmp(a, "SetAVTransportURI")) {
		static char uri[sizeof(upnp.uri)];
		char *meta = malloc(CONFIG_DLNA_MAX_REQUEST);
		char title[sizeof(upnp.title)] = "";
		uint32_t width, height;
		int ret;

		if (!soap_arg(body, "CurrentURI", uri, sizeof(uri))) {
			free(meta);
			upnp.uri_rejected = true;
			if (o->stop) {
				o->stop(o->ctx);
			}
			return UPNP_ERR_INVALID_ARGS;
		}
		if (meta && soap_arg(body, "CurrentURIMetaData", meta, CONFIG_DLNA_MAX_REQUEST)) {
			didl_title(meta, title, sizeof(title));
			if (o->check_resolution && didl_resolution(meta, uri, &width, &height)) {
				ret = o->check_resolution(o->ctx, width, height);
				if (ret < 0) {
					free(meta);
					upnp.uri_rejected = true;
					return UPNP_ERR_NO_CONTENT;
				}
			}
		}
		free(meta);
		if (!o->set_uri) {
			return UPNP_ERR_FAILED;
		}
		if (!uri[0]) {
			/* an empty address clears the transport */
			if (o->stop) {
				o->stop(o->ctx);
			}
			upnp.uri[0] = upnp.title[0] = '\0';
			upnp.uri_rejected = false;
			return 0;
		}
		ret = o->set_uri(o->ctx, uri, title);
		if (ret < 0) {
			upnp.uri_rejected = true;
			return ret == -ENOTSUP ? UPNP_ERR_NO_CONTENT : UPNP_ERR_FAILED;
		}
		strcpy(upnp.uri, uri);
		strcpy(upnp.title, title);
		upnp.uri_rejected = false;
		return 0;
	}
	if (!strcmp(a, "GetMediaInfo")) {
		out_int(r, "NrTracks", upnp.uri[0] ? 1 : 0);
		out(r, "MediaDuration", dur);
		out(r, "CurrentURI", upnp.uri);
		out(r, "CurrentURIMetaData", "");
		out(r, "NextURI", "");
		out(r, "NextURIMetaData", "");
		out(r, "PlayMedium", upnp.uri[0] ? "NETWORK" : "NONE");
		out(r, "RecordMedium", "NOT_IMPLEMENTED");
		out(r, "WriteStatus", "NOT_IMPLEMENTED");
		return 0;
	}
	if (!strcmp(a, "GetTransportInfo")) {
		state = upnp_transport_state(&status);
		out(r, "CurrentTransportState", state);
		out(r, "CurrentTransportStatus", status);
		out(r, "CurrentSpeed", "1");
		return 0;
	}
	if (!strcmp(a, "GetPositionInfo")) {
		out_int(r, "Track", upnp.uri[0] ? 1 : 0);
		out(r, "TrackDuration", dur);
		out(r, "TrackMetaData", "");
		out(r, "TrackURI", upnp.uri);
		out(r, "RelTime", pos);
		out(r, "AbsTime", pos);
		out_int(r, "RelCount", 2147483647L);
		out_int(r, "AbsCount", 2147483647L);
		return 0;
	}
	if (!strcmp(a, "GetDeviceCapabilities")) {
		out(r, "PlayMedia", "NETWORK");
		out(r, "RecMedia", "NOT_IMPLEMENTED");
		out(r, "RecQualityModes", "NOT_IMPLEMENTED");
		return 0;
	}
	if (!strcmp(a, "GetTransportSettings")) {
		out(r, "PlayMode", "NORMAL");
		out(r, "RecQualityMode", "NOT_IMPLEMENTED");
		return 0;
	}
	if (!strcmp(a, "GetCurrentTransportActions")) {
		out(r, "Actions", transport_actions());
		return 0;
	}
	if (!strcmp(a, "Stop")) {
		return o->stop ? result(o->stop(o->ctx)) : UPNP_ERR_NOT_AVAILABLE;
	}
	if (!strcmp(a, "Play")) {
		if (!upnp.uri[0] || upnp.uri_rejected) {
			return UPNP_ERR_NOT_AVAILABLE;
		}
		return o->play ? result(o->play(o->ctx)) : UPNP_ERR_NOT_AVAILABLE;
	}
	if (!strcmp(a, "Pause")) {
		return o->pause ? result(o->pause(o->ctx)) : UPNP_ERR_NOT_AVAILABLE;
	}
	if (!strcmp(a, "Seek")) {
		uint32_t ms;

		if (!soap_arg(body, "Unit", t, sizeof(t))) {
			return UPNP_ERR_INVALID_ARGS;
		}
		if (strcmp(t, "REL_TIME") && strcmp(t, "ABS_TIME")) {
			return UPNP_ERR_SEEK_MODE;
		}
		if (!soap_arg(body, "Target", t, sizeof(t)) || !parse_time(t, &ms)) {
			return UPNP_ERR_SEEK_TARGET;
		}
		return o->seek ? result(o->seek(o->ctx, ms)) : UPNP_ERR_NOT_AVAILABLE;
	}
	if (!strcmp(a, "Next") || !strcmp(a, "Previous")) {
		return UPNP_ERR_NOT_AVAILABLE;
	}
	if (!strcmp(a, "SetPlayMode")) {
		return 0;
	}
	return UPNP_ERR_INVALID_ACTION;
}

static int volume_now(void)
{
	const struct dlna_ops *o = upnp.cfg->ops;

	if (upnp.muted) {
		return upnp.volume_before_mute;
	}
	return o->get_volume ? o->get_volume(o->ctx) : 100;
}

static int rcs_action(const char *a, const char *body, struct sb *r)
{
	const struct dlna_ops *o = upnp.cfg->ops;
	char v[16];

	if (!instance_ok(body)) {
		return UPNP_ERR_BAD_INSTANCE;
	}
	if (!strcmp(a, "ListPresets")) {
		out(r, "CurrentPresetNameList", "FactoryDefaults");
	} else if (!strcmp(a, "SelectPreset")) {
		return 0;
	} else if (!strcmp(a, "GetMute")) {
		out_int(r, "CurrentMute", upnp.muted);
	} else if (!strcmp(a, "GetVolume")) {
		out_int(r, "CurrentVolume", volume_now());
	} else if (!strcmp(a, "SetVolume")) {
		int vol;

		if (!soap_arg(body, "DesiredVolume", v, sizeof(v))) {
			return UPNP_ERR_INVALID_ARGS;
		}
		vol = atoi(v);
		vol = vol < 0 ? 0 : (vol > 100 ? 100 : vol);
		if (upnp.muted) {
			upnp.volume_before_mute = vol;
		} else if (o->set_volume) {
			o->set_volume(o->ctx, vol);
		}
	} else if (!strcmp(a, "SetMute")) {
		bool m;

		if (!soap_arg(body, "DesiredMute", v, sizeof(v))) {
			return UPNP_ERR_INVALID_ARGS;
		}
		m = atoi(v) != 0 || !strcmp(v, "true") || !strcmp(v, "yes");
		if (m && !upnp.muted) {
			upnp.volume_before_mute = o->get_volume ? o->get_volume(o->ctx) : 100;
			if (o->set_volume) {
				o->set_volume(o->ctx, 0);
			}
		} else if (!m && upnp.muted && o->set_volume) {
			o->set_volume(o->ctx, upnp.volume_before_mute);
		}
		upnp.muted = m;
	} else {
		return UPNP_ERR_INVALID_ACTION;
	}
	return 0;
}

static int cm_action(const char *a, const char *body, struct sb *r)
{
	(void)body;
	if (!strcmp(a, "GetProtocolInfo")) {
		out(r, "Source", "");
		out(r, "Sink", SINK_PROTOCOLS);
	} else if (!strcmp(a, "GetCurrentConnectionIDs")) {
		out(r, "ConnectionIDs", "0");
	} else if (!strcmp(a, "GetCurrentConnectionInfo")) {
		out_int(r, "RcsID", 0);
		out_int(r, "AVTransportID", 0);
		out(r, "ProtocolInfo", "");
		out(r, "PeerConnectionManager", "");
		out_int(r, "PeerConnectionID", -1);
		out(r, "Direction", "Input");
		out(r, "Status", "OK");
	} else {
		return UPNP_ERR_INVALID_ACTION;
	}
	return 0;
}

void upnp_event(enum upnp_svc s, struct sb *b)
{
	sb_puts(b, "<?xml version=\"1.0\"?>\r\n"
		   "<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\">");
	if (s == SVC_CM) {
		sb_puts(b, "<e:property><SourceProtocolInfo></SourceProtocolInfo></e:property>"
			   "<e:property><SinkProtocolInfo>" SINK_PROTOCOLS "</SinkProtocolInfo></e:property>"
			   "<e:property><CurrentConnectionIDs>0</CurrentConnectionIDs></e:property>");
	} else {
		size_t inner_size = 2048 + 12 * strlen(upnp.uri);
		char *inner = malloc(inner_size);
		struct sb in;

		if (!inner) {
			b->over = true;
			return;
		}
		sb_init(&in, inner, inner_size);
		sb_printf(&in, "<Event xmlns=\"urn:schemas-upnp-org:metadata-1-0/%s/\">"
			      "<InstanceID val=\"0\">", s == SVC_AVT ? "AVT" : "RCS");
		if (s == SVC_AVT) {
			const struct dlna_ops *o = upnp.cfg->ops;
			const char *status;
			const char *state = upnp_transport_state(&status);
			char duration[16];

			fmt_time(o->duration_ms ? o->duration_ms(o->ctx) : 0, duration, sizeof(duration));
			sb_printf(&in, "<TransportState val=\"%s\"/><TransportStatus val=\"%s\"/>"
				      "<CurrentTransportActions val=\"%s\"/><TransportPlaySpeed val=\"1\"/>"
				      "<CurrentPlayMode val=\"NORMAL\"/><NumberOfTracks val=\"%d\"/>"
				      "<CurrentTrack val=\"%d\"/><CurrentTrackDuration val=\"%s\"/>"
				      "<CurrentMediaDuration val=\"%s\"/><AVTransportURI val=\"",
				  state, status, transport_actions(), !!upnp.uri[0], !!upnp.uri[0],
				  duration, duration);
			sb_puts_escaped(&in, upnp.uri);
			sb_puts(&in, "\"/><CurrentTrackURI val=\"");
			sb_puts_escaped(&in, upnp.uri);
			sb_puts(&in, "\"/>");
		} else {
			sb_printf(&in, "<Volume channel=\"Master\" val=\"%d\"/>"
				      "<Mute channel=\"Master\" val=\"%d\"/>"
				      "<PresetNameList val=\"FactoryDefaults\"/>", volume_now(), upnp.muted);
		}
		sb_puts(&in, "</InstanceID></Event>");
		sb_puts(b, "<e:property><LastChange>");
		sb_puts_escaped(b, inner);
		sb_puts(b, "</LastChange></e:property>");
		b->over |= in.over;
		free(inner);
	}
	sb_puts(b, "</e:propertyset>");
}

int upnp_action(enum upnp_svc s, const char *action, const char *body, struct sb *resp)
{
	char type[80];
	size_t mark;
	int ret;

	upnp_service_type(s, type, sizeof(type));
	sb_printf(resp, "<u:%sResponse xmlns:u=\"%s\">", action, type);
	mark = resp->len;
	switch (s) {
	case SVC_AVT:
		ret = avt_action(action, body, resp);
		break;
	case SVC_RCS:
		ret = rcs_action(action, body, resp);
		break;
	default:
		ret = cm_action(action, body, resp);
	}
	(void)mark;
	if (ret == 0) {
		sb_printf(resp, "</u:%sResponse>", action);
	}
	return ret;
}
