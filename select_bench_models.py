#!/usr/bin/env python3
# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0
"""Prepare benchncnn.cpp for the board.

usage: select_bench_models.py SRC DST MODELS MODELS_DIR MAX_WEIGHTS_MB

- keeps the benchmark() calls of the listed networks (all of them if MODELS
  is empty), never the int8 ones (ncnn is built without int8), and not the
  networks with more fp32 weights than MAX_WEIGHTS_MB (0: no limit): ncnn
  does not check the allocation of the weights and crashes when they do not fit
- runs them from the smallest to the biggest, so that one that does not fit
  cannot take the memory of the ones after it
- a network during which a malloc() failed is not reported
"""

import glob
import os
import re
import sys

src, dst, models, models_dir, max_mb = sys.argv[1:6]
want = {m for m in models.split(",") if m} or None
max_mb = float(max_mb)

call = re.compile(r'^\s*benchmark\("([^"]+)",\s*ncnn::Mat')
result = re.compile(r'^(\s*)(fprintf\(stderr, "%20s  min = .*\);)\s*$')


def weight_mb(name):
    """fp32 size of the weights of the network, from its param file"""
    path = os.path.join(models_dir, name + ".param")
    total = 0
    for line in open(path).read().splitlines()[2:]:
        t = line.split()
        if len(t) < 4:
            continue
        nin, nout = int(t[2]), int(t[3])
        kv = dict(x.split("=", 1) for x in t[4 + nin + nout:] if "=" in x)
        if t[0] in ("Convolution", "ConvolutionDepthWise", "Deconvolution",
                    "DeconvolutionDepthWise"):
            total += int(kv.get("6", 0))
        elif t[0] == "InnerProduct":
            total += int(kv.get("2", 0))
    return total * 4 / 1e6


lines = open(src).read().splitlines(True)
calls = []
first = None
out = []
for line in lines:
    m = call.match(line)
    if m:
        name = m.group(1)
        if first is None:
            first = len(out)
            out.append(None)  # placeholder for the sorted calls
        if (want is None or name in want) and not name.endswith("_int8"):
            size = weight_mb(name)
            if max_mb <= 0 or size <= max_mb:
                calls.append((size, name, line))
        continue

    m = result.match(line)
    if m:
        out.append(f"{m.group(1)}if (!ncnn_zephyr_oom_seen())\n")
        out.append(f"{m.group(1)}    {m.group(2)}\n")
        continue

    if line.strip() == "g_blob_pool_allocator.clear();" and "ncnn_zephyr_oom_clear" not in "".join(
            x for x in out if x):
        out.append("    ncnn_zephyr_oom_clear();\n")

    out.append(line)

calls.sort()
out[first] = "".join(l for _, _, l in calls)

text = "".join(out)
text = text.replace('#include "benchncnn_param_data.h"',
                    '#include "benchncnn_param_data.h"\n\n'
                    'extern "C" void ncnn_zephyr_oom_clear(void);\n'
                    'extern "C" int ncnn_zephyr_oom_seen(void);\n', 1)
open(dst, "w").write(text)

sys.stderr.write("benchncnn networks: %s\n" % ", ".join(
    "%s (%.1f MB)" % (n, s) for s, n, _ in calls))
