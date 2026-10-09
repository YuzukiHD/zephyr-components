#!/usr/bin/env python3
# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0
"""Make a .uf2 file (or a raw flash image) for the F101 boards.

  mkuf2.py app zephyr.bin -o app.uf2             application at 0x200000, loaded to 0x40010000
  mkuf2.py app zephyr.bin --load 0x40000000 ...  another load (= entry) address
  mkuf2.py system zephyr.bin -o downloader.uf2   system program (the UF2 downloader) at 0x30000
  mkuf2.py loader boot.img -o loader.uf2         the boot loader, written at 0 (family: system)

Copy the file onto the F101BOOT drive. "app" files are accepted by the downloader as they are; the
loader and the system program need the "system" family, which also rewrites the downloader itself:
the board has to stay powered while it is written.

--raw writes the flash image (header page + program) instead, for xfel spinor write.
"""
import argparse
import struct
import sys

FAMILY_APP = 0x31303146
FAMILY_SYS = 0x31303147
APP_OFFSET = 0x200000
SYS_OFFSET = 0x30000
HEADER_PAGE = 0x1000
BOOT_MAGIC = 0x44524146  # "FARD"
BOOT_VERSION = 2


def header_page(load, size, ident, extra_load=0, extra_max=0, extra_offset=0):
    page = struct.pack("<8I", BOOT_MAGIC, BOOT_VERSION, load, size, ident, extra_load, extra_max, extra_offset)
    return page.ljust(HEADER_PAGE, b"\xff")


def uf2(data, address, family, payload=256):
    blocks = [data[i:i + payload] for i in range(0, len(data), payload)]
    out = bytearray()
    for n, chunk in enumerate(blocks):
        out += struct.pack("<8I", 0x0A324655, 0x9E5D5157, 0x2000, address + n * payload, len(chunk), n, len(blocks), family)
        out += chunk.ljust(476, b"\x00")
        out += struct.pack("<I", 0x0AB16F30)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("kind", choices=["app", "system", "loader"])
    ap.add_argument("input")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--load", type=lambda v: int(v, 0), default=0x40010000, help="PSRAM address of the program (default 0x40010000)")
    ap.add_argument("--id", type=lambda v: int(v, 0), default=0)
    ap.add_argument("--raw", action="store_true", help="write the flash image, not a UF2 file")
    a = ap.parse_args()

    data = open(a.input, "rb").read()
    if a.kind == "loader":
        image, address, family = data, 0, FAMILY_SYS
    else:
        max_size = 0x9F0000 if a.kind == "app" else 0x1CF000
        if len(data) > max_size:
            sys.exit(f"{a.input}: {len(data)} bytes, the limit is {max_size}")
        image = header_page(a.load, len(data), a.id) + data
        address, family = (APP_OFFSET, FAMILY_APP) if a.kind == "app" else (SYS_OFFSET, FAMILY_SYS)
    if a.raw:
        open(a.output, "wb").write(image)
    else:
        open(a.output, "wb").write(uf2(image, address, family))
    print(f"{a.output}: {len(image)} bytes at 0x{address:06x}" + ("" if a.raw else f", {(len(image) + 255) // 256} UF2 blocks"))


main()
