# nes

NES / Famicom emulator for Zephyr: the [nofrendo](https://github.com/ducalex/retro-go) core
(`nofrendo/`, copied from the Retro-Go tree, GPL-2.0, see `nofrendo/COPYING`) plus a small glue
layer (`include/nes_zephyr/nesemu.h`, `src/nesemu.c`, Apache-2.0).

- iNES files (`.nes`) are read through the Zephyr file system, the battery RAM is kept in a `.sav`
  file next to the ROM and written when it changed (`nesemu_save_flush()`).
- The picture is RGB565, 256x224 (NTSC, 256x240 PAL); the sound is 16 bit stereo at the rate you
  ask for, one frame of it per `nesemu_run_frame()`. Mappers: the ones of the nofrendo tree.
- Local changes in `nofrendo/`: `NES_NO_STDIO` removes the `FILE` based loaders (the glue loads the
  ROM into memory), `utils.h` takes the CRC from the glue.

Sample `samples/nes_player` (needs the display, SD card and codec drivers of the F101 tree):

```
west build -b f101_evb -d build/nes zephyr-components/nes/samples/nes_player
```

It takes `/SD:/game.nes` (`CONFIG_SAMPLE_NES_ROM`) or the first `.nes` in the root of the card,
paces the emulation with the sound ring, and has on-screen keys (LVGL, over the video plane).
`CONFIG_SAMPLE_NES_BENCH` runs the emulation unpaced and prints the time per frame and a picture CRC.
