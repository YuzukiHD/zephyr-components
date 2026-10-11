# ios-screen-mirror

iOS screen mirroring on the YuzukiNeko board (Allwinner F101, XuanTie C907, 1024x768 DSI panel,
AIC8800 WiFi on SDIO). The board shows up as an AirPlay receiver in the Screen Mirroring list of
the iPhone; the phone sends its screen as an H.264 stream, which the video engine decodes onto the
video plane of the display engine.

The protocol library is [RPiPlay](https://github.com/FD-/RPiPlay) (git submodule `rpiplay`,
GPL-3.0) with its FairPlay handshake. OpenSSL is replaced by mbedTLS, Bonjour by the Zephyr
DNS-SD responder and libplist by a small binary plist reader in `src/plist.c`. Because RPiPlay is
GPL-3.0, this module as a whole is GPL-3.0.

Only the picture is shown: the audio of the stream is received and dropped.

## Use

The RPiPlay sources are a git submodule; fetch it once before the first build:

```
cd zephyr-components && git submodule update --init ios-screen-mirror/rpiplay && cd ..
west build -b yuzukineko -d build/ios-mirror zephyr-components/ios-screen-mirror/samples/ios_player
```

The sample adds this module and the sibling `aic8800` module to `ZEPHYR_EXTRA_MODULES` itself. In
another application add both and enable `CONFIG_IOS_MIRROR=y` and `CONFIG_AIC8800=y`; the glue
headers need the minimal C library (`CONFIG_MINIMAL_LIBC=y`).

The API is `include/ios_zephyr/airplay.h`: `airplay_start()` takes the name, the MAC address, the
port and the callbacks for the H.264 data (Annex B), a phone connecting or leaving, and the phone
stopping the mirroring.

## Sample

`samples/ios_player` joins the WiFi, announces the board and decodes the stream. The network is
read from `/SD:/wifi.cfg` (the name on the first line, the password on the second), or taken from
`CONFIG_SAMPLE_IOS_WIFI_SSID` and `CONFIG_SAMPLE_IOS_WIFI_PSK`. The phone must be in the same
network. The decoder memory limits the stream size: a portrait stream of the phone (about
448x972) fits, a landscape one of 1920x1080 is refused.
