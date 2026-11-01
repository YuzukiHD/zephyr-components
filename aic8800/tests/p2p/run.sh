#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
cd "$(dirname "$0")"
test_build=$(mktemp -d)
trap 'rm -rf "$test_build"' EXIT INT TERM
# Run the production control parser and group creation with radio operations stubbed.
python3 - "$test_build/functions.inc" <<'PY'
import pathlib
import sys

root = pathlib.Path("../../host/wpa_supplicant/wpa_supplicant")
functions = []
for filename, start, end in (
    ("p2p_supplicant.c", "int wpas_p2p_group_add(", "static int wpas_start_p2p_client("),
    ("ctrl_iface.c", "static int p2p_ctrl_group_add(", "static int p2p_ctrl_group_member("),
):
    text = (root / filename).read_text()
    begin = text.index(start)
    functions.append(text[begin:text.index(end, begin)])
pathlib.Path(sys.argv[1]).write_text("\n".join(functions))
PY
gcc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$test_build" test_group.c -o "$test_build/test_group"
"$test_build/test_group"
