#!/usr/bin/env python3
# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0
#
# Makes a web page showable on the board: scrape_page.py URL OUTDIR [--font FONT.otf]
#
# OUTDIR gets index.html (no scripts, style sheets inlined, images renamed), img/*.lhi (the
# images as RGB565, see lh_image format below) and font.lhf (the glyphs of the non ASCII
# characters of the page). Copy OUTDIR to the SD card and show OUTDIR/index.html.
#
# .lhi: "LHI1", u16 width, u16 height (little endian), width*height RGB565 little endian
# .lhf: "LHF1", u32 count, u32 cell (16), count * u32 code points (sorted),
#       count * cell*cell bytes of alpha (rows from the top)
#
# Needs Pillow (python3 -m pip install pillow). The scripts of the page are dropped: neither
# litehtml nor the board runs them.

import argparse
import concurrent.futures
import hashlib
import html
import os
import re
import struct
import sys
import urllib.parse
import urllib.request
from io import BytesIO

from PIL import Image, ImageDraw, ImageFont

UA = ("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
      "Chrome/124.0 Safari/537.36")


def fetch(url, binary=False, accept=None):
    req = urllib.request.Request(url, headers={
        "User-Agent": UA,
        "Referer": "https://www.bilibili.com/",
        "Accept": accept or "*/*",
    })
    with urllib.request.urlopen(req, timeout=30) as r:
        data = r.read()
    return data if binary else data.decode("utf-8", "replace")


def absolute(base, ref):
    ref = html.unescape(ref.strip())
    if ref.startswith("//"):
        ref = "https:" + ref
    return urllib.parse.urljoin(base, ref)


def jpeg_url(url):
    """The image CDN of the page resizes with a suffix and converts with the extension."""
    if "hdslb.com" in url and "@" not in url:
        return url
    m = re.match(r"^(.*?\.(?:jpg|jpeg|png|webp|avif|gif))(@[^?]*?)?(\.(?:webp|avif|png|jpg))?(\?.*)?$",
                 url, re.I)
    if m and "hdslb.com" in url:
        size = (m.group(2) or "")
        return m.group(1) + size.split("!")[0] + ".jpg"
    return url


def to_lhi(data, max_w, max_h):
    img = Image.open(BytesIO(data))
    if img.mode in ("RGBA", "LA", "P"):
        img = img.convert("RGBA")
        bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
        bg.alpha_composite(img)
        img = bg
    img = img.convert("RGB")
    img.thumbnail((max_w, max_h), Image.LANCZOS)
    w, h = img.size
    px = img.tobytes()
    out = bytearray(struct.pack("<4sHH", b"LHI1", w, h))
    for i in range(0, len(px), 3):
        r, g, b = px[i], px[i + 1], px[i + 2]
        out += struct.pack("<H", ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3))
    return bytes(out), w, h


class Images:
    def __init__(self, outdir, limit, max_w, max_h):
        self.dir = os.path.join(outdir, "img")
        os.makedirs(self.dir, exist_ok=True)
        self.limit = limit
        self.max_w, self.max_h = max_w, max_h
        self.names = {}   # absolute url -> local name or None
        self.todo = []

    def local(self, url):
        if url in self.names:
            return self.names[url]
        if len(self.names) >= self.limit:
            return None
        name = "img/%s.lhi" % hashlib.sha1(url.encode()).hexdigest()[:12]
        self.names[url] = name
        self.todo.append(url)
        return name

    def download(self):
        def one(url):
            try:
                data = fetch(jpeg_url(url), binary=True, accept="image/jpeg,image/png,*/*;q=0.5")
                lhi, w, h = to_lhi(data, self.max_w, self.max_h)
                with open(os.path.join(self.dir, os.path.basename(self.names[url])), "wb") as f:
                    f.write(lhi)
                return url, w, h, len(lhi)
            except Exception as e:  # keep going: a missing picture is not fatal
                return url, 0, 0, str(e)

        ok = 0
        with concurrent.futures.ThreadPoolExecutor(8) as ex:
            for url, w, h, info in ex.map(one, self.todo):
                if w:
                    ok += 1
                else:
                    self.names[url] = None
                    print("  image failed:", url[:90], info, file=sys.stderr)
        print("images: %d of %d" % (ok, len(self.todo)))


