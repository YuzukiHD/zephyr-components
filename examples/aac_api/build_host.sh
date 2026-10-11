#!/bin/sh
# build the decoder sources and host_decode.c for the host: build_host.sh OUT_DIR
set -e
Z=${ZEPHYR_BASE:-$HOME/f101-zephyr/zephyr}
AAC=$Z/../modules/lib/opencore/codecs_v2/audio/aac/dec
INC=$Z/lib/opencore_aac/include
OUT=$1
mkdir -p "$OUT/obj"
for s in "$AAC"/src/*.cpp; do
	b=$(basename "$s" .cpp)
	[ "$b" = decoder_aac ] && continue
	# the .cpp files are C
	echo "gcc -x c -std=gnu99 -O2 -w -include $INC/oscl_base.h -I$AAC/src -I$AAC/include -I$INC -c $s -o $OUT/obj/$b.o"
done | xargs -P8 -I{} sh -c '{}'
gcc -std=gnu99 -O2 -w -I"$AAC/include" -I"$INC" -I"$AAC/src" "$(dirname "$0")/host_decode.c" \
	"$OUT"/obj/*.o -o "$OUT/host_decode"
