# zephyr-components

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
| [`ios-screen-mirror/`](ios-screen-mirror) | iOS screen mirror: AirPlay receiver on the [RPiPlay](https://github.com/FD-/RPiPlay) library, DNS-SD announcements, H.264 decoded by the video engine |
| [`uf2-downloader/`](uf2-downloader) | not a module but an application: shows a `F101BOOT` USB drive and writes the `.uf2` files copied onto it to the SPI NOR (`tools/mkuf2.py`) |
| [`bochs/`](bochs) | the [Bochs](https://github.com/bochs-emu/Bochs) x86 PC emulator (486 class, no FPU) for a Windows 95 class guest; the BIOS runs on the board, output, input and a disk are not done yet |

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
