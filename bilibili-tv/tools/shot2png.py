#!/usr/bin/env python3
# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0
"""
Turns the output of "bili shot" (lines between SHOT w h and END, base64 of RGB565) in a serial log into a PNG.

  tools/shot2png.py serial.log out.png [n]      n-th picture counted from the end of the log, 1 by default
"""
import base64, re, struct, sys, zlib

text = open(sys.argv[1], "rb").read().decode("latin-1")
# a message of another thread can come in the middle of a line of the picture: cut it out and the two
# halves of the line are one line again
text = re.sub(r"alive \d+ s;[^\n]*\n", "", text)
m = list(re.finditer(r"SHOT (\d+) (\d+)\r?\n(.*?)\r?\nEND", text, re.S))
if not m:
    sys.exit("no complete SHOT block in the log")
m = m[-int(sys.argv[3]) if len(sys.argv) > 3 else -1]
w, h = int(m.group(1)), int(m.group(2))
# every full line is 64 characters = 48 bytes; a line that another message broke is replaced by black
# bytes so that the lines after it stay where they belong
body = [l.strip() for l in m.group(3).splitlines()]
raw = bytearray()
bad = 0
for i, l in enumerate(body):
    last = i == len(body) - 1
    if re.fullmatch(r"[A-Za-z0-9+/]+", l) and (len(l) == 64 or (last and len(l) % 4 == 0)):
        raw += base64.b64decode(l)
    else:
        raw += bytes(48)
        bad += 1
if bad:
    print("warning: %d damaged lines were replaced by black" % bad, file=sys.stderr)
raw = bytes(raw)
if len(raw) < w * h * 2:
    # a line of another log message in the middle costs a few bytes: the picture is padded
    print("warning: only %d of %d bytes arrived, the end is black" % (len(raw), w * h * 2), file=sys.stderr)
    raw += bytes(w * h * 2 - len(raw))
rows = bytearray()
for y in range(h):
    rows.append(0)
    for x in range(w):
        v = raw[(y * w + x) * 2] | raw[(y * w + x) * 2 + 1] << 8
        rows += bytes((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))

def chunk(t, d):
    c = struct.pack(">I", len(d)) + t + d
    return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)

png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + \
      chunk(b"IDAT", zlib.compress(bytes(rows), 6)) + chunk(b"IEND", b"")
open(sys.argv[2], "wb").write(png)
print("%s: %dx%d" % (sys.argv[2], w, h))
