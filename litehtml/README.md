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

## Big pages

A page that does not fit the memory is cut into pieces (`lh_index_scan()`, `samples/html_viewer/src/chunked.c`):
the file is read once by a tag scanner that finds the ends of the children of `<body>`, and only the
piece on the screen and the next one are laid out, each as a page of its own with the style sheets of the
head. `CONFIG_SAMPLE_HTML_CHUNK_KB` sets the size of a piece (12 KiB; 0 turns it off). A 30 MiB test page
(`samples/html_viewer/gen_pages.py` makes them) was cut into 2537 pieces in 6.3 s and a piece of about 12 KiB
takes 376 ms to load, 36 ms a frame. The markup must be well formed (end tags for everything that is not
void), a child of `<body>` is never split and rules that look at siblings across pieces do not work.
Pieces are loaded synchronously: about 0.4 s of standstill each time the scroll reaches a new one.

Laying out the page as a whole in demand paged memory (`CONFIG_LITEHTML_PAGED_HEAP`, `paged.conf`, the swap
file backing store of the Zephyr tree) works but does not help much: the tree of a dense page is several
hundred times the size of the text and the layout reads it in random order, a 256 KiB test page needed more
than 70 MiB and minutes.

## Images, extra glyphs and web pages

`lh_set_image_loader()` and `lh_set_cjk_font()` give the renderer the pictures (RGB565, scaled to the size of
the element) and the glyphs of the characters the bitmap font lacks. `tools/scrape_page.py` makes both from a
web page: scripts and event attributes are dropped, the style sheets are inlined (with the `@media` queries
evaluated for the viewport width, no grid), the pictures are converted to `.lhi` files and the glyphs of the
characters of the page are rendered from a font to `font.lhf`. `tools/host_preview` is the renderer on the PC
for looking at the result without the board. Copy the output directory to the card and show its `index.html`;
the viewer reads `img/` and `font.lhf` next to the page.

## Limits

- Text is a fixed width bitmap font (the 10 x 16 frame buffer font) scaled to the CSS size.
- No gradients or rounded corners, borders are solid; images and non ASCII characters need the loader and
  the glyph file above, without them images are not drawn and the characters are drawn as `?`.
- litehtml has no CSS grid and reads few conditions of modern `@media` rules: a page from the web needs the
  scraper's rewriting, and its layout is still only an approximation.
- Measured on the F101 EVB with the test page (1651 px high): 107 ms to parse and lay out, 22 ms
  per 1024 x 600 frame, about 1.6 MiB of heap.