def strip_scripts(doc):
    doc = re.sub(r"<script\b.*?</script\s*>", "", doc, flags=re.S | re.I)
    doc = re.sub(r"<noscript\b.*?</noscript\s*>", "", doc, flags=re.S | re.I)
    doc = re.sub(r"<!--.*?-->", "", doc, flags=re.S)
    # event handlers and javascript: links
    doc = re.sub(r"\s+on[a-z]+\s*=\s*(\"[^\"]*\"|'[^']*')", "", doc, flags=re.I)
    doc = re.sub(r"(href\s*=\s*)([\"'])javascript:[^\"']*\2", r"\1\2#\2", doc, flags=re.I)
    return doc


def clean_css(css):
    css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
    # blocks the renderer cannot use and that cost memory
    for at in (r"@font-face", r"@keyframes", r"@-webkit-keyframes", r"@supports", r"@property",
               r"@container", r"@layer"):
        while True:
            m = re.search(at + r"[^{;]*\{", css)
            if not m:
                break
            depth, i = 1, m.end()
            while i < len(css) and depth:
                depth += {"{": 1, "}": -1}.get(css[i], 0)
                i += 1
            css = css[:m.start()] + css[i:]
    css = re.sub(r"@import[^;]*;", "", css)
    return css


def _block_end(css, i):
    depth, j = 0, i
    while j < len(css):
        if css[j] == "{":
            depth += 1
        elif css[j] == "}":
            depth -= 1
            if depth == 0:
                return j + 1
        j += 1
    return len(css)


def _query_ok(q, width):
    q = q.strip().lower()
    if any(w in q for w in ("print", "prefers", "hover", "pointer", "orientation", "resolution")):
        return False
    for m in re.finditer(r"\((min|max)-width\s*:\s*([0-9.]+)px\)", q):
        v = float(m.group(2))
        if (m.group(1) == "min" and width < v) or (m.group(1) == "max" and width > v):
            return False
    return True


def flatten_media(css, width):
    """The @media blocks are evaluated here, for a viewport of the given width: litehtml
    reads few of the conditions of a modern style sheet. What matches is kept, the rest goes."""
    out, i = [], 0
    while i < len(css):
        k = css.find("@media", i)
        if k < 0:
            out.append(css[i:])
            break
        out.append(css[i:k])
        b = css.find("{", k)
        end = _block_end(css, b)
        if any(_query_ok(q, width) for q in css[k + 6:b].split(",")):
            out.append(flatten_media(css[b + 1:end - 1], width))
        i = end
    return "".join(out)


def grid_to_flex(css):
    """No grid in litehtml: containers wrap their children like a row of boxes instead."""
    css = css.replace("display:grid", "display:flex;flex-wrap:wrap")
    css = css.replace("display:inline-grid", "display:inline-flex;flex-wrap:wrap")
    return css


def rewrite_css_urls(css, base, images):
    def sub(m):
        ref = m.group(2).strip()
        if ref.startswith("data:"):
            return "none"
        url = absolute(base, ref)
        if not re.search(r"\.(jpe?g|png|webp|gif|avif)", url, re.I):
            return "none"
        name = images.local(url)
        return "url(%s)" % name if name else "none"

    return re.sub(r"url\(\s*([\"']?)([^)\"']*)\1\s*\)", sub, css)


