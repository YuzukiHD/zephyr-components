#!/bin/sh
# Builds and runs the host tests of the protocol layer (no Zephyr needed)
set -e
cd "$(dirname "$0")"
mkdir -p ../../build
gcc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
	-DCONFIG_DLNA_MAX_REQUEST=12288 -I../../include -I../../src \
	test_dlna.c ../../src/util.c ../../src/soap.c ../../src/upnp.c -o ../../build/test_dlna
../../build/test_dlna
gcc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -pthread \
	-DCONFIG_DLNA_MAX_REQUEST=12288 -DCONFIG_DLNA_MAX_SUBSCRIBERS=4 \
	-DCONFIG_DLNA_STACK_SIZE=6144 -DCONFIG_DLNA_THREAD_PRIORITY=9 -DCONFIG_DLNA_HTTP_PORT=0 \
	-Ishim -I../../include -I../../src \
	test_http.c ../../src/util.c ../../src/soap.c ../../src/upnp.c -o ../../build/test_http
python3 test_http.py --server ../../build/test_http
