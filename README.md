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
| [`bochs/`](bochs) | the [Bochs](https://github.com/bochs-emu/Bochs) x86 PC emulator (486 class, no FPU) for a Windows 95 class guest; the BIOS runs on the board, output, input and a disk are not done yet |

The upstream projects are git submodules (`ncnn/ncnn`, `mgba/mgba`, `doom/doomgeneric`, `bochs/Bochs`); the nofrendo core
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
licenses (ncnn: BSD-3-Clause, mGBA: MPL-2.0, doomgeneric and nofrendo: GPL-2.0, so an image that
links `doom/` or `nes/` is GPL-2.0).
