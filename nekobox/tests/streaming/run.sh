#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
cd "$(dirname "$0")"
ws=$(cd ../../../.. && pwd)
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=320x240:rate=15 \
	-f lavfi -i sine=frequency=440:sample_rate=48000 -t 12 -c:v libx264 -b:v 900k \
	-pix_fmt yuv420p -c:a aac -movflags +faststart "$build_dir/fast.mp4"
ffmpeg -hide_banner -loglevel error -i "$build_dir/fast.mp4" -c copy "$build_dir/tail.mp4"
for size in 640x360 360x640 642x360 640x362 480x480 1280x720 1920x1080; do
	ffmpeg -hide_banner -loglevel error -f lavfi -i "color=size=$size:rate=1" \
		-frames:v 1 -c:v libx264 -preset ultrafast -pix_fmt yuv420p -movflags +faststart \
		"$build_dir/$size.mp4"
done
gcc -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer \
	-pthread -Ishim -I../../src/features/video -I../../src/features/dlna \
	-I../../../dlna/include -I"$ws/zephyr/include" \
	test_streaming.c ../../src/features/video/player_file.c ../../src/features/dlna/fetch.c \
	"$ws/zephyr/lib/mp4/mp4.c" -o "$build_dir/test_streaming"
timeout 45 "$build_dir/test_streaming" "$build_dir"
