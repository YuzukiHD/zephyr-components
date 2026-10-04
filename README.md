# zephyr-components

Zephyr modules for RISC-V boards, built and tested on the Allwinner F101 EVB
(XuanTie C907, rv32imafdcv, 16 MB PSRAM). Each directory is a module of its own
(`zephyr/module.yml`), add the ones you need to `ZEPHYR_EXTRA_MODULES`.

| Module | What |
|---|---|
| [`ncnn/`](ncnn) | the [ncnn](https://github.com/Tencent/ncnn) neural network inference library, with the RISC-V vector kernels, and a benchmark sample |
| [`mgba/`](mgba) | the Game Boy / Game Boy Color / Game Boy Advance core of [mGBA](https://github.com/mgba-emu/mgba), a player sample with scaled output, on-screen keys and sound |

The upstream projects are git submodules (`ncnn/ncnn`, `mgba/mgba`):

```
git submodule update --init
```

```
list(APPEND ZEPHYR_EXTRA_MODULES /path/to/zephyr-components/ncnn
                                 /path/to/zephyr-components/mgba)   # before find_package(Zephyr)
```

The glue code is Apache-2.0 (see `LICENSE`); the submodules keep their own
licenses (ncnn: BSD-3-Clause, mGBA: MPL-2.0).
