# UF2 downloader

Flash the F101 boards by copying a `.uf2` file onto a USB drive.

1. Hold the key on **PD5** (to ground) while powering the board, or power it with no valid application in
   the flash. The loader starts this program instead of the application.
2. The board shows up as the drive `F101BOOT` (the USB cable of the download port).
3. Copy the `.uf2` file onto it. When the last block is written the board restarts into the application.

Make the file from a Zephyr image:

    tools/mkuf2.py app build/gba/zephyr/zephyr.bin -o gba.uf2        # application, flash 0x200000
    tools/mkuf2.py system build/uf2/zephyr/zephyr.bin -o dl.uf2      # this program, flash 0x30000
    tools/mkuf2.py loader boot.img -o loader.uf2                     # the loader, flash 0

`app` files go to the application area only (UF2 family `0x31303146`); `system` and `loader` files (family
`0x31303147`) may write anywhere, so a bad one can leave the board without a loader and only FEL helps.

Flash layout (16 MiB): loader x3 at 0/0x10000/0x20000, saved flash sample point 0x2f000, this program 0x30000,
application 0x200000. Both programs have a 4 KiB header page (see `tools/mkuf2.py`, loader `flash-boot` in
SyterKit) naming the PSRAM address they are loaded to.

Build: `west build -b yuzukineko -d build/uf2 zephyr-components/uf2-downloader`.
