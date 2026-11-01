#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
cd "$(dirname "$0")"
test_build=$(mktemp -d)
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
for features in both video minimal; do
	case "$features" in
		both) flags="-DCONFIG_NEKOBOX_FEATURE_DLNA -DCONFIG_NEKOBOX_FEATURE_VIDEO" ;;
		video) flags="-DCONFIG_NEKOBOX_FEATURE_VIDEO" ;;
		minimal) flags="" ;;
	esac
	gcc -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer -pthread -DCONFIG_NEKOBOX_FEATURE_MIRACAST \
		'-DCONFIG_NEKOBOX_MIRACAST_NAME="NekoBox Miracast"' $flags \
		-Ishim -I../../../miracast/tests/host/shim -I../../src \
		-I../../../miracast/include -I../../../aic8800/include \
		test_session.c ../../src/features/miracast/session.c -o "$test_build/$features"
	timeout 30 "$test_build/$features"
done
