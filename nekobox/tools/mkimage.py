#!/usr/bin/env python3
# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0
"""
Builds the complete flash image of the NEKOBOX player: the boot stub (SPL image for the SPI NOR, which
brings up the PSRAM and maps the flash window) in front, then the Zephyr image at its XIP offset.
One file for the whole flash up to the end of the application, written with

    xfel spinor write 0 nekobox-full.img

Layout: 0..128 KiB the boot stub (one copy at 0, the BROM starts it), 128..192 KiB the parameter area
of the flash (left blank here: the first start trains the sampling and the stub saves the result there),
the application at 512 KiB.
"""
import argparse
import sys

SECTOR = 4096
STUB_COPIES = (0x0,)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--boot", required=True, help="xip-boot_spi.bin")
    ap.add_argument("--app", required=True, help="zephyr.bin of the application")
    ap.add_argument("--offset", type=lambda x: int(x, 0), default=0x80000,
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
    # the copies of the stub that fit in front of the application (the first one is the one the
    # boot ROM starts)
    copies = [off for off in STUB_COPIES if off + len(boot) <= a.offset]
    if not copies:
        sys.exit("the application offset leaves no room for the boot stub")
    for off in copies:
        img[off:off + len(boot)] = boot
    img[a.offset:a.offset + len(app)] = app
    open(a.output, "wb").write(img)
    print(f"{a.output}: {size} bytes, stub {len(boot)} bytes x{len(copies)}, "
          f"application {len(app)} bytes at {a.offset:#x}")


if __name__ == "__main__":
    main()
