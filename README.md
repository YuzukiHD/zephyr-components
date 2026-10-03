# ncnn-zephyr

[ncnn](https://github.com/Tencent/ncnn) as a Zephyr module for RISC-V
(rv32imafdcv, with the V extension). ncnn itself is the git submodule `ncnn/`.

Tested on the Allwinner F101 EVB (XuanTie C907).

ncnn is built by its own CMake project with the architecture options of the
image, `NCNN_SIMPLESTL` (its own STL, no libstdc++) and the headers of the
minimal C library plus `shim/`. With the V extension in the image the RVV
kernels of ncnn are used. `ncnn_libc.c` has what ncnn needs from the C library
and the minimal one does not provide (`sscanf`, a clock, `usleep`, file stubs).

## Use

```
git submodule update --init
west build -b f101_evb -d build/ncnn ncnn-zephyr/samples/benchmark
```

or, in another application, add the module and enable it:

```
list(APPEND ZEPHYR_EXTRA_MODULES /path/to/ncnn-zephyr)   # before find_package(Zephyr)
```

```
CONFIG_CPP=y
CONFIG_NCNN=y
CONFIG_SUN252I_F101_RVV=y
CONFIG_FPU=y
```

`CONFIG_NCNN_BENCHMARK` builds benchncnn as `int ncnn_bench_main(int argc, char **argv)`
with the networks built in (random weights): all of them except the int8 ones and
those with more than `CONFIG_NCNN_BENCHMARK_MAX_WEIGHTS_MB` of weights, smallest
first. A network during which a `malloc()` fails is not reported.

## Notes

- ncnn is built without int8 (`NCNN_INT8=OFF`).
- Networks are loaded from memory only, there is no file system access.
- The weights are fp32 in RAM and the layers copy them while they are set up.
  `COMMON_LIBC_MALLOC_ARENA_SIZE=-1` gives all RAM above the image to the heap;
  with 16 MB PSRAM that is 13 MB, enough for networks of a few M parameters.
  ncnn does not check these allocations, a network that is too big fails or
  crashes.
- ncnn only detects a vector unit under Linux and reports a vector length of 0
  everywhere else, which breaks the tensor layout; `ext/riscv_cpu.cpp` replaces
  that detection, the two functions in `libncnn.a` are renamed after the build.
- With the V extension every thread stack has to hold an interrupt frame
  (about 560 bytes with VLEN 128).

## License

Apache-2.0, see `LICENSE`. ncnn is under its own license (BSD-3-Clause).
