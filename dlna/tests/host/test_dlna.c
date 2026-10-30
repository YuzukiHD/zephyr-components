/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Host tests of the protocol layer: text helpers, SOAP argument reading, the descriptions and the actions */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "soap.h"
#include "upnp.h"
#include "util.h"

static int failures;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* a recording player */
static struct {
	char uri[DLNA_URI_SIZE], title[128];
	int plays, pauses, stops, vol;
	uint32_t seek;
	enum dlna_state state;
	uint32_t width, height;
} fake = {.vol = 50, .state = DLNA_NO_MEDIA};

static int f_set_uri(void *c, const char *u, const char *t)
{
	(void)c;
	if (!strncmp(u, "bad:", 4)) {
		return -95;
	}
	strcpy(fake.uri, u);
	strcpy(fake.title, t);
	fake.state = DLNA_TRANSITIONING;
	return 0;
}
static int f_play(void *c) { (void)c; fake.plays++; fake.state = DLNA_PLAYING; return 0; }
static int f_resolution(void *c, uint32_t w, uint32_t h)
{
	(void)c; fake.width = w; fake.height = h;
	return ((w <= 640 && h <= 360) || (w <= 360 && h <= 640)) ? 0 : -1;
}
static int f_pause(void *c) { (void)c; fake.pauses++; fake.state = DLNA_PAUSED; return 0; }
static int f_stop(void *c) { (void)c; fake.stops++; fake.state = DLNA_STOPPED; return 0; }
static int f_seek(void *c, uint32_t ms) { (void)c; fake.seek = ms; return 0; }
static enum dlna_state f_state(void *c) { (void)c; return fake.state; }
static uint32_t f_pos(void *c) { (void)c; return 3723000; }
static uint32_t f_dur(void *c) { (void)c; return 7200000; }
static int f_getvol(void *c) { (void)c; return fake.vol; }
static int f_setvol(void *c, int v) { (void)c; fake.vol = v; return 0; }

static const struct dlna_ops ops = {
	.set_uri = f_set_uri, .play = f_play, .pause = f_pause, .stop = f_stop, .seek = f_seek,
	.check_resolution = f_resolution,
	.state = f_state, .position_ms = f_pos, .duration_ms = f_dur, .get_volume = f_getvol,
	.set_volume = f_setvol,
};
static const struct dlna_config cfg = {
	.name = "Neko & <Box>", .uuid = "12345678-1234-5678-1234-567812345678", .ops = &ops,
};

static char buf[UPNP_EVENT_SIZE];

static int act(enum upnp_svc s, const char *name, const char *args, struct sb *b)
{
	char body[CONFIG_DLNA_MAX_REQUEST];

	snprintf(body, sizeof(body), "<s:Envelope><s:Body><u:%s xmlns:u=\"x\">%s</u:%s></s:Body></s:Envelope>",
		 name, args, name);
	sb_init(b, buf, sizeof(buf));
	return upnp_action(s, name, body, b);
}

static void test_util(void)
{
	char s[64];
	uint32_t ms;
	struct sb b;
	char small[8];

	fmt_time(3723000, s, sizeof(s));
	CHECK(!strcmp(s, "1:02:03"));
	fmt_time(100 * 3600000u, s, sizeof(s));
	CHECK(!strcmp(s, "100:00:00"));
	CHECK(parse_time("1:02:03", &ms) && ms == 3723000);
	CHECK(parse_time("0:00:10.500", &ms) && ms == 10500);
	CHECK(parse_time("+00:01:00.5", &ms) && ms == 60500);
	CHECK(!parse_time("1:99:03", &ms));
	CHECK(!parse_time("12", &ms));
	CHECK(!parse_time("1:02:03x", &ms));

	strcpy(s, "a&lt;b&amp;&quot;&#65;&#x4e2d;");
	xml_unescape(s);
	CHECK(!strcmp(s, "a<b&\"A\xe4\xb8\xad"));

	sb_init(&b, buf, sizeof(buf));
	sb_puts_escaped(&b, "<a href=\"x\">&'");
	CHECK(!strcmp(b.buf, "&lt;a href=&quot;x&quot;&gt;&amp;&apos;"));
	sb_init(&b, small, sizeof(small));
	sb_puts(&b, "0123456789");
	CHECK(b.over && small[0] == '\0');

	CHECK(header_find("A: 1\r\nsoapaction: \"x#Play\"\r\n", "SOAPACTION") != NULL);
	CHECK(header_find("A: 1\r\nSoapActionX: 1\r\n", "SOAPACTION") == NULL);
}

