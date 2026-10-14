#!/usr/bin/env python3
# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0
"""
Cuts the Chinese font of the firmware out of Noto Sans SC (SIL OFL, the copy in the LVGL tests).

  tools/make_font.py [--level1] [-o src/ui/fonts/NotoSansSC-subset.ttf]

Characters: GB2312 (all 6763 hanzi and its symbols; --level1 keeps the 3755 common ones only), the 8105 hanzi of the
standard list of 2013 (tools/data/tgh8105.txt, kTGH of Unihan: 1300 more than GB2312, names and everyday words), the single
characters of tools/data/extra.txt, ASCII and
Latin-1, general and CJK punctuation, full-width forms, hiragana and katakana, arrows and a few symbols
that show up in titles. Needs fontTools (pip install --target tools-local/pylib fonttools).
"""
import argparse, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
WS = os.path.abspath(os.path.join(HERE, "../../.."))
sys.path.insert(0, os.path.join(WS, "tools-local/pylib"))
SRC = os.path.join(WS, "modules/lib/gui/lvgl/tests/src/test_files/fonts/noto/NotoSansSC-Regular.ttf")

ap = argparse.ArgumentParser()
ap.add_argument("--level1", action="store_true", help="only the 3755 level 1 hanzi of GB2312")
ap.add_argument("-o", default=os.path.join(HERE, "../src/ui/fonts/NotoSansSC-subset.ttf"))
a = ap.parse_args()

chars = set()
for hi in range(0xA1, 0xF8):
    for lo in range(0xA1, 0xFF):
        # level 1 hanzi are rows 0xB0..0xD7, level 2 rows 0xD8..0xF7, rows 0xA1..0xA9 symbols
        if a.level1 and hi >= 0xD8:
            continue
        try:
            chars.add(bytes([hi, lo]).decode("gb2312"))
        except UnicodeDecodeError:
            pass
if not a.level1:
    chars.update(open(os.path.join(HERE, "data/tgh8105.txt"), encoding="utf-8").read().strip())
# characters that are asked for one by one (tools/data/extra.txt): names that are in no standard list
extra = os.path.join(HERE, "data/extra.txt")
if os.path.exists(extra):
    chars.update(c for c in open(extra, encoding="utf-8").read() if not c.isspace())
ranges = [(0x20, 0x7E), (0xA0, 0xFF), (0x2010, 0x2027), (0x2030, 0x203E), (0x2190, 0x2199),
          (0x2460, 0x2473), (0x25A0, 0x25CF), (0x2605, 0x2606), (0x3000, 0x303F), (0x3041, 0x3096),
          (0x30A1, 0x30FA), (0x30FB, 0x30FF), (0xFF01, 0xFF5E), (0xFFE0, 0xFFE5)]
for lo, hi in ranges:
    chars.update(chr(c) for c in range(lo, hi + 1))
text = "".join(sorted(chars))

from fontTools import subset
opts = subset.Options()
opts.layout_features = ["kern"]
opts.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14]
opts.notdef_outline = True
opts.hinting = False
opts.desubroutinize = True
opts.drop_tables += ["GSUB", "GPOS", "GDEF", "DSIG", "vhea", "vmtx", "gasp", "STAT", "meta"]
font = subset.load_font(SRC, opts)
sub = subset.Subsetter(opts)
sub.populate(text=text)
sub.subset(font)
os.makedirs(os.path.dirname(a.o), exist_ok=True)
subset.save_font(font, a.o, opts)
print("%s: %d characters asked for, %d glyphs, %d bytes" %
      (os.path.relpath(a.o), len(chars), len(font.getGlyphOrder()), os.path.getsize(a.o)))
