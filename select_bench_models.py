#!/usr/bin/env python3
# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0
"""Drop the benchmark() calls of benchncnn.cpp for the networks not listed."""

import re
import sys

src, dst, models = sys.argv[1], sys.argv[2], set(sys.argv[3].split(","))
call = re.compile(r'^\s*benchmark\("([^"]+)",\s*ncnn::Mat')

with open(src) as f, open(dst, "w") as out:
    for line in f:
        m = call.match(line)
        if m and m.group(1) not in models:
            continue
        out.write(line)