static void test_soap(void)
{
	char v[64];
	uint32_t width, height;
	const char *body = "<s:Body><u:Seek xmlns:u=\"x\"><InstanceID>0</InstanceID><Unit>REL_TIME</Unit>"
			   "<Target>0:01:00</Target><Empty/><E2></E2></u:Seek></s:Body>";

	CHECK(soap_arg(body, "Unit", v, sizeof(v)) && !strcmp(v, "REL_TIME"));
	CHECK(soap_arg(body, "Target", v, sizeof(v)) && !strcmp(v, "0:01:00"));
	CHECK(soap_arg(body, "Empty", v, sizeof(v)) && v[0] == '\0');
	CHECK(soap_arg(body, "E2", v, sizeof(v)) && v[0] == '\0');
	CHECK(!soap_arg(body, "Missing", v, sizeof(v)));
	CHECK(!soap_arg(body, "Target", v, 3));
	CHECK(!soap_arg("<ns:Unit2>x</ns:Unit2>", "Unit", v, sizeof(v)));
	CHECK(soap_arg("<ns:Unit>x</ns:Unit>", "Unit", v, sizeof(v)) && !strcmp(v, "x"));
	CHECK(soap_arg("<Unit\n >x</Unit>", "Unit", v, sizeof(v)) && !strcmp(v, "x"));
	CHECK(soap_arg("<V>&amp;&amp;&amp;&amp;</V>", "V", v, 5) && !strcmp(v, "&&&&"));
	CHECK(soap_arg("<V><![CDATA[x&y&amp;z]]></V>", "V", v, sizeof(v)) &&
	      !strcmp(v, "x&y&amp;z"));
	CHECK(!soap_arg("<V><![CDATA[bad</V>", "V", v, sizeof(v)));
	CHECK(!soap_arg("<V/>", "V", v, 0));
	CHECK(didl_resolution("<x><res protocolInfo='a:b' resolution = \"640x360\">"
			      "http://x/v?a=1&amp;b=2</res></x>", "http://x/v?a=1&b=2", &width, &height));
	CHECK(width == 640 && height == 360);
	CHECK(didl_resolution("<x><res resolution='1920x1080'>http://x/other</res>"
			      "<res resolution='360x640'>http://x/v</res></x>",
			      "http://x/v", &width, &height) && width == 360 && height == 640);
	CHECK(!didl_resolution("<res resolution='640x360'>http://x/other</res>",
			       "http://x/v", &width, &height));
	CHECK(!didl_resolution("<res resolution='-640x360'>http://x/v</res>",
			       "http://x/v", &width, &height));
	CHECK(!didl_resolution("<res resolution='640x0'>http://x/v</res>",
			       "http://x/v", &width, &height));
	CHECK(!didl_resolution("<res resolution='4294967296x360'>http://x/v</res>",
			       "http://x/v", &width, &height));

	didl_title("<DIDL-Lite><item><dc:title>Tom &amp; Jerry</dc:title></item></DIDL-Lite>", v, sizeof(v));
	CHECK(!strcmp(v, "Tom & Jerry"));
	didl_title("<DIDL-Lite><item></item></DIDL-Lite>", v, sizeof(v));
	CHECK(v[0] == '\0');
	didl_title("<x><dc:title>\xe4\xb8\xad\xe6\x96\x87</dc:title></x>", v, 5);	/* 4 bytes fit: one char */
	CHECK(!strcmp(v, "\xe4\xb8\xad"));
}

