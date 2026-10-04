# MicroPython for the F101

The unmodified MicroPython Zephyr port (`micropython/`, git submodule, MIT) as the
application, plus this directory as a Zephyr module (headers and `-lm` the minimal C
library lacks), the board configuration and an `f101` module for the hardware that
MicroPython has no generic binding for.

```sh
git submodule update --init
git -C zephyr-components/micropython/micropython submodule update --init --depth 1 \
	lib/oofatfs lib/littlefs lib/micropython-lib
. ./env.sh
zephyr-components/micropython/build.sh [build/upy] [-DCONFIG_MICROPY_HEAP_SIZE=...]
```

`build.sh` configures with the cross toolchain and builds with `CROSS_COMPILE` unset:
the build also compiles `mpy-cross` for the host, and with `CROSS_COMPILE` set it is
built for RISC-V and fails to link.  The image goes to `0x40010000` like the other
samples; the REPL is on the console (UART3 of the EVB, 115200).

## What works (verified on the EVB)

| MicroPython | hardware |
|---|---|
| `machine.Pin(('gpiod', 5), Pin.OUT)` | GPIO banks `gpioa`..`gpiof` |
| `machine.SPI('spi0')` | SPI0 |
| `machine.ADC(('adc', n))` | GPADC, channels 0..3 are declared in `conf/f101.overlay` |
| `machine.PWM(('pwm_bl', 0), freq=..., duty_ns=...)` | PWM_BL (PB0..PB3); `pwm` is off on the EVB, the LCD uses its pins |
| `machine.WDT(timeout=...)`, `machine.reset()` | watchdog; reset is the watchdog too (about 1 s) |
| `zephyr.DiskAccess`, `/sd` | SD card, mounted by `_boot.py`, FAT |
| `zephyr.Display(0)` | the LCD, RGB565 1024x600 (`write(buf, x, y, w, h)`, `as_framebuf()`) |
| `f101.unique_id()` | 128 bit chip id from the fuses |
| `f101.g2d_fill/g2d_blit/g2d_blend` | 2D accelerator on `bytearray` surfaces (ARGB8888, RGB565, ... `f101.RGB565`), rectangles are `(x, y, w, h)` tuples, `rotate=0/90/180/270`, `f101.FLIP_H/V` |
| `f101.Audio(rate)` | on-chip codec output, `write(S16LE stereo)`, `tone(hz, ms)`, `volume(0..255)`, `close()` |

I2C is not on the list: on the EVB the pins of the three controllers are used (console,
backlight, LCD).  Enable a controller and its `pinctrl` in your own overlay.

## Limits and gotchas

- The MicroPython heap is a static `CONFIG_MICROPY_HEAP_SIZE` array (1 MiB here) that is
  padded into the flat image, so a bigger heap makes the xfel download slower.  A full
  1024x600 RGB565 frame (1.2 MB) does not fit; draw in bands.
- `f101.Audio`: 44.1 kHz and 48 kHz cannot be mixed in one boot (one PLL, one family);
  the first rate used wins until the next reset.
- `machine.PWM(..., duty_u16=...)` overflows 32 bit for periods above about 130 us
  (`duty_u16 * period` in the port); use `duty_ns`.
- The G2D surfaces are plain buffers, so they have to be in 1:1 memory (the kernel has
  no MMU enabled in this configuration, all of RAM is).
- `networking` is switched off in `conf/f101.conf`; the Kconfig warnings about NET symbols
  at configure time are the board defaults being overridden.
