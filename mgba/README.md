# mGBA

The Game Boy, Game Boy Color and Game Boy Advance core of
[mGBA](https://github.com/mgba-emu/mgba) as a Zephyr module. mGBA itself is the
git submodule `mgba/mgba` (MPL-2.0), unchanged. The glue in this directory is
Apache-2.0; `src/renderers/` and `src/gba_memory.c` hold modified copies of mGBA
files (MPL-2.0).

Tested on the Allwinner F101 EVB (XuanTie C907, 16 MB PSRAM).

Only the emulation core is built: no threading, debugger, scripting or image
codecs, and no BIOS file (the built-in replacement is used). The libc gaps of
the minimal C library are filled by `src/mgba_libc.c` and `shim/`.

## Use

```
git submodule update --init mgba/mgba
west build -b f101_evb -d build/gba zephyr-components/mgba/samples/gba_player
```

or, in another application, add the module and enable it:

```
list(APPEND ZEPHYR_EXTRA_MODULES /path/to/zephyr-components/mgba)   # before find_package(Zephyr)
```

```
CONFIG_MGBA=y
CONFIG_FPU=y
CONFIG_SUN252I_F101_RVV=y   # optional, vector versions of the hot loops
```

The API is `include/mgba_zephyr/gba.h`: `gba_open()` picks the core by the file
extension (.gba, .gb, .gbc), `gba_run_frame()` emulates one frame into an RGB565
buffer, `gba_audio_read()` returns the sound resampled to the rate asked for,
`gba_set_keys()`, `gba_save_flush()`.

## Sample

`samples/gba_player` runs a ROM from the SD card on the display engine and the
codec: the first fitting ROM in the root of the card (or `CONFIG_SAMPLE_GBA_ROM`),
the picture scaled by the video plane of the display engine, LVGL keys on the
ARGB plane over it, the sound pacing the emulation. The save file is written next
to the ROM. The CPU clock is raised to `CONFIG_SAMPLE_GBA_CPU_MHZ`.

The whole ROM is read into memory, so a ROM larger than `CONFIG_MGBA_MAX_ROM_SIZE_MB`
(10 on the EVB) is skipped. With `-DEXTRA_CONF_FILE=.../paged.conf`
(`CONFIG_MGBA_ROM_DEMAND_PAGED`, needs the MMU and the demand paging of the
kernel) the ROM is a window of the file that is paged in from the card when the
emulation touches it, so a 16 MiB ROM runs on the 16 MB board; `src/gba_memory.c`
then leaves out the 32 MiB copy that mGBA makes when a game writes into the ROM.

The picture is drawn at 30 fps (`CONFIG_SAMPLE_GBA_VIDEO_DIV`) and less while the
sound is about to run out: a skipped frame runs the emulation and the sound but
not the renderer. The sound is resampled by a fixed point linear interpolation.

## Vector extension

With `CONFIG_MGBA_RVV` the library is built with the V extension, `memcpy` and
`memset` use the vector unit, and the background renderer draws a row of a tile
(eight pixels, 16 or 256 colours, with or without alpha blending) with vector
instructions. The output is identical to the scalar renderer, checked with the
frame CRCs of `CONFIG_SAMPLE_GBA_BENCH`. The interpreter itself stays scalar.
