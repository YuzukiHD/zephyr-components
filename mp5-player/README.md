# mp5-player

A media player with a touch user interface for the YuzukiNeko, built on LVGL. Not a module but an
application, like `uf2-downloader`. Every function is a feature with a switch of its own
(`CONFIG_MP5_FEATURE_*`); a feature that is switched off is not built and leaves no tile, no
settings row and no file type behind.

| Feature | Switch | What it is |
|---|---|---|
| Home | always | the clock (tap it to set the time) and the tiles of the features |
| File browser | `MP5_FEATURE_FILES` | browse the SD card |
| Settings | `MP5_FEATURE_SETTINGS` | the screen with the rows the other features add |
| Brightness, theme | `MP5_FEATURE_BRIGHTNESS`, `MP5_FEATURE_THEME_PICKER` | settings rows: backlight, dark or light, accent colour |
| Videos | `MP5_FEATURE_VIDEO` | MP4 (H.264 + AAC) and raw H.264, resume, recent list |
| Photos | `MP5_FEATURE_PHOTOS` | JPEG and PNG with pinch zoom |
| Calculator | `MP5_FEATURE_CALCULATOR` | four function pocket calculator |
| Games | `MP5_FEATURE_GAMES` | NES, Game Boy, Game Boy Color, Game Boy Advance |
| USB Display | `MP5_FEATURE_USB_DISPLAY` | second screen of a PC with touch |
| Network | `MP5_FEATURE_NET` | Wi-Fi (AIC8800, saved passwords) and the time from NTP |
| Bluetooth | `MP5_FEATURE_BT` | the connection is shown in the settings |

## Source layout

```
src/
  main.c           runs the start stages, nothing else
  core/            services without a user interface
    app.[ch]         start steps: MP5_INIT(stage, order, name, fn)
    storage cfg clock cpu fsutil boot_display
  ui/              the framework, no features in it
    ui.[ch]          screen manager (screens are found by name)
    registry.h       MP5_SCREEN, MP5_HOME_TILE, MP5_SETTINGS_ROW, MP5_FILE_HANDLER
    theme widgets time_dialog
  gfx/             drawing accelerators for LVGL (vector blends, 2D engine)
  features/<name>/ one directory per feature: CMakeLists.txt, Kconfig, sources
  debug/           statistics and the self tests (MP5_DEBUG)
```

Start stages run in this order: `EARLY` (CPU clock, vector check, the display thread), `STORAGE`
(card, settings), `UI` (LVGL, theme, screens), `SERVICES` (network), `READY` (after the first picture).
Each module registers its own steps, so `main()` has no `#ifdef`.

The registries are read only records in the flash that the linker collects (`src/*/sections-rom.ld`):

- `MP5_SCREEN(sym, "name", create, enter, leave)` a screen; `ui_goto("name")` shows it and does
  nothing when the feature is not built.
- `MP5_HOME_TILE(sym, .order, .symbol, .title, .subtitle, .color_top, .color_bottom, .screen | .kind)`
  a tile of the home screen: it opens the screen, or the file browser for a kind of file.
- `MP5_SETTINGS_ROW(sym, order, create, refresh)` a row of the settings.
- `MP5_FILE_HANDLER(sym, .kind, .title, .symbol, .color, .exts, .open, [.extra_screen, .extra_label])`
  a kind of file: its extensions, its icon and what opens it.

### Adding a feature

1. `src/features/<name>/` with `Kconfig` (`config MP5_FEATURE_<NAME>`, `default y`, `select` what it
   needs from Zephyr) and `CMakeLists.txt` (`target_sources_ifdef(CONFIG_MP5_FEATURE_<NAME> app PRIVATE ...)`).
2. `add_subdirectory(src/features/<name>)` in the top `CMakeLists.txt`; the `Kconfig` is found by the
   glob in the top `Kconfig`.
3. In the sources register what the feature offers: `MP5_SCREEN`, `MP5_HOME_TILE`,
   `MP5_SETTINGS_ROW`, `MP5_FILE_HANDLER`, `MP5_INIT`.

## How it works

LVGL draws the user interface on the ARGB frame buffer plane (1024x768), the video goes through the
video engine to the video plane underneath, and the display engine blends the two. The player
(`src/features/video/player.c`) plays, pauses and seeks the same way: a request for a new session at a position.
The video and the audio thread watch the sequence number of the request and start over when it
changes. A paused session shows the picture at its position and holds. The sound of a session
starts when the first picture is decoded, otherwise it would run away from a video that has to
decode from its key frame to the position.

Seeking has to decode from the key frame before the position. A file with one key frame at the start
(for example `ffmpeg` with the default settings and a long clip) takes seconds to seek; give the
encoder a key frame every 1 or 2 seconds (`-g 30`) for a quick seek.

## XIP

