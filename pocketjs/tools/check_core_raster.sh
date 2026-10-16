#!/bin/sh
# Check patch_core_raster.py on the host: the patched rasterizer must give byte-identical
# pictures to the original for random triangles, textured triangles and textured quads,
# and the tests of the core must pass. Needs cargo and network access for the crates.
set -e
here=$(cd "$(dirname "$0")" && pwd)
pj=$here/../pocketjs
work=${TMPDIR:-/tmp}/pocketjs_raster_check
rm -rf "$work"
mkdir -p "$work/engine"
cp -r "$pj/engine/core" "$work/engine/core"
cp "$pj/engine/Cargo.lock" "$work/engine/core/"
ln -s "$pj/tests" "$work/tests"
python3 "$here/patch_core_raster.py" "$work/engine/core/src/raster.rs" "$pj/engine/core/src/raster.rs" \
	--with-reference
cd "$work/engine/core" && cargo test --release 2>&1 | grep -E "^test result|FAILED|panicked|differs|^error"

# the renderer with all of its patches: its own tests must pass as they do unpatched
bk=${TMPDIR:-/tmp}/pocketjs_backend_check
rm -rf "$bk"
mkdir -p "$bk/engine/backends"
cp -r "$pj/engine/core" "$bk/engine/core"
cp -r "$pj/engine/backends/rgb565" "$bk/engine/backends/rgb565"
cp "$pj/engine/Cargo.lock" "$bk/engine/"
ln -s "$pj/tests" "$bk/tests"
python3 "$here/patch_rgb565.py" "$bk/engine/backends/rgb565/src/lib.rs"
cd "$bk/engine/backends/rgb565" && cargo test --release 2>&1 | grep -E "^test result|FAILED|panicked|^error"

