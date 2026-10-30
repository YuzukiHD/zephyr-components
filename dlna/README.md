# dlna

A UPnP AV **MediaRenderer:1** (the "cast to this device" target of phones and PCs) as a Zephyr module.
The module speaks the protocol; the application plays. Add it with `ZEPHYR_EXTRA_MODULES`, enable
`CONFIG_DLNA` and give a table of callbacks (`include/dlna/dlna.h`) to `dlna_start()`.

- `src/ssdp.c` — M-SEARCH answers, alive notices every `DLNA_ANNOUNCE_INTERVAL_S`, byebye on stop. The
  thread follows the network: no address -> no socket; a new address -> group joined again, announced.
- `src/http_srv.c` — one thread, one connection at a time (every answer closes the connection):
  `/description.xml`, `<service>/scpd.xml`, SOAP `<service>/control`, `SUBSCRIBE`/`UNSUBSCRIBE`
  `<service>/event`. All three services send the initial event after the response socket closes.
  AVTransport state/URI/duration changes and RenderingControl volume/mute changes send notices.
  Subscription renewal and rejected subscriptions also close the response socket. SOAP clients
  using `Expect: 100-continue` are supported.
- `src/upnp.c` — AVTransport, RenderingControl, ConnectionManager. The service descriptions are generated
  from the action/variable tables, so the SCPD and the actions cannot drift apart.
- `src/soap.c`, `src/util.c` — argument reading, DIDL title, time format; no OS dependency.

Media addresses may contain up to 4095 bytes. SOAP text capacity is checked after XML entity
decoding; CDATA values are also accepted. A rejected replacement address reports a transport
error and blocks Play until a valid address is set, so a controller cannot accidentally restart
the previous video. The application carries the complete address through its command queue,
HTTP request and redirects. Download diagnostics identify the host, response status and failure
stage without printing signed query parameters.

The optional `dlna_ops.check_resolution` callback checks dimensions supplied by the selected
DIDL `res` element before `set_uri`. Other resources in the metadata are ignored. Missing or
invalid metadata leaves size validation to the player. NekoBox defaults to a 640x360 cast limit
(360x640 portrait is also accepted); it checks the actual MP4 track dimensions independently
before starting decoder threads and cancels oversized transfers with a lower-quality prompt.
There is no standard DLNA command that forces an app to select 360p, so choose it in the sender.

Not done: https addresses are the application's business (the NekoBox fetcher refuses them), playlists
(`Next`/`Previous`/`SetNextAVTransportURI` answer 701/401), MIME checks of the protocol info.

## Tests

`tests/host/run.sh` builds the protocol layer with ASan/UBSan on the host and runs it against a fake
player (descriptions, every action, error codes, volume/mute, XML escaping, time formats). Last line
`PASS (0 failures)`. It also runs the actual HTTP server through POSIX socket/thread adapters:
initial events for every service, repeated subscription/renewal/unsubscribe cycles, error responses,
TCP EOF checks for socket cleanup, volume/mute changes and `Expect: 100-continue`. The event bodies
are parsed as XML. The transport test can target a board:

```sh
python3 tests/host/test_http.py --device BOARD_IP --local-ip HOST_IP --interval 0.08
```

This changes volume during the test and leaves it at 50 percent.

## NekoBox

`nekobox/src/features/dlna` (`CONFIG_NEKOBOX_FEATURE_DLNA`): the DLNA tile on the home screen opens
a receiver page with the device name, start/stop controls, connection status and a Wi-Fi settings
button. The settings switch controls the same receiver; its preference is saved on the SD card.
The receiver keeps Wi-Fi on (`app_net_keep_awake()`) while enabled, including after leaving the page and after the boot network check. Connection pools account for
controller polling, event callbacks, Wi-Fi control links and TCP connections waiting to close.
SSDP tolerates an existing multicast membership when its socket is recreated.
Receiving an address opens the receiver page immediately, showing download progress and specific
errors for unsupported links, SD writes, network downloads or media formats. Leaving a cast video
returns to the receiver page. A cast address is fetched with a small HTTP client into
`CONFIG_NEKOBOX_DLNA_DIR` on the card and played while it arrives. The writer syncs every 64 KiB
before publishing readable bytes. Playback starts when the MP4 movie box (`moov`) is complete and
`CONFIG_NEKOBOX_DLNA_BUFFER_KB` (512 KiB by default) is buffered, or the file is complete. This also
works with chunked responses and bodies without Content-Length. A movie box at the end of the file
requires the download to finish. The current player supports ordinary H.264/AAC MP4 files; fragmented
MP4, playlists and HTTPS are unsupported.

