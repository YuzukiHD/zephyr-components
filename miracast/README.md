# Miracast / Wi-Fi Display receiver

A Zephyr Wi-Fi Display sink for the **Wireless display / Screen mirroring**
function on Android phones that support Miracast. The receiver sample uses
the AIC8800 Wi-Fi Direct interface, the F101 H.264 decoder, the display video
plane, and the on-chip audio codec. It does not require an app on the phone.
Phones offering only Google Cast do not provide a Miracast source.

The protocol core is portable C; network setup and decoding are separate.
The standalone sample and the [NekoBox home screen app](../nekobox/README.md)
share the same receiver and player.

In NekoBox, open **Miracast** and select **NekoBox Miracast** on the phone.
The page starts receiving automatically and provides stop/retry controls.
Leaving the page stops playback and restores normal Wi-Fi and previously
enabled DLNA services. Set `CONFIG_NEKOBOX_FEATURE_MIRACAST=n` to remove it;
`CONFIG_NEKOBOX_MIRACAST_NAME` sets its advertised name.

## Build and connect

From the west workspace, with its Zephyr environment activated:

```sh
west build -b yuzukineko -d build/miracast zephyr-components/miracast/samples/receiver
```

The default image runs from RAM. For the board's SPIF XIP development loader:

```sh
west build -b yuzukineko -d build/miracast-xip zephyr-components/miracast/samples/receiver -- \
  -DEXTRA_CONF_FILE=xip.conf -DDTC_OVERLAY_FILE=boards/yuzukineko_xip.overlay
```

Use the board's existing FEL loader procedure. The XIP application belongs at
NOR offset `0x80000`; the loader maps it at `0x0e000000`. Keep the boot region
and the saved memory parameters at `0x20000` intact. The loader runs from SRAM;
it does not need to be installed in NOR for development.

1. Run the sample and wait for `Miracast: discoverable` on the serial console.
2. On the Android phone, open **Wireless display**, **Screen mirroring**, or
   the manufacturer's equivalent and select **YuzukiNeko Miracast**.
3. The phone joins the group, obtains an address, and supplies an RTSP server.
   The board connects to it on TCP port 7236, negotiates the stream, then
   displays video and plays sound. Serial output reports session states and
   the decoded frame count every five seconds.
4. Disconnect on the phone to end the session. The sample removes the group
   and prepares to receive again.

The sample keeps reopening its WPS push-button window while waiting for a
phone. Run it while intending to pair. It owns the AIC8800 interface and
cannot share an active STA/DLNA connection. No TF card is needed.

`CONFIG_SAMPLE_WFD_NAME` changes the advertised name (1..32 printable ASCII
bytes). The sample advertises the mandatory 640x480p60 CEA timing, constrained
baseline H.264 level 3.1, and LPCM 48 kHz / 16-bit stereo. Set
`CONFIG_SAMPLE_WFD_720P=y` to also offer 1280x720p30; account for its larger
decoder surfaces and network throughput when sizing RAM. Frame delivery may
be slower than the negotiated timing on this board.

## Component API

Add this directory to `ZEPHYR_EXTRA_MODULES` before `find_package(Zephyr)` and
enable `CONFIG_MIRACAST`. Include `<miracast/miracast.h>`.

Once an IP link to a source exists, call `miracast_receiver_start()` with its
numeric IPv4 address, an even RTP port, and media/state callbacks. The
transport binds RTP/RTCP before SETUP, initiates TCP to the source, processes
RTSP and datagrams, sends receiver reports, and requests an IDR after loss.
SETUP's `server_port` selects the source RTP/RTCP ports; explicit ports also
filter incoming datagrams. Without it, RTCP initially uses the RTP sender's
next port and follows valid sender reports. Both `RTP/AVP` and `RTP/AVP/UDP`
denote UDP. The transport sends empty GET_PARAMETER keepalives at half the
negotiated Session timeout, capped at 15 seconds, including while paused.
RTCP BYE from the active source ends the session and releases its sockets.
`miracast_receiver_stop()` joins cleanup and releases all three sockets.
Call start/stop from an application thread, outside the callbacks. Start is
asynchronous: runtime connection failures end with `MIRACAST_CLOSED` and a
serial error. Only one receiver can run at a time.

Callbacks execute on the receiver thread. Media buffers are borrowed until
the callback returns. Copy them into bounded queues and decode elsewhere;
`-ENOBUFS`/`-EAGAIN` drops a payload without ending the session. The shared player
has separate video and audio queues, preserves NAL boundaries across PES
packets, waits for an IDR after discontinuity, and retains displayed frames
until the display no longer uses them. `miracast_receiver_request_idr()` is
safe to call from the decoder thread after an asynchronous failure.

