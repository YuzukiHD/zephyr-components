# bochs

Zephyr module for the [Bochs](https://github.com/bochs-emu/Bochs) x86 PC emulator (git submodule
`Bochs/`, LGPL-2.1-or-later; the glue in this directory is Apache-2.0). The goal is a 386/486 class PC
on the 16 MB board that runs an old guest such as Windows 3.1 or Windows 95.

State: **Windows 3.1 boots on the board and is shown on the panel.** Bochs runs a 486 (`i486dx4`, with
the x87 emulation) and plain VGA; the guest has 10 MB; the disk is a flat image on the SD card; the
panel shows the VGA output (`src/bochs_gui.cc`); the guest text screen can be mirrored to the serial console
as `|row|text` lines (`CONFIG_BOCHS_TEXT_MIRROR`, off by default, noise in graphics modes) and the serial
console is the keyboard (keys are fed one at a time, a whole line at
once overflows the guest's key queue). The Windows 95 image has not been run since the last fix, see
"Not done". The disk images are the Bochs ones of
[archive.org/details/bochs_windows_images](https://archive.org/details/bochs_windows_images)
(Windows 3.1: 260/16/63 cylinders/heads/sectors, Windows 95: 507/16/63), copied to the card as
`Win31.img` and `windows95b.img`; their licence is not stated there, check yours.

Not done: mouse (the sample has the serial keyboard only: no arrow or function keys), Windows 95 and
the v86 demo image of Windows 95 after the BIOS address fix (below), sound, speed (below).

Speed: the board executes about 1..3 million guest instructions per second (a native Bochs on a PC
does 23 million), waiting loops that read ports about 0.65 million. The guest clock has to be set to
what the host can do (`cpu: ips=`, `CONFIG_SAMPLE_PC_IPS`, 3000000): at the default of 50 million the
30 second countdown of the Windows 95 boot menu takes half an hour.

## What is built

486 level CPU with the x87 emulation (softfloat), no PCI, no plugins, no networking, no sound. Devices:
PIC, PIT, DMA, CMOS, keyboard controller, floppy controller, IDE hard disk, serial and parallel port
stubs, speaker, plain VGA. The legacy BIOS and the VGA BIOS are linked into the image
(`src/bochs_roms.S`), the configuration is a string in the sample (`mem:bochsrc`).

| Part | File |
|---|---|
| Source list, compile flags, patches | `CMakeLists.txt` |
| Sizes of the caches, debug options | `Kconfig` |
| Configured Bochs header (rv32, level 4) | `include/config.h` |
| POSIX headers that the minimal C library lacks | `shim/` |
| File descriptors and streams on the Zephyr file system, `sscanf`, `strtod`, time, in-memory files | `src/bochs_libc.c` |
| `setjmp`/`longjmp` (the CPU leaves an instruction by `longjmp`) | `src/bochs_setjmp.S` |
| Display and serial keyboard | `src/bochs_gui.cc` |
| Entry points of devices that are not built, socket stubs, C++ runtime, self tests, heartbeat | `src/bochs_stubs.cc` |
| Changes to the Bochs tree, applied by CMake | `patches/` |
| Sample (`west build -b f101_evb -d build/bochs zephyr-components/bochs/samples/pc`) | `samples/pc` |

Patches to the submodule (applied once at configure time with `git apply`, the submodule worktree stays
modified): `0001` sizes of the instruction cache, the trace cache and the self modifying code table,
`0002` no `std::string`, `0003` the BIOS window of 128 KiB instead of 4 MiB, `0004` a register digest
timer for comparing two builds (`CONFIG_BOCHS_DIGEST`), `0005` guest RAM starts as zeros.

## Memory

The board has 16 MB of RAM in total (0x40000000), the image starts at 0x40010000 and the C library
heap takes everything above it (13.7 MB with the display, about 14.8 without).

| | Size |
|---|---|
| Code and constants of the emulator, BIOS images | 0.9 MB |
| Static data (CPU object, caches, stacks, display frame buffer 1.2 MB) | 1.7 MB |
| Guest RAM (`megs`) | 10 MB |
| Guest ROM windows (BIOS 128 KiB, option ROMs 128 KiB, plus a page) | 0.26 MB |
| I/O port tables (two tables of 64 K pointers) | 0.5 MB |
| Trace cache (4096 entries, 8192 instructions) | about 0.6 MB |
| Guest picture buffer (up to 720x480 RGB565) | 0.7 MB |

Things that do not fit and what was done about them: the stock trace cache is 64 K entries and a pool
of 576 K decoded instructions (tens of MB, allocated at start: the failure shows as a hang after the
device init); the stock instruction cache is 16 MB of static data; the stock BIOS window is 4 MB; the
table of write stamps for self modifying code covers the whole 4 GiB (4 MB, `BX_PAGE_STAMP_ENTRIES`);
VBE video memory is 8 MB (`vga: extension=none`). `CONFIG_DISPLAY_SUNXI_FB_DIV=2` (frame buffer of the
display driver at half the panel size) was tried; the first screen that looked repeated was caused by the
text column count below, so the option is worth another try.

## Findings

- **The BIOS must not be given an address.** `romimage: file=..., address=0xf0000` makes Bochs treat
  every address from 0xF0000 up, that is all extended memory, as BIOS ROM: writes are dropped, reads
  return ROM bytes. HIMEM reports `unreliable XMS memory at address 00100004h`, and the guest ends in a
  loop of invalid opcodes. Without `address` the BIOS goes to the top of the address space (patch
  `0003` keeps that window small). Most of the debugging of this port (several days of it) was this.
- `bx_gui_c::dimension_update()` has to copy `fwidth`/`fheight` into `guest_fwidth`/`guest_fheight`, and
  the GUI has to set `new_text_api = 1`; without them the text is read with the wrong column count (the
  right edge shows the start of the next row) or the base class recurses into `text_update()`.
- Handler chaining (`BX_SUPPORT_HANDLERS_CHAINING_SPEEDUPS`) is off in `include/config.h`; worth trying
  again for speed.
- The minimal C library has no `strtod`, `sscanf`, file streams, `mktime` for years 0000..9999 (`time_t`
  is 32 bit), `strtok`; they are in `src/bochs_libc.c`.
- An allocation that fails returns NULL (no exceptions): look at the heap first when something stops
  after device init.
- The 450 MB image of the v86 demo (`i.copy.sh/windows95-v3/<start>-<end>.img`, 256 KiB parts) loads
  `VBMOUSE.EXE` and SmartDrive from `AUTOEXEC.BAT`; the sample can turn both lines into remarks in
  place (`CONFIG_SAMPLE_PC_PATCH_AUTOEXEC`, offset of that image). It was not run again after the BIOS
  fix.
- A flat disk image written to a FAT32 card can be damaged by an unclean removal: one bad sector
  (the directory sector holding `WIN.COM`) made Windows report that file as missing. Compare the crc32
  (`CONFIG_SAMPLE_PC_VERIFY`) with the original before the first start.
- Comparing two builds: `CONFIG_BOCHS_DIGEST` prints the registers every N virtual microseconds with a
  window and the code bytes; a native Bochs of the same configuration (fixed `time0`, same `ips`)
  produces the same lines until the builds differ.