static void test_descriptions(void)
{
	struct sb b;

	upnp.cfg = &cfg;
	sb_init(&b, buf, sizeof(buf));
	upnp_description(&b);
	CHECK(!b.over);
	CHECK(strstr(buf, "<friendlyName>Neko &amp; &lt;Box&gt;</friendlyName>"));
	CHECK(strstr(buf, "<UDN>uuid:12345678-1234-5678-1234-567812345678</UDN>"));
	CHECK(strstr(buf, "<controlURL>/AVTransport/control</controlURL>"));
	CHECK(strstr(buf, "urn:schemas-upnp-org:service:ConnectionManager:1"));

	for (int i = 0; i < SVC_COUNT; i++) {
		sb_init(&b, buf, sizeof(buf));
		upnp_scpd(i, &b);
		CHECK(!b.over);
		CHECK(strstr(buf, "</scpd>"));
	}
	sb_init(&b, buf, sizeof(buf));
	upnp_scpd(SVC_AVT, &b);
	CHECK(strstr(buf, "<name>SetAVTransportURI</name><argumentList><argument><name>InstanceID</name>"
			  "<direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID"));
	CHECK(strstr(buf, "<name>CurrentTransportState</name><direction>out</direction>"
			  "<relatedStateVariable>TransportState</relatedStateVariable>"));
	CHECK(strstr(buf, "<action><name>Stop</name><argumentList>"));
	CHECK(strstr(buf, "<allowedValue>PAUSED_PLAYBACK</allowedValue>"));
	CHECK(strstr(buf, "<stateVariable sendEvents=\"yes\"><name>LastChange</name>"));
	sb_init(&b, buf, sizeof(buf));
	upnp_scpd(SVC_RCS, &b);
	CHECK(strstr(buf, "<minimum>0</minimum><maximum>100</maximum><step>1</step>"));
	printf("avt scpd size %zu\n", b.len);
}