For the Sunxi decoder, display and audio devices, enable
`CONFIG_MIRACAST_PLAYER` and include `<miracast/player.h>`.
Call `miracast_player_start(volume)` with a volume percentage from 0 to 100,
then pass `miracast_player_media` as the receiver's media callback.
After stopping the receiver, call `miracast_player_stop()` to join decoder
cleanup and release video/audio resources. Player start/stop belong to the
application or UI thread. `miracast_player_frames()` reports displayed frames
since start, allowing the UI to hide its waiting message after a picture.

For a different transport, use `miracast_create()`, feed TCP chunks to
`miracast_rtsp()` and UDP datagrams to `miracast_rtp()`, then destroy the
session. All core calls belong to one thread. The `send` callback transmits a
complete RTSP message. The core also supports PAUSE, resume, TEARDOWN and IDR
requests. `miracast_flush()` is for a clean end of a test stream; never flush
damaged packets into the decoder.
Caller-driven transports can use `miracast_keepalive()` to send an empty
GET_PARAMETER; requests and successful replies carry the negotiated Session.

| Media callback | Payload |
| --- | --- |
| `MIRACAST_H264` | Complete PES elementary payload, Annex B; a NAL can span multiple PES packets |
| `MIRACAST_LPCM` | WFD's four-byte audio header followed by signed big-endian PCM; `miracast_lpcm_decode()` produces stereo S16LE |
| `MIRACAST_AAC` | ADTS bytes, only offered when `stream.aac` is enabled by an application with an AAC decoder |

PTS values use the 90 kHz, 33-bit MPEG clock; `-1` means absent. AAC may include
several ADTS frames in a PES; its PTS applies to the first. A discontinuity
marks a stream change, loss, buffer overflow, or dropped callback. The default
maximum video PES payload is 256 KiB and audio is bounded to 32 KiB. Allocation
and malformed-input errors are returned as negative errno values.

## Protocol coverage and limits

- WFD M1/M2 OPTIONS, M3 capability query, M4 stream selection, M5 trigger,
  M6 SETUP and M7 PLAY; GET_PARAMETER keepalive, PAUSE/resume, teardown and
  `wfd_idr_request`. Session identifiers, timeouts and selected client ports
  are checked; requests naming a different session receive status 454.
- RTP v2 payload type 33, optional CSRC/extension/padding, sequence wrap,
  duplicate/late packet rejection, loss and SSRC-change recovery.
- MPEG-TS PAT/PMT section reassembly and CRC checking, adaptation fields,
  continuity counters, bounded/unbounded PES and PTS. H.264, WFD LPCM stream
  type `0x83`, and optional AAC stream type `0x0f`.
- Compound RTCP receiver reports/SDES, sender-report timing and BYE. Media is
  accepted only from the configured source IP and negotiated source ports.

HDCP/protected content, UIBC/touch return, interleaved TCP media, RTP FEC,
packet reordering, standby, simultaneous STA/GO, and Wi-Fi Direct group-client
operation are not implemented. The sample offers LPCM only; AAC is an option
for applications using the core. A sender that requires different H.264
profiles or resolutions will fail negotiation rather than silently exceed
the advertised capabilities.

## Verification

```sh
sh zephyr-components/miracast/tests/host/run.sh
sh zephyr-components/aic8800/tests/p2p/run.sh
sh zephyr-components/nekobox/tests/miracast/run.sh
```

Requires GCC, Python 3 and FFmpeg with libx264. Tests run with ASan/UBSan and
exercise fragmented/coalesced RTSP, M1-M7, pause/resume/teardown, capability
rejection, WFD LPCM conversion, RTP loss/sequence wrap and header options,
fragmented PSI, media limits, malformed packets, and socket cleanup across
twenty start/stop cycles. A generated H.264/AAC MPEG-TS stream is demultiplexed
and compared byte-for-byte against FFmpeg; PTS values are checked with
ffprobe. An independent loopback RTSP source drives the actual socket
receiver and verifies its RTCP report.
It also negotiates nonadjacent source RTP/RTCP ports, checks receiver reports
before the first sender report, verifies periodic keepalives with a two-second
Session timeout, and ends sessions through both RTSP TEARDOWN and RTCP BYE.
The P2P test runs the control parser and group creation with the radio stubbed;
it checks 8..63-byte passphrases, long-input rejection and generated defaults.
The NekoBox tests check UI/worker thread ownership, DLNA cleanup before radio
handoff, asynchronous stop/restart, connection retry and failure recovery.

The receiver sample builds for `yuzukineko` in RAM and XIP configurations.
Android discovery, negotiation, displayed video, audio, reconnect and extended
playback require testing with a physical Miracast phone; host tests alone do
not establish interoperability.
