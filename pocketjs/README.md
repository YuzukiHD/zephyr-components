# PocketJS for Zephyr (F101)

Zephyr module for [PocketJS](https://github.com/pocket-nexus/pocketjs) (submodule `pocketjs/`, MIT): the
caller-driven components of `hosts/esp-idf` (package parser, QuickJS-ng guest, retained UI core,
RGB565 software renderer), compiled unchanged. `shim/` stands in for the few ESP-IDF headers
(`esp_err.h`, `esp_log.h`, `esp_heap_caps.h`) and the gaps of the minimal C library; `src/pocketjs_std.c`
has `strdup`, `strtod` (the number parser of QuickJS), `gettimeofday` and the `console.log` helpers
instead of `quickjs-libc.c`.

- QuickJS-ng is the `quickjs-ng/` submodule at v0.14.0; `tools/prepare_quickjs.py` applies the
  immutable ArrayBuffer patch of PocketJS and makes `Date` UTC-only. Threads and atomics are off.
- The UI core and the renderer are Rust static libraries built by cargo for
  `riscv32imac-unknown-none-elf` (`rustup target add riscv32imac-unknown-none-elf`).
- With `CONFIG_FPU` the Rust libraries are built for ilp32d (and `+v` with `POCKETJS_RUST_RVV`) via
  `tools/riscv32imafdc[v]-zephyr.json` and `-Zbuild-std` (`nightly-2026-07-02`, rust-src).
- `pocketjs_g2d_*` (`include/pocketjs_zephyr/g2d_accel.h`) plugs the G2D into the renderer.
- Not ported: the `pocketjs_runner` task (the sample runs the tick loop itself), the ESP32-P4 PPA.

Sample `samples/player` (an app in `app/`, packaged with the PocketJS CLI, needs `bun`):
`west build -b yuzukineko -d build/pjs zephyr-components/pocketjs/samples/player`.
Verified on the yuzukineko board (cards ~59 ticks/s, motions ~50, hero, gallery); the picture itself
(a blue card with a title on a dark background) has to be looked at. No input is wired yet.
