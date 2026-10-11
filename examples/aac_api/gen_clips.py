#!/usr/bin/env python3
"""Make src/clips.h: two AAC clips (ADTS) and two reference frames each, decoded by ffmpeg.

usage: gen_clips.py VIDEO.mp4 > src/clips.h
"""
import array
import subprocess
import sys
import tempfile
import os

video = sys.argv[1]


def run(*args):
    subprocess.run(["ffmpeg", "-v", "error", "-y", *args], check=True)


def carr(name, data, typ, per):
    s = "static const %s %s[%d] = {\n" % (typ, name, len(data))
    for i in range(0, len(data), per):
        row = data[i:i + per]
        s += "\t" + ", ".join(str(x) if typ == "int16_t" else "0x%02x" % x for x in row) + ",\n"
    return s + "};\n\n"


def pcm(path):
    a = array.array("h")
    a.frombytes(open(path, "rb").read())
    return a


with tempfile.TemporaryDirectory() as d:
    a, m = os.path.join(d, "a.aac"), os.path.join(d, "m.aac")
    run("-i", video, "-vn", "-acodec", "copy", "-frames:a", "64", a)
    run("-i", video, "-vn", "-ac", "1", "-ar", "22050", "-c:a", "aac", "-b:a", "32k",
        "-frames:a", "24", "-f", "adts", m)
    run("-i", a, "-f", "s16le", "-acodec", "pcm_s16le", os.path.join(d, "a.pcm"))
    run("-i", m, "-f", "s16le", "-acodec", "pcm_s16le", os.path.join(d, "m.pcm"))
    clip_a, clip_b = open(a, "rb").read(), open(m, "rb").read()
    ref_a, ref_b = pcm(os.path.join(d, "a.pcm")), pcm(os.path.join(d, "m.pcm"))

out = """/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Generated test data.
 *
 * clip_a: the first 64 AAC frames of the audio track of video540p.mp4, AAC-LC, 44.1 kHz,
 *         stereo, ADTS (ffmpeg -i video540p.mp4 -vn -acodec copy -frames:a 64 a.aac).
 * clip_b: 26 frames of AAC-LC, 22.05 kHz, mono, ADTS, encoded from the same track
 *         (ffmpeg -ac 1 -ar 22050 -c:a aac -b:a 32k -f adts).
 * ref_a, ref_b: two frames of the ffmpeg decode of the clips, signed 16 bit, to compare with.
 */

#ifndef CLIPS_H_
#define CLIPS_H_

#include <stdint.h>

"""
out += carr("clip_a", clip_a, "uint8_t", 16) + carr("clip_b", clip_b, "uint8_t", 16)
out += "#define REF_A_FIRST_FRAME 20\n#define REF_A_FRAMES 2\n"
out += carr("ref_a", ref_a[20 * 2048:22 * 2048].tolist(), "int16_t", 12)
out += "#define REF_B_FIRST_FRAME 10\n#define REF_B_FRAMES 2\n"
out += carr("ref_b", ref_b[10 * 1024:12 * 1024].tolist(), "int16_t", 12)
out += "#endif /* CLIPS_H_ */\n"
sys.stdout.write(out)
