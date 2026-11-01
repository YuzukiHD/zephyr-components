# zephyr-components

The `zephyr-v4.5.0-f101` branch targets the matching F101 Zephyr branch based on
Zephyr 4.5.0, including LVGL 9.6 and Mbed TLS 4. See the Zephyr board guide at
`boards/yuzukihd/yuzukineko/doc/f101-migration-4.5.rst` for the environment,
module revisions, build commands and rollback procedure. The previous component
baseline remains on `main`.

Zephyr modules for RISC-V boards. Each directory is a module of its own
(`zephyr/module.yml`), add the ones you need to `ZEPHYR_EXTRA_MODULES`.

| Module | What |
|---|---|
| [`ncnn/`](ncnn) | the [ncnn](https://github.com/Tencent/ncnn) neural network inference library, with the RISC-V vector kernels, and a benchmark sample |
| [`doom/`](doom) | Doom on the [doomgeneric](https://github.com/ozkl/doomgeneric) engine, with file access on the Zephyr file system, sound effects and a player sample |
| [`nes/`](nes) | NES / Famicom emulator, the nofrendo core with a Zephyr glue layer and a player sample |
| [`aic8800/`](aic8800) | AIC8800 WiFi chips on SDIO: host driver, supplicant, Zephyr `net_if` / `wifi_mgmt` glue, scan sample |
| [`mgba/`](mgba) | the Game Boy / Game Boy Color / Game Boy Advance core of [mGBA](https://github.com/mgba-emu/mgba), a player sample with scaled output, on-screen keys and sound |
| [`micropython/`](micropython) | [MicroPython](https://github.com/micropython/micropython) (the unmodified Zephyr port as the application), board configuration and the `f101` module (G2D, audio, chip id); REPL on the console |
| [`pocketjs/`](pocketjs) | [PocketJS](https://github.com/pocket-nexus/pocketjs): QuickJS-ng guest, Rust UI core and RGB565 renderer with G2D / RVV acceleration, player sample |
| [`litehtml/`](litehtml) | the [litehtml](https://github.com/litehtml/litehtml) HTML/CSS renderer drawing into an RGB565 buffer, HTML viewer sample |
| [`android-screen-mirror/`](android-screen-mirror) | Android screen mirror over WiFi: ADB over TCP and a scrcpy client, H.264 decoded by the video engine, touch and key events back to the phone |
| [`miracast/`](miracast) | Wi-Fi Display sink: RTSP negotiation, RTP/MPEG-TS demux, H.264/LPCM player, standalone receiver and NekoBox home screen app |
| [`ios-screen-mirror/`](ios-screen-mirror) | iOS screen mirror: AirPlay receiver on the [RPiPlay](https://github.com/FD-/RPiPlay) library, DNS-SD announcements, H.264 decoded by the video engine |
| [`uf2-downloader/`](uf2-downloader) | not a module but an application: shows a `F101BOOT` USB drive and writes the `.uf2` files copied onto it to the SPI NOR (`tools/mkuf2.py`) |
| [`bochs/`](bochs) | the [Bochs](https://github.com/bochs-emu/Bochs) x86 PC emulator (486 class, no FPU) for a Windows 95 class guest; the BIOS runs on the board, output, input and a disk are not done yet |
| [`nekobox/`](nekobox) | not a module but an application: a media player with an LVGL touch interface, a home screen with a clock and a video player with a progress bar and a volume slider; runs in place from the flash (XIP) |
| [`examples/`](examples) | not a module: small applications for yuzukineko, one per driver or library interface, with the board output in the documentation |

The upstream projects are git submodules (`ncnn/ncnn`, `mgba/mgba`, `doom/doomgeneric`, `bochs/Bochs`, `micropython/micropython`,
`litehtml/litehtml`, `pocketjs/pocketjs`, `pocketjs/quickjs-ng`, `ios-screen-mirror/rpiplay`); the nofrendo core
of `nes/` is a copy in the tree:

```
git submodule update --init
```

```
list(APPEND ZEPHYR_EXTRA_MODULES /path/to/zephyr-components/ncnn
                                 /path/to/zephyr-components/mgba
                                 /path/to/zephyr-components/doom
                                 /path/to/zephyr-components/nes)   # before find_package(Zephyr)
```

The glue code is Apache-2.0 (see `LICENSE`); the submodules keep their own
licenses (ncnn: BSD-3-Clause, mGBA: MPL-2.0, MicroPython, PocketJS: MIT, litehtml: BSD-3-Clause,
Bochs: LGPL-2.1-or-later, doomgeneric and nofrendo: GPL-2.0, so an image that links `doom/` or `nes/` is
GPL-2.0; RPiPlay: GPL-3.0, so `ios-screen-mirror/` as a whole is GPL-3.0).

## Zephyr 4.5.0 compatibility

The `yuzukineko` build sweep covers 40 application configurations: NekoBox
with XIP/RVV, DLNA and the Miracast home app; Android and iOS mirror players;
Bilibili TV; UF2 downloader; Doom, NES, mGBA, gpSP and Bochs; litehtml with
and without demand paging; four AIC8800 samples; the Miracast receiver;
ncnn; PocketJS; MicroPython; and all 20 applications under `examples/`.
MicroPython uses its `micropython/build.sh` wrapper, which also builds the
host bytecode compiler.

The DLNA, NekoBox streaming, Miracast, AIC8800 P2P, NekoBox Miracast lifecycle
and Android authentication host suites pass. Their entry points are:

```sh
sh zephyr-components/dlna/tests/host/run.sh
sh zephyr-components/nekobox/tests/streaming/run.sh
sh zephyr-components/miracast/tests/host/run.sh
sh zephyr-components/aic8800/tests/p2p/run.sh
sh zephyr-components/nekobox/tests/miracast/run.sh
sh zephyr-components/android-screen-mirror/tests/host/run.sh
```

The media suites require GCC, Python and FFmpeg; the Android crypto suite
also requires CMake and Python `cryptography`. Run them with an executable
temporary directory. These results establish build and host-test compatibility.
Board startup, display, audio, Wi-Fi, phone interoperability and sustained
memory use still require validation on the new firmware. Existing component
limitations, including the incomplete Bochs guest peripherals, still apply.
