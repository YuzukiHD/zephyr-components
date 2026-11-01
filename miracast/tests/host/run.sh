#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
cd "$(dirname "$0")"
test_build=$(mktemp -d)
trap 'rm -rf "$test_build"' EXIT INT TERM
ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=640x480:rate=30 \
  -f lavfi -i sine=frequency=997:sample_rate=48000 -t 1.5 \
  -c:v libx264 -profile:v baseline -level 3.1 -preset ultrafast -tune zerolatency \
  -x264-params keyint=15:scenecut=0 -c:a aac -ac 2 -b:a 128k \
  -f mpegts "$test_build/input.ts"
gcc -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -I../../include -I../../src \
  test_core.c ../../src/session.c ../../src/media.c -o "$test_build/test_core"
"$test_build/test_core" "$test_build/input.ts" "$test_build/output"
ffmpeg -hide_banner -loglevel error -i "$test_build/input.ts" \
  -map 0:v:0 -c copy -f h264 "$test_build/expected.h264" \
  -map 0:a:0 -c copy -f adts "$test_build/expected.aac"
cmp "$test_build/output.h264" "$test_build/expected.h264"
cmp "$test_build/output.aac" "$test_build/expected.aac"
ffprobe -v error -show_packets -show_entries packet=codec_type,pts \
  -of json "$test_build/input.ts" > "$test_build/packets.json"
python3 - "$test_build" <<'PY'
import json, pathlib, sys
root = pathlib.Path(sys.argv[1])
actual = {0: [], 2: []}
for line in (root / "output.pts").read_text().splitlines():
    codec, pts = map(int, line.split())
    actual[codec].append(pts)
packets = json.loads((root / "packets.json").read_text())["packets"]
expected_video = [int(p["pts"]) for p in packets if p["codec_type"] == "video"]
expected_audio = {int(p["pts"]) for p in packets if p["codec_type"] == "audio"}
assert actual[0] == expected_video, "Video PTS differs from ffprobe"
assert actual[2] and all(pts in expected_audio for pts in actual[2]), "Audio PES PTS differs"
print("FFmpeg elementary streams and ffprobe timestamps match")
PY
gcc -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -pthread \
  -fno-omit-frame-pointer -DCONFIG_MIRACAST_STACK_SIZE=8192 \
  -DCONFIG_MIRACAST_THREAD_PRIORITY=7 -DCONFIG_MIRACAST_TIMEOUT_MS=5000 \
  -Ishim -I../../include -I../../src test_receiver.c ../../src/session.c \
  ../../src/media.c ../../src/receiver.c -o "$test_build/test_receiver"
python3 test_receiver.py "$test_build/test_receiver" "$test_build/input.ts"
