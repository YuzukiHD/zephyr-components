# gbsp

The [gpSP](https://github.com/libretro/gpsp) Game Boy Advance emulator as a Zephyr module, with the
RISC-V dynamic recompiler: the ARM code of the game is translated to RISC-V and run natively.
GPL-2.0-or-later (see `LICENSE`).

`core/` is the libretro version of gpSP with the RISC-V back end (`core/riscv/`, derived from
[HowBoyAdvance](https://github.com/Irak4t0n/HowBoyAdvance)), taken from the `gbsp` application of
[retro-go-tab5](https://github.com/andjiang0083/retro-go-tab5) (commit `bee1ef0`). It is a copy
that was changed to build here, not a submodule:

- the logging, the file access (the Zephyr file system), the memory for the translated code and
  the cache synchronisation of the translated code use Zephyr;
- the zip loader, the diagnostic probes and the libretro front end are gone;
- the cheat parser has no `sscanf` (the minimal C library has none).

`src/gbsp_player.c` gives the core the interface of the mGBA module (`include/gbsp_zephyr/gbsp.h`
has the same functions as `mgba_zephyr/gba.h`, named `gbsp_*`, so both can be in one image): `gbsp_open()`, `gbsp_run_frame()`,
`gbsp_audio_read()` (the 32768 Hz sound of the core resampled to the rate asked for),
`gbsp_set_keys()`, `gbsp_save_flush()`. Only Game Boy Advance ROMs are supported (no Game Boy).

## Use

```
west build -b yuzukineko -d build/gbsp zephyr-components/gbsp/samples/gbsp_player
```

The sample is the `gba_player` sample of the mGBA module with the interface of this one; the
options (`SAMPLE_GBA_*`) are the same, so the two cores run under the same loop and benchmark.
In another application:

```
list(APPEND ZEPHYR_EXTRA_MODULES /path/to/zephyr-components/gbsp)   # before find_package(Zephyr)
```

```
CONFIG_GBSP=y
CONFIG_MINIMAL_LIBC=y
CONFIG_CPP=y            # two of the sources are C++
CONFIG_STD_CPP17=y
```

| Option | Default | |
|---|---|---|
| `GBSP_DYNAREC` | on | the recompiler; off is the interpreter |
| `GBSP_ROM_CACHE_KB`, `GBSP_RAM_CACHE_KB` | 2048, 256 | the caches for translated code (ROM code, code that runs from RAM) |
| `GBSP_ROM_BUFFER_MB` | 6 | memory for the ROM; what does not fit is read from the file in 32 KiB pages |
| `GBSP_OPT_LEVEL` | `-O3` | optimisation level of the core |

The recompiler needs the RAM it writes the code into to be executable (it is, on the board) and
the data cache written back before the code runs (`platform_cache_sync()` in `core/cpu_threaded.c`).

## Speed against mGBA

The same loop, no frame pacing, 1200 frames, CPU 960 MHz, YuzukiNeko (`SAMPLE_GBA_BENCH`):

| ROM | mGBA | gpSP | |
|---|---|---|---|
| Zelda (8 MiB) | 13166 us per frame | 5464 us per frame | 2.4x |
| FireRed (16 MiB, mGBA demand paged, gpSP 8 MiB + file) | 14104 us per frame | 5181 us per frame | 2.7x |

The picture checksums of the two cores are equal in the first frames (and at frame 600 and 1200
of FireRed); they differ later in Zelda, the cores do not time every event the same way. The
slowest frame is 51..54 ms with gpSP (a burst of new code to translate), 31..57 ms with mGBA.

## Where the time goes, and what was tried

`CONFIG_SAMPLE_GBA_PROFILE` on Zelda (960 MHz): translated code 26%, the renderer about 40% (text
background scanlines 16%, colour effect merge passes 10%, sprites 7%), the memory handlers the
translated code calls 7%, the rest is timers, sound and the frame loop.

- `-O3` instead of `-O2`: 1.6% faster, same checksums.
- `v` in `-march` costs nothing on its own (no change in speed).
- Vector version of the 4 bit tile rows drawn as colour indices (`GBSP_RVV`, `rvv_tile_row4_indexed()` in
  `video.cpp`: unpack the nibbles, OR the flags, masked store; no table lookup, so no gather): same
  pictures, 5630 -> 5578 us per frame on Zelda, about 1%. Palette lookups (the 16 bit colour tiles,
  the blend and brightness passes) need gathers, which are slow on this core, so they stay scalar.
- A vector version of the blend and brightness merge passes (palette lookups as indexed loads, the
  packed colour arithmetic one pixel per lane) gave the same pictures and was 11% slower: indexed
  loads are slow on the C907. It was dropped; the 8 pixel tile rows of the text renderer are too
  short for a vector loop to pay for its set-up.
- The core uses almost no floating point (fixed point everywhere, the sound resampler is integer),
  so the FPU has nothing to speed up.

## Playing

`west build -b yuzukineko -d build/gbsp zephyr-components/gbsp/samples/gbsp_player` runs
`/SD:/Pokemon-FireRed.gba` (`CONFIG_SAMPLE_GBA_ROM`; when it does not exist the first GBA ROM
in the root of the card is used). CPU 960 MHz, every frame drawn (60 fps), 48 kHz sound that
paces the emulation, the keys are the touch buttons on the screen, the save file
`Pokemon-FireRed.sav` is next to the ROM and written every 3 s. Measured on the board: 4-6 ms of
emulation per frame, no sound underrun.

## Slow scenes (Kirby)

Looked at with the counters of `gbsp_get_stats()` (the benchmark prints them every 200 frames):

- **The instruction cache was synchronised from the start of the translation cache after every
  translated block.** The cost grew with the fill of the cache, so a scene with a lot of new code
  (a thousand blocks a frame) was ten times slower than the same scene would have been. Only the
  range written since the last sync is synchronised now (all the patching of a block happens
  within the translation that started it). Kirby's slow scene: 99 ms to 61 ms per frame.
- **The ROM translation cache was too small for that scene** (1 MiB: emptied every second frame,
  1360 blocks translated each time). With 2 MiB it never fills: 11-13 ms per frame instead of 30-100,
  0-4 frames over 16.7 ms per 200 instead of 130-197.
- A ROM page that is not in memory is read from the card in the middle of the frame, 32 KiB in
  about 2 ms. A scene change that needs 30 pages makes one frame of 60 ms. That is what is left of
  the stutter; the memory for the ROM is what it costs (Kirby is 16 MiB, 5-6 MiB fit).

The picture checksums of Zelda (all six) and Kirby are the same as before the changes.

## Notes

- The routines between C and the translated code (`core/riscv/riscv_stubs.c`) are generated at
  start up with the same emitter as the translated code, there is no assembly file. The register
  file (`reg`, `spsr`, `reg_mode`) sits in its own section on purpose: with the three arrays
  scattered through `.bss` the same code was 8% slower (data cache conflicts with the hot
  arrays), so keep that placement.

- The memory: the arrays of the core (EWRAM, VRAM, the caches and tables) make the image 6.1 MB,
  the translation caches take 1.25 MB, the ROM buffer 8 MB: with 16 MB there is nothing to spare.
- The save file is the save memory of the core (up to 128 KiB, trailing 0xff left out), it is not
  the file format of mGBA.
- The open source BIOS is built in (`core/bios/`, its md5 is checked when the header is made); a
  `gba_bios.bin` in the root of the card is used when it is there.
