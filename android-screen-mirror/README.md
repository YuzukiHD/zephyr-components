# android-screen-mirror

Android screen mirroring on the YuzukiNeko board (Allwinner F101, XuanTie C907, 1024x768 DSI
panel, GT967 touch panel, AIC8800 WiFi on SDIO). The board connects to the phone with ADB over
TCP, starts the [scrcpy](https://github.com/Genymobile/scrcpy) server on it, decodes its H.264
stream with the video engine onto the video plane, and sends touches and the Back / Home / Recent
keys back to the phone.

| File | What |
|---|---|
| `src/adb.c` | ADB client: connect, `AUTH` (RSA) or `STLS` (TLS 1.3), stream multiplexer, `sync:` file push |
| `src/adb_auth.c` | the RSA key of the client (PEM file, created when missing), signing, public key format |
| `src/pairing.c`, `src/ed25519_ops.c` | wireless debugging pairing (QR code): SPAKE2 on edwards25519, HKDF, AES-128-GCM |
| `src/mdns.c` | minimal DNS-SD browser that finds `_adb-tls-connect` / `_adb-tls-pairing` |
| `src/tls_io.c` | mbed TLS on a socket |
| `src/scrcpy.c` | pushes and starts the server, reads the video packets, sends touch and key messages |

## Use

Put the scrcpy server of the version in `CONFIG_SAMPLE_MIRROR_SERVER_VERSION` (3.3.1) on the TF
card as `scrcpy-server.jar`; the release asset `scrcpy-server-v3.3.1` is that file. Then:

```
west build -b yuzukineko -d build/android-mirror zephyr-components/android-screen-mirror/samples/mirror_player
```

The sample adds this module and the sibling `aic8800` module to `ZEPHYR_EXTRA_MODULES` itself. In
another application add both, enable `CONFIG_MIRROR=y` and `CONFIG_AIC8800=y`, set
`CONFIG_MINIMAL_LIBC=y` (the glue headers of the AIC8800 driver need it) and
`CONFIG_MBEDTLS_USER_CONFIG_FILE="mirror_mbedtls.h"`; `samples/mirror_player/prj.conf` lists the
rest (network buffers, TLS 1.3, mbed TLS heap).

The API is `include/mirror_zephyr/`: `adb.h` (connection, streams, file push), `scrcpy.h`
(`scrcpy_start()`, `scrcpy_read_packet()`, `scrcpy_send_touch()`, `scrcpy_send_key()`),
`pairing.h` and `mdns.h`.

## Sample

`samples/mirror_player` joins the WiFi (the network is read from `/SD:/wifi.cfg`, the name on the
first line and the password on the second, or typed on the screen, or taken from
`CONFIG_SAMPLE_MIRROR_WIFI_SSID` and `CONFIG_SAMPLE_MIRROR_WIFI_PSK`), then connects to the phone:

- `CONFIG_SAMPLE_MIRROR_PHONE_IP` set: ADB over TCP to that address (run `adb tcpip 5555` once
  from a computer while the phone is on USB).
- empty (default): the phone is found through its wireless debugging service (Android 11 or newer)
  and paired with a QR code shown on the screen (Developer options, Wireless debugging, Pair
  device with QR code).

The client key is `/SD:/adbkey`. Touches on the picture are mapped to the picture of the phone;
the three round keys on the right edge send Back, Home and Recent apps; the gear in the top right
corner opens the settings (change the WiFi network, pair a new phone).

Not supported: audio, clipboard, keyboard and mouse input, more than five touch points.

## Security notes

- The random numbers (the RSA key, TLS, pairing) come from `sys_rand_get()`, which is the test
  generator (`CONFIG_TEST_RANDOM_GENERATOR=y`): the SoC has a hardware random number generator,
  but there is no register description or Zephyr driver for it on this chip yet. Do not use the
  key in a product.
- The TLS connection does not verify the certificate of the phone
  (`MBEDTLS_SSL_VERIFY_NONE`); adbd uses a self-signed certificate generated on the phone, and the
  ADB protocol authenticates the client by the key stored at pairing, not the phone.
- `/SD:/adbkey` and `/SD:/wifi.cfg` are plain text files.