static void test_actions(void)
{
	struct sb b;

	upnp.cfg = &cfg;
	upnp.uri[0] = upnp.title[0] = '\0';
	upnp.muted = false;

	CHECK(act(SVC_AVT, "GetTransportInfo", "<InstanceID>0</InstanceID>", &b) == 0);
	CHECK(strstr(buf, "<CurrentTransportState>NO_MEDIA_PRESENT</CurrentTransportState>"));
	CHECK(act(SVC_AVT, "Play", "<InstanceID>0</InstanceID><Speed>1</Speed>", &b) == UPNP_ERR_NOT_AVAILABLE);
	CHECK(act(SVC_AVT, "GetTransportInfo", "<InstanceID>1</InstanceID>", &b) == UPNP_ERR_BAD_INSTANCE);
	CHECK(act(SVC_AVT, "Bogus", "<InstanceID>0</InstanceID>", &b) == UPNP_ERR_INVALID_ACTION);

	CHECK(act(SVC_AVT, "SetAVTransportURI",
		  "<InstanceID>0</InstanceID><CurrentURI>http://1.2.3.4/a.mp4?x=1&amp;y=2</CurrentURI>"
		  "<CurrentURIMetaData>&lt;DIDL-Lite&gt;&lt;item&gt;&lt;dc:title&gt;Film &amp;amp; Co&lt;/dc:title&gt;"
		  "&lt;/item&gt;&lt;/DIDL-Lite&gt;</CurrentURIMetaData>", &b) == 0);
	CHECK(!strcmp(fake.uri, "http://1.2.3.4/a.mp4?x=1&y=2"));
	CHECK(!strcmp(fake.title, "Film & Co"));
	CHECK(strstr(buf, "<u:SetAVTransportURIResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"></u:SetAVTransportURIResponse>"));
	CHECK(act(SVC_AVT, "SetAVTransportURI", "<InstanceID>0</InstanceID><CurrentURI>bad:x</CurrentURI>"
		  "<CurrentURIMetaData></CurrentURIMetaData>", &b) == UPNP_ERR_NO_CONTENT);
	CHECK(act(SVC_AVT, "SetAVTransportURI", "<InstanceID>0</InstanceID>", &b) == UPNP_ERR_INVALID_ARGS);
	CHECK(act(SVC_AVT, "Play", "", &b) == UPNP_ERR_NOT_AVAILABLE && fake.plays == 0);
	CHECK(act(SVC_AVT, "GetTransportInfo", "", &b) == 0 && strstr(buf, "ERROR_OCCURRED"));
	CHECK(act(SVC_AVT, "SetAVTransportURI",
		  "<CurrentURI>http://1.2.3.4/a.mp4?x=1&amp;y=2</CurrentURI>", &b) == 0);

	CHECK(act(SVC_AVT, "GetTransportInfo", "<InstanceID>0</InstanceID>", &b) == 0);
	CHECK(strstr(buf, "<CurrentTransportState>TRANSITIONING</CurrentTransportState>"));
	CHECK(act(SVC_AVT, "Play", "<InstanceID>0</InstanceID><Speed>1</Speed>", &b) == 0 && fake.plays == 1);
	CHECK(act(SVC_AVT, "GetPositionInfo", "<InstanceID>0</InstanceID>", &b) == 0);
	CHECK(strstr(buf, "<TrackDuration>2:00:00</TrackDuration>"));
	CHECK(strstr(buf, "<RelTime>1:02:03</RelTime>"));
	CHECK(strstr(buf, "<TrackURI>http://1.2.3.4/a.mp4?x=1&amp;y=2</TrackURI>"));
	CHECK(act(SVC_AVT, "Pause", "<InstanceID>0</InstanceID>", &b) == 0 && fake.pauses == 1);
	CHECK(act(SVC_AVT, "GetCurrentTransportActions", "<InstanceID>0</InstanceID>", &b) == 0);
	CHECK(strstr(buf, "<Actions>Play,Stop,Seek</Actions>"));
	CHECK(act(SVC_AVT, "Seek", "<InstanceID>0</InstanceID><Unit>REL_TIME</Unit><Target>0:00:30</Target>", &b) == 0);
	CHECK(fake.seek == 30000);
	CHECK(act(SVC_AVT, "Seek", "<InstanceID>0</InstanceID><Unit>TRACK_NR</Unit><Target>1</Target>", &b) == UPNP_ERR_SEEK_MODE);
	CHECK(act(SVC_AVT, "Seek", "<InstanceID>0</InstanceID><Unit>REL_TIME</Unit><Target>junk</Target>", &b) == UPNP_ERR_SEEK_TARGET);
	CHECK(act(SVC_AVT, "Stop", "<InstanceID>0</InstanceID>", &b) == 0 && fake.stops == 2);
	fake.state = DLNA_ERROR;
	CHECK(act(SVC_AVT, "GetTransportInfo", "<InstanceID>0</InstanceID>", &b) == 0);
	CHECK(strstr(buf, "STOPPED") && strstr(buf, "ERROR_OCCURRED"));

	/* volume and mute */
	CHECK(act(SVC_RCS, "GetVolume", "<InstanceID>0</InstanceID><Channel>Master</Channel>", &b) == 0);
	CHECK(strstr(buf, "<CurrentVolume>50</CurrentVolume>"));
	CHECK(act(SVC_RCS, "SetVolume", "<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredVolume>130</DesiredVolume>", &b) == 0);
	CHECK(fake.vol == 100);
	CHECK(act(SVC_RCS, "SetMute", "<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredMute>1</DesiredMute>", &b) == 0);
	CHECK(fake.vol == 0);
	CHECK(act(SVC_RCS, "GetVolume", "<InstanceID>0</InstanceID><Channel>Master</Channel>", &b) == 0);
	CHECK(strstr(buf, "<CurrentVolume>100</CurrentVolume>"));
	CHECK(act(SVC_RCS, "GetMute", "<InstanceID>0</InstanceID><Channel>Master</Channel>", &b) == 0);
	CHECK(strstr(buf, "<CurrentMute>1</CurrentMute>"));
	CHECK(act(SVC_RCS, "SetMute", "<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredMute>0</DesiredMute>", &b) == 0);
	CHECK(fake.vol == 100);

	CHECK(act(SVC_CM, "GetProtocolInfo", "", &b) == 0);
	CHECK(strstr(buf, "http-get:*:video/mp4:*"));
	CHECK(act(SVC_CM, "GetCurrentConnectionIDs", "", &b) == 0);
	CHECK(act(SVC_CM, "Nope", "", &b) == UPNP_ERR_INVALID_ACTION);
}

static void test_long_uri(void)
{
	char uri[DLNA_URI_SIZE], args[CONFIG_DLNA_MAX_REQUEST];
	const char *prefix = "http://127.0.0.1/video.mp4?token=";
	struct sb b, a;

	memset(uri, 'x', sizeof(uri) - 1);
	memcpy(uri, prefix, strlen(prefix));
	uri[sizeof(uri) - 1] = '\0';
	sb_init(&a, args, sizeof(args));
	sb_puts(&a, "<CurrentURI>");
	sb_puts_escaped(&a, uri);
	sb_puts(&a, "</CurrentURI>");
	CHECK(act(SVC_AVT, "SetAVTransportURI", args, &b) == 0);
	CHECK(!strcmp(fake.uri, uri) && !strcmp(upnp.uri, uri));
	CHECK(act(SVC_AVT, "GetMediaInfo", "", &b) == 0 && strstr(buf, uri));

	/* The XML representation can exceed the URI buffer while its decoded value fits. */
	memset(uri, '&', 1200);
	uri[1200] = '\0';
	sb_init(&a, args, sizeof(args));
	sb_puts(&a, "<CurrentURI>");
	sb_puts_escaped(&a, uri);
	sb_puts(&a, "</CurrentURI>");
	CHECK(act(SVC_AVT, "SetAVTransportURI", args, &b) == 0);
	CHECK(!strcmp(fake.uri, uri));

	memset(uri, 'x', sizeof(uri) - 1);
	uri[sizeof(uri) - 1] = '\0';
	snprintf(args, sizeof(args), "<CurrentURI>%sx</CurrentURI>", uri);
	CHECK(act(SVC_AVT, "SetAVTransportURI", args, &b) == UPNP_ERR_INVALID_ARGS);
	CHECK(act(SVC_AVT, "Play", "", &b) == UPNP_ERR_NOT_AVAILABLE);
}

static void test_events(void)
{
	struct sb b;

	upnp.cfg = &cfg;
	/* A long signed URL must survive both XML layers in LastChange. */
	memset(upnp.uri, '&', sizeof(upnp.uri) - 1);
	upnp.uri[sizeof(upnp.uri) - 1] = '\0';
	for (int svc = 0; svc < SVC_COUNT; svc++) {
		sb_init(&b, buf, sizeof(buf));
		upnp_event(svc, &b);
		CHECK(!b.over);
		CHECK(strstr(buf, "</e:propertyset>"));
		if (svc == SVC_AVT) {
			CHECK(strstr(buf, "&amp;amp;"));
			CHECK(strstr(buf, "CurrentTransportActions"));
		} else if (svc == SVC_RCS) {
			CHECK(strstr(buf, "Volume") && strstr(buf, "Mute"));
		} else {
			CHECK(strstr(buf, "SinkProtocolInfo") && strstr(buf, "CurrentConnectionIDs"));
		}
	}
}

static void test_resolution(void)
{
	struct sb b;
	const char *head = "<CurrentURI>http://x/v</CurrentURI><CurrentURIMetaData>"
			   "&lt;DIDL-Lite&gt;&lt;item&gt;&lt;res resolution=&quot;";
	const char *tail = "&quot;&gt;http://x/v&lt;/res&gt;&lt;/item&gt;&lt;/DIDL-Lite&gt;"
			   "</CurrentURIMetaData>";
	char args[1024];

	snprintf(args, sizeof(args), "%s640x360%s", head, tail);
	CHECK(act(SVC_AVT, "SetAVTransportURI", args, &b) == 0);
	CHECK(fake.width == 640 && fake.height == 360);
	snprintf(args, sizeof(args), "%s1920x1080%s", head, tail);
	CHECK(act(SVC_AVT, "SetAVTransportURI", args, &b) == UPNP_ERR_NO_CONTENT);
	CHECK(act(SVC_AVT, "Play", "", &b) == UPNP_ERR_NOT_AVAILABLE);
	snprintf(args, sizeof(args), "%s360x640%s", head, tail);
	CHECK(act(SVC_AVT, "SetAVTransportURI", args, &b) == 0);
	CHECK(act(SVC_AVT, "Play", "", &b) == 0);
}

int main(void)
{
	test_util();
	test_soap();
	test_descriptions();
	test_actions();
	test_long_uri();
	test_events();
	test_resolution();
	if (failures) {
		printf("FAIL (%d failures)\n", failures);
		return 1;
	}
	printf("PASS (0 failures)\n");
	return 0;
}
