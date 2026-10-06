# litehtml

[litehtml](https://github.com/litehtml/litehtml) (v0.9, git submodule `litehtml/litehtml`) as a
Zephyr module, with a renderer that draws a page into an RGB565 buffer
(`include/litehtml_zephyr/lh.h`).

litehtml needs the C++ standard library, which does not go with the minimal C library of
the image. It is built by its own CMake project (`ext/`) against the headers of the toolchain
and linked as an imported static library. Only the header-only parts of libstdc++ are used:
`_GLIBCXX_EXTERN_TEMPLATE` is switched off (the precompiled `std::string` also holds the stream
operators and would pull the stream and exception runtime in), the standard streams are
forward declarations (`ext/shim`, `ext/src/url.cpp` and `tstring_view.cpp` are copies without
them), exceptions are off and `std::__throw_*` end in `abort()` (`ext/src/lh_stubs.cpp`).
`src/litehtml_libc.c` has the C library functions that are missing (character table,
`strcasecmp`, wide character helpers, `__errno`).

## Use

```
git submodule update --init litehtml/litehtml
west build -b f101_evb -d build/html zephyr-components/litehtml/samples/html_viewer
```

or in another application: add the module to `ZEPHYR_EXTRA_MODULES` and set `CONFIG_CPP=y`,
`CONFIG_STD_CPP17=y`, `CONFIG_LITEHTML=y`.

`samples/html_viewer` shows `/SD:/page.html` (or the first .html file of the card, or a built in
page) and scrolls a long page by itself. `samples/html_viewer/page.html` is a test page.

## Limits

- Text is a fixed width bitmap font (the 10 x 16 frame buffer font) scaled to the CSS size.
- No images, gradients or rounded corners, borders are solid, non ASCII characters are drawn
  as `?`.
- Measured on the F101 EVB with the test page (1651 px high): 107 ms to parse and lay out, 22 ms
  per 1024 x 600 frame, about 1.6 MiB of heap.