The reader refreshes FAT handles as the file grows. Reads beyond the downloaded prefix wait in the
player threads and can be interrupted by pause, seek, stop or a replacement cast. Header checks
never wait for network data in the UI thread. Commands from the controller are queued to the UI
timer; replacement downloads wait for the old worker to exit and for the player to close before
reusing its resources. Temporary casts are excluded from playback history.

`../nekobox/tests/streaming/run.sh` generates H.264/AAC test files with ffmpeg and runs the real
fetcher, growing reader and MP4 demuxer with ASan/UBSan. A local HTTP server holds back most of each
file, so the test verifies header parsing and reading the first video sample before completion,
then waiting for more data, refreshing the size cached by a FAT-style handle, and cancelling a
blocked read/download. It also checks redirects, chunk extensions/trailers, mixed-case chunked
headers, bodies terminated by close, truncated transfers, HTTP errors, malformed chunk sizes,
movie boxes at the end, extended box sizes and offset overflow. The filesystem adapter models
cached file sizes; it does not replace testing on a real SD card.

Hardware check (2026-10-30): the NekoBox image starts on YuzukiNeko through the RAM XIP loader,
mounts the SD card and keeps the saved Wi-Fi connected while the receiver is enabled. The application
at flash offset `0x80000` was read back and matched the build byte for byte before starting it.
The HTTP test above passed on the board: initial events for all three services, 36 subscription /
renewal / unsubscribe cycles, rejected subscriptions, volume/mute events and `Expect: 100-continue`.
An ordinary HTTP H.264/AAC MP4 (480x272, 25 fps, 20 seconds) started playing with only 768 KiB of
its 1,872,293 bytes supplied. Pause held the position; resume, seek and stop passed. A rejected
HTTPS address opened the DLNA page, and a subsequent HTTP cast recovered and played. Stop returned
to the DLNA page, and SSDP discovery and the device description still worked afterward.
Board results are recorded in `build/nekobox/dlna-board-http-tests.log`,
`build/nekobox/dlna-board-cast-tests.log` and `build/nekobox/dlna-board-serial.log` in the workspace.
HTTPS and playlists remain unsupported.

A later Bilibili attempt returned 402 from SetAVTransportURI, followed by Play retrying the old
test URL and failing to connect. The previous 512-byte URI buffer and its check of the escaped
XML length were corrected. Host tests cover 4095-byte addresses, entity decoding, CDATA, event
escaping and rejected-address playback. A board test passed with a 3054-byte signed-style HTTP
address: the server checked the complete request target, playback started before the download
finished, and pause/resume/seek/stop passed. See
`build/nekobox/dlna-board-long-uri-tests.log`. This test uses local media; the Bilibili check below
uses actual streams from the app.

Subsequent serial logs confirmed actual Bilibili HTTP streams: 700--702-byte addresses were
accepted, the CDN returned HTTP 200, and 640x360 H.264/AAC video played on the board. This verifies
the long-address correction for those streams. Other formats and HTTPS remain subject to the
limitations above.

The 360p limit was also checked on the board: oversized DIDL metadata was rejected before an
HTTP fetch; a 1280x720 MP4 with missing metadata or misleading 640x360 metadata was rejected
before decoder startup while the download was incomplete. A subsequent 640x360 cast with a
3048-byte address started progressively and stopped cleanly. Results:
`build/nekobox/dlna-board-360p-tests.log` and `build/nekobox/dlna-board-360p-serial.log`.
