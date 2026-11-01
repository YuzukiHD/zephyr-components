#!/bin/sh
# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
workspace=$(CDPATH= cd -- "$test_dir/../../../.." && pwd)
mbedtls="$workspace/modules/crypto/mbedtls"
psa="$workspace/modules/crypto/tf-psa-crypto"
test_build=$(mktemp -d)
trap 'rm -rf "$test_build"' EXIT INT TERM

# Reuse native libraries if supplied; otherwise build the workspace's exact revisions.
crypto_build=${MIRROR_HOST_CRYPTO_BUILD:-"$test_build/crypto"}
if [ -z "${MIRROR_HOST_CRYPTO_BUILD:-}" ]; then
  cmake -S "$mbedtls" -B "$crypto_build" -DGEN_FILES=OFF \
    -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DMBEDTLS_FATAL_WARNINGS=OFF \
    -DTF_PSA_CRYPTO_DIR="$psa" -DTF_PSA_CRYPTO_DISPATCH_DIR="$psa/dispatch"
  cmake --build "$crypto_build" -j 4
fi

cc -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -ffunction-sections -fdata-sections \
  -DMBEDTLS_DECLARE_PRIVATE_IDENTIFIERS -I"$test_dir/shim" \
  -I"$test_dir/../../include" -I"$test_dir/../../src" \
  -I"$mbedtls/include" -I"$psa/include" -I"$psa/drivers/builtin/include" \
  -I"$crypto_build/tf-psa-crypto/include" -I"$psa/dispatch/include" \
  "$test_dir/test_crypto.c" "$test_dir/../../src/adb_auth.c" \
  -Wl,--gc-sections -Wl,--start-group "$crypto_build/library/libmbedx509.a" \
  "$crypto_build/library/libtfpsacrypto.a" -Wl,--end-group \
  -o "$test_build/test_crypto"
cd "$test_build"
./test_crypto
python3 "$test_dir/verify.py"