def make_font(chars, fontfile, path, cell=16):
    codes = sorted(c for c in chars if c >= 0x80 and c not in (0x200b, 0xfeff))
    font = ImageFont.truetype(fontfile, cell)
    glyphs = []
    keep = []
    for c in codes:
        im = Image.new("L", (cell, cell), 0)
        d = ImageDraw.Draw(im)
        d.text((0, -1), chr(c), font=font, fill=255)
        if im.getbbox() is None:
            continue
        keep.append(c)
        glyphs.append(im.tobytes())
    with open(path, "wb") as f:
        f.write(struct.pack("<4sIII", b"LHF1", len(keep), cell, 0))
        f.write(b"".join(struct.pack("<I", c) for c in keep))
        f.write(b"".join(glyphs))
    print("font: %d glyphs, %d bytes" % (len(keep), os.path.getsize(path)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("url")
    ap.add_argument("outdir")
    ap.add_argument("--font", default="")
    ap.add_argument("--max-images", type=int, default=120)
    ap.add_argument("--max-image-size", default="640x360")
    ap.add_argument("--keep-css", action="store_true", help="do not drop @font-face, @keyframes...")
    ap.add_argument("--viewport", type=int, default=1024, help="width the @media queries are evaluated for")
    ap.add_argument("--fixup", action="append", default=[], help="extra style sheet appended after the page's")
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    mw, mh = (int(v) for v in args.max_image_size.split("x"))
    images = Images(args.outdir, args.max_images, mw, mh)

    doc = fetch(args.url)
    print("page: %d bytes" % len(doc))
    doc = strip_scripts(doc)

    # style sheets: inlined where the link was
    def inline_css(m):
        tag = m.group(0)
        if not re.search(r"rel\s*=\s*[\"']?stylesheet", tag, re.I):
            return ""
        href = re.search(r"href\s*=\s*[\"']([^\"']+)[\"']", tag, re.I)
        if not href:
            return ""
        url = absolute(args.url, href.group(1))
        try:
            css = fetch(url)
        except Exception as e:
            print("  css failed:", url, e, file=sys.stderr)
            return ""
        print("css: %s %d bytes" % (url[-50:], len(css)))
        css = css if args.keep_css else clean_css(css)
        css = grid_to_flex(flatten_media(css, args.viewport))
        return "<style>%s</style>" % rewrite_css_urls(css, url, images)

    doc = re.sub(r"<link\b[^>]*>", inline_css, doc, flags=re.I)

    def style_block(m):
        return m.group(1) + rewrite_css_urls(m.group(2), args.url, images) + m.group(3)

    doc = re.sub(r"(<style\b[^>]*>)(.*?)(</style>)", style_block, doc, flags=re.S | re.I)
    doc = re.sub(r"(style\s*=\s*\")([^\"]*)(\")",
                 lambda m: m.group(1) + rewrite_css_urls(m.group(2), args.url, images) + m.group(3),
                 doc, flags=re.I)

    # pictures: only the <img> element, with the first usable address
    doc = re.sub(r"</?(picture|source)\b[^>]*>", "", doc, flags=re.I)

    def img_tag(m):
        tag = m.group(0)
        src = None
        for attr in ("src", "data-src", "data-original"):
            a = re.search(r"\b%s\s*=\s*\"([^\"]*)\"" % attr, tag, re.I)
            if a and a.group(1) and not a.group(1).startswith("data:"):
                src = a.group(1)
                break
        tag = re.sub(r"\s(src|srcset|data-src|data-original|loading|decoding)\s*=\s*(\"[^\"]*\"|'[^']*')",
                     "", tag, flags=re.I)
        if src:
            name = images.local(absolute(args.url, src))
            if name:
                tag = tag.replace("<img", '<img src="%s"' % name, 1)
        return tag

    doc = re.sub(r"<img\b[^>]*>", img_tag, doc, flags=re.I)
    for fx in args.fixup:
        with open(fx, encoding="utf-8") as f:
            doc = doc.replace("</head>", "<style>%s</style></head>" % f.read(), 1)
    images.download()

    # images that failed to download or are not supported: no src
    for url, name in list(images.names.items()):
        if name is None:
            pass

    with open(os.path.join(args.outdir, "index.html"), "w", encoding="utf-8") as f:
        f.write(doc)
    print("index.html: %d bytes" % len(doc))

    if args.font:
        chars = set(ord(c) for c in html.unescape(doc))
        make_font(chars, args.font, os.path.join(args.outdir, "font.lhf"))


main()