The image runs in place from the SPI NOR flash: it is linked into the window of the SPIF controller
(`CONFIG_XIP`, `boards/yuzukineko.overlay`, 6 MiB), code and read only data are fetched from the
flash and only `.data`, `.bss` and the heap are in the RAM. The window has to be mapped before the
first instruction, which the SyterKit application `xip-boot` does: it brings up the PSRAM and the
flash, maps the flash from the offset `0x80000` at `0x0e000000` and jumps there. `xip-boot` is not
started by the flash loader yet, so the image does not start by itself at power on.

```sh
. ~/f101-zephyr/env.sh
cd ~/f101-zephyr
west build -b yuzukineko -d build/mp5 zephyr-components/mp5-player

# SyterKit, the application xip-boot (RISC-V toolchain on PATH)
cd SyterKit
make O=../out/syterkit CROSS_COMPILE=$CROSS yuzukineko_rv32_sram_defconfig
make -j8 O=../out/syterkit CROSS_COMPILE=$CROSS xip-boot
```

The PSRAM clock of the `yuzukineko` board file of SyterKit is 312 MHz (it was 252 MHz): the video
decoder shares the memory bus with the display and is slower at the lower clock, a 540p film at
30 fps then drops pictures in heavy scenes.

With the board in FEL mode:

```sh
xfel spinor write 0x8000 build/mp5/zephyr/zephyr.bin
xfel write 0x20000 out/syterkit/build/yuzukineko/app_sram/xip-boot/xip-boot_fel.bin
xfel exec 0x20000
```

### Complete flash image (starts by itself at power on)

`tools/mkimage.py` puts the boot stub (`xip-boot_spi.bin`, the SPL image for the flash, one copy at 0) and the application at `0x80000` into one file for the whole flash up to the end
of the application. Everything in front of the application is overwritten.

```sh
python3 tools/mkimage.py --boot out/syterkit/build/yuzukineko/app_sram/xip-boot/xip-boot_spi.bin \
    --app build/mp5/zephyr/zephyr.bin -o mp5-full.img
# board in FEL mode (hold the FEL key at power on)
xfel spinor write 0 mp5-full.img
```

After the next power cycle the BROM starts the stub from the flash, which maps the window and jumps to the
application; the log of the stub and of the application is on the console of the board. To change only the
application write `zephyr.bin` at `0x80000` as above (FEL mode is still needed, the board no longer
enters it by itself).

Flash layout: `0..128 KiB` the boot stub, `128..192 KiB` the parameter area (the stub keeps the result of the
sampling training there, so only the first start trains: about 0.55 s less at every later start, see
`CONFIG_DRIVER_MTD_SPIF_NOR_SAVE_PARAMS` in SyterKit), the image from `512 KiB`.

During development the stub is not written to the flash: the board then enters FEL at every power
cycle and `xip-boot_fel.bin` is run from the RAM (`xfel write 0x20000 ... ; xfel exec 0x20000`) after the
application was written to `0x80000`; the parameter area is not touched by that and is used the same way.

The console is UART1 (PB0/PB1), 115200 8N1. `0x80000` to the end of the flash is used by the image;
the UF2 application area (`0x200000`) overlaps it, so do not use both.

## Options

| Option | Default | |
|---|---|---|
| `CONFIG_MP5_FEATURE_*` | on | the features above |
| `CONFIG_MP5_GFX_RVV` | on | vector blends for LVGL (checked against the C code at boot) |
| `CONFIG_MP5_GFX_G2D` | off | fills with the 2D engine (slower, see the help) |
| `CONFIG_MP5_CPU_MHZ` | 816 | CPU clock, raised in steps with a memory check |
| `CONFIG_MP5_VIDEO_DEFAULT` | `/SD:/video540p-ysjf.mp4` | the file the self test plays |
| `CONFIG_MP5_GAME` | empty | a game to start at boot, for a check without touching |
| `CONFIG_MP5_DEBUG` | on | numbers about the user interface |
| `CONFIG_MP5_SELFTEST` | off | play the default video, seek, pause, resume and change the volume by itself |
| `CONFIG_MP5_THEME_TEST` | off | settings, theme, Wi-Fi and game steps by themselves, ends with `PASS` or `FAIL` |

The options were called `CONFIG_SAMPLE_MP5_*` before (the two test switches keep their old names as aliases); `SAMPLE_MP5_UI_BENCH`, `_UI_PROFILE`,
`_UI_DEMO` and `_ROOT` are gone.

## Notes

- The text of the user interface is English: the LVGL fonts have no Chinese glyphs.
- FatFs is built reentrant (`CONFIG_FS_FATFS_REENTRANT`): the audio and the video thread read the
  same card, without it the reads fail with `-EIO`.
- Only 44.1 and 48 kHz AAC is played, as in `samples/drivers/mp4_player`.
