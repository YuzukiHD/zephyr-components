#!/usr/bin/env python3
"""Apply the immutable-ArrayBuffer patch of the PocketJS ESP-IDF host to quickjs-ng.

The patch script of the pocketjs submodule is pinned to the source hash of the
Espressif package; the same structural edits apply to the quickjs-ng v0.14.0 tag,
so its hash check is replaced by one on the tag's file.
"""
import hashlib
import importlib.util
import sys
from pathlib import Path

TAG_SHA256 = "0dc62d8f9a2ed0e6f19fba948a1c54ee09c8d5c74545a814918716da89ccc35f"

src, dst, upstream = sys.argv[1:4]
spec = importlib.util.spec_from_file_location("upstream_prepare", upstream)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

data = Path(src).read_bytes()
if hashlib.sha256(data).hexdigest() != TAG_SHA256:
    sys.exit("quickjs-ng is not v0.14.0; review the immutable-buffer patch first")
mod.SOURCE_SHA256 = TAG_SHA256
result = mod.prepare(data)
# no time zone database: local time is UTC (the minimal libc has no tm_gmtoff)
old = "    return -tm.tm_gmtoff / 60;"
if old not in result:
    sys.exit("getTimezoneOffset not found in quickjs.c")
result = result.replace(old, "    return 0;")
out = Path(dst)
out.parent.mkdir(parents=True, exist_ok=True)
if not out.exists() or out.read_text() != result:
    out.write_text(result)
