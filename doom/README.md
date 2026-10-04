# doom

Doom for Zephyr: the [doomgeneric](https://github.com/ozkl/doomgeneric) engine (git submodule
`doomgeneric/`, GPL-2.0) with a platform layer in `src/` (Apache-2.0).

- `src/doom_port.c`: the `DG_*` functions and `include/doom_zephyr/doom.h`. The engine renders
  the 8 bit 320x200 picture; it is converted with the engine's palette to RGB565. Keys come through
  `doom_key()` (any thread).
- `src/doom_libc.c` + `shim/doom_libc.h` (force-included): file streams on top of the Zephyr file
  system, `sscanf` for the number formats of the config code, `strdup`, `strcasecmp` ... which the
  minimal C library lacks. Relative paths (`./doom.cfg`, `.savegame/`) are taken below the base
  directory given to `doom_open()`.
- `src/doom_sound.c` (`CONFIG_DOOM_SOUND`): mixes the sound effects (8 channels, linear
  interpolation, panning) into 16 bit stereo; `doom_audio_read()` is called by the audio thread of the
  application. The music is not played.

The game data is not included: put `doom1.wad` (shareware) or a Freedoom WAD on the card.

```
git submodule update --init
west build -b f101_evb -d build/doom zephyr-components/doom/samples/doom_player
```

The sample takes `/SD:/doom1.wad` (`CONFIG_SAMPLE_DOOM_WAD`) or the first `.wad` in the root of the card
and shows the picture on the scaling video plane, with on-screen keys (LVGL).
