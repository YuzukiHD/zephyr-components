# mp5-player

A media player with a touch user interface for the YuzukiNeko, built on LVGL. Not a module but an
application, like `uf2-downloader`.

| Screen | State |
|---|---|
| Home: a clock (tap it to set the time, the board has no RTC) and the tiles of the functions | done |
| Videos: MP4 (H.264 + AAC) from the SD card, a progress bar that can be dragged, play / pause, 10 s skips, a volume slider, controls that hide after 4 s | done |
| Files: browse the SD card | not done |
| Photos: JPEG and PNG viewer | not done |
| USB Display: use the board as a second screen of a PC (the `cherryusb_display` sample of the Zephyr fork) | not done |

The tiles of the screens that are not done do nothing yet.

## How it works

LVGL draws the user interface on the ARGB frame buffer plane (1024x768), the video goes through the
video engine to the video plane underneath, and the display engine blends the two. The player
(`src/player.c`) plays, pauses and seeks the same way: a request for a new session at a position.
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
flash, maps the flash from the offset `0x40000` at `0x0e000000` and jumps there. `xip-boot` is not
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
xfel spinor write 0x40000 build/mp5/zephyr/zephyr.bin
xfel write 0x20000 out/syterkit/build/yuzukineko/app_sram/xip-boot/xip-boot_fel.bin
xfel exec 0x20000
```

### Complete flash image (starts by itself at power on)

`tools/mkimage.py` puts the boot stub (`xip-boot_spi.bin`, the SPL image for the flash, three copies at
0, 64 KiB and 128 KiB) and the application at `0x40000` into one file for the whole flash up to the end
of the application. Everything in front of the application is overwritten.

```sh
python3 tools/mkimage.py --boot out/syterkit/build/yuzukineko/app_sram/xip-boot/xip-boot_spi.bin \
    --app build/mp5/zephyr/zephyr.bin -o mp5-full.img
# board in FEL mode (hold the FEL key at power on)
xfel spinor write 0 mp5-full.img
```

After the next power cycle the BROM starts the stub from the flash, which maps the window and jumps to the
application; the log of the stub and of the application is on the console of the board. To change only the
application write `zephyr.bin` at `0x40000` as above (FEL mode is still needed, the board no longer
enters it by itself).

The console is UART1 (PB0/PB1), 115200 8N1. `0x40000` to the end of the flash is used by the image;
the UF2 application area (`0x200000`) overlaps it, so do not use both.

## Options

| Option | Default | |
|---|---|---|
| `CONFIG_SAMPLE_MP5_VIDEO_DEFAULT` | `/SD:/video540p-ysjf.mp4` | the file the Videos tile plays |
| `CONFIG_SAMPLE_MP5_ROOT` | `/SD:` | where the file browser starts |
| `CONFIG_SAMPLE_MP5_SELFTEST` | off | play the default video, seek, pause, resume and change the volume by itself and print the positions, for a check without touching the screen |

## Notes

- The text of the user interface is English: the LVGL fonts have no Chinese glyphs.
- FatFs is built reentrant (`CONFIG_FS_FATFS_REENTRANT`): the audio and the video thread read the
  same card, without it the reads fail with `-EIO`.
- Only 44.1 and 48 kHz AAC is played, as in `samples/drivers/mp4_player`.
