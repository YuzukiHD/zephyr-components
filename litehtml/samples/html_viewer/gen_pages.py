#!/usr/bin/env python3
# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0
#
# Writes big test pages for the html_viewer: gen_pages.py OUTDIR SIZE_MB [SIZE_MB ...]
# Each page is a run of sections (heading, text, list, table, boxes) up to the size.
import os
import sys

HEAD = """<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>big page %s MB</title>
<style>
body{margin:0;background:#eef1f7;color:#1d2433;font-size:16px}
h2{color:#1a4fa0;border-bottom:2px solid #1a4fa0;margin:18px 24px 6px 24px}
.card{background:#fff;border:1px solid #b9c4da;padding:8px 14px;margin:6px 24px}
.chip{display:inline-block;padding:2px 8px;margin:2px;color:#fff}
.a{background:#c0392b}.b{background:#1e8449}.c{background:#2471a3}.d{background:#d68910}
table{border-collapse:collapse;width:100%%}th{background:#1a4fa0;color:#fff;padding:3px 8px}
td{border:1px solid #b9c4da;padding:3px 8px}.n{border-left:6px solid #d68910;background:#fdf3e0;padding:4px 10px}
</style></head><body>
"""

SECTION = """<h2>Section %(i)d</h2>
<div class="card"><p>Section %(i)d: <b>bold</b>, <i>italic</i>, <u>underline</u> and plain text that
runs over a few lines so that the line breaking has work to do. The quick brown fox jumps over
the lazy dog, %(i)d times; lorem ipsum dolor sit amet, consectetur adipiscing elit.</p>
<span class="chip a">red %(i)d</span><span class="chip b">green</span><span class="chip c">blue</span>
<span class="chip d">orange</span></div>
<div class="card"><ul><li>item one of section %(i)d</li><li>item two</li><li>item three
<ul><li>nested a</li><li>nested b</li></ul></li></ul></div>
<table><tr><th>#</th><th>Name</th><th>Value</th></tr>
<tr><td>%(i)d.1</td><td>alpha</td><td>%(v1)d</td></tr>
<tr><td>%(i)d.2</td><td>beta</td><td>%(v2)d</td></tr>
<tr><td>%(i)d.3</td><td>gamma</td><td>%(v3)d</td></tr></table>
<div class="card"><div class="n">note for section %(i)d</div></div>
"""

def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for mb in sys.argv[2:]:
        size = int(float(mb) * 1024 * 1024)
        name = os.path.join(out, "page_%sm.html" % mb.replace(".", "_"))
        n = 0
        total = 0
        with open(name, "w") as f:
            head = HEAD % mb
            f.write(head)
            total += len(head)
            while total < size:
                n += 1
                s = SECTION % {"i": n, "v1": n * 7 % 1000, "v2": n * 13 % 1000, "v3": n * 31 % 1000}
                f.write(s)
                total += len(s)
            f.write("</body></html>\n")
        print(name, n, "sections", os.path.getsize(name), "bytes")

main()
