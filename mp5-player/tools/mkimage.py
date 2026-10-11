#!/usr/bin/env python3
# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0
"""
Builds the complete flash image of the MP5 player: the boot stub (SPL image for the SPI NOR, which
brings up the PSRAM and maps the flash window) in front, then the Zephyr image at its XIP offset.
One file for the whole flash up to the end of the application, written with

    xfel spinor write 0 mp5-full.img

The BROM looks for the stub at the start of the flash; it is stored three times (0, 64 KiB, 128 KiB)
like the loader of the other applications.
"""
import argparse
import sys

SECTOR = 4096
STUB_COPIES = (0x0, 0x10000, 0x20000)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--boot", required=True, help="xip-boot_spi.bin")
    ap.add_argument("--app", required=True, help="zephyr.bin of the application")
    ap.add_argument("--offset", type=lambda x: int(x, 0), default=0xA00000,
                    help="flash offset of the application (matches the XIP window of the build)")
    ap.add_argument("-o", "--output", required=True)
    a = ap.parse_args()

    boot = open(a.boot, "rb").read()
    app = open(a.app, "rb").read()
    if boot[4:12] != b"eGON.BT0":
        sys.exit("the boot stub is not a SPL image for the flash (xip-boot_spi.bin)")
    if len(boot) > STUB_COPIES[1] - STUB_COPIES[0]:
        sys.exit("the boot stub does not fit before its next copy")
    size = a.offset + (len(app) + SECTOR - 1) // SECTOR * SECTOR
    img = bytearray(b"\xff" * size)
    for off in STUB_COPIES:
        img[off:off + len(boot)] = boot
    img[a.offset:a.offset + len(app)] = app
    open(a.output, "wb").write(img)
    print(f"{a.output}: {size} bytes, stub {len(boot)} bytes x{len(STUB_COPIES)}, "
          f"application {len(app)} bytes at {a.offset:#x}")


if __name__ == "__main__":
    main()
