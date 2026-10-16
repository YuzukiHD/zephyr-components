"""Speed up the software rasterizer of the core (patches a copy of raster.rs).

Run from patch_rgb565.py with sys.argv[1] = raster.rs. Every change gives the same
pixels as before:

* tri / tex_tri: the pixels of a row inside the triangle are one run of columns (the three
  edge functions are linear in x), so the run is found once per row with integer arithmetic
  instead of testing the edges at every pixel, a flat opaque triangle is filled one run at a
  time and the edge functions step by a constant per pixel. Inside the triangle every
  weight lies between 0 and the area, so with an area below 2^31 the i64 -> f32 conversion
  of the weights (a soft-float library call here) is done as i32 -> f32, which rounds the
  same.
* glyph_run: the coverage column of a pixel depends on the column only (the index has a
  division in it), it is computed once per glyph column instead of once per pixel.
* tex_quad: the x part of the sample coordinates depends on the column only and the y part
  on the row only, both are computed once per column and per row; RGBA8 textures are read
  as words and equal texels skip the interpolation.
"""
import sys
from pathlib import Path

path = Path(sys.argv[1])
text = path.read_text()


def replace_between(text, start, end, new, what):
    a = text.find(start)
    if a < 0 or text.count(start) != 1:
        sys.exit(what + ": start marker not found")
    b = text.find(end, a)
    if b < 0:
        sys.exit(what + ": end marker not found")
    return text[:a] + new + text[b:]


# ---- helpers placed before tri() -------------------------------------------------------
helpers = '''/// Narrow the columns [lo, hi) to those where `a + b * px >= 0`.
#[inline]
fn edge_run(a: i64, b: i64, lo: &mut i64, hi: &mut i64) {
    if b == 0 {
        if a < 0 {
            *hi = *lo;
        }
    } else if b > 0 {
        // px >= ceil(-a / b)
        let t = -a;
        let c = if t >= 0 { (t + b - 1) / b } else { -((-t) / b) };
        *lo = (*lo).max(c);
    } else {
        // px <= floor(a / -b)
        let nb = -b;
        let f = if a >= 0 { a / nb } else { -((-a + nb - 1) / nb) };
        *hi = (*hi).min(f + 1);
    }
}

/// The edge function orient(p, q, s) for s = (2 * px + 1, sy) as `a + b * px`.
#[inline]
fn edge_line(pxy: (i64, i64), qxy: (i64, i64), sy: i64) -> (i64, i64) {
    (
        (qxy.0 - pxy.0) * (sy - pxy.1) - (qxy.1 - pxy.1) * (1 - pxy.0),
        -2 * (qxy.1 - pxy.1),
    )
}

'''
marker = "fn tri<T: RenderTarget>("
assert text.count(marker) == 1, "tri not found"
text = text.replace(marker, helpers + marker, 1)

# ---- tri -------------------------------------------------------------------------------
tri_new = '''    let half = area / 2;
    for py in min_y..max_y {
        let sy = 2 * py as i64 + 1;
        let (mut lo, mut hi) = (min_x as i64, max_x as i64);
        // Barycentric weights of the OPPOSITE vertices: w0 on edge b-c, w1 on c-a, w2 on a-b.
        let (a0, d0) = edge_line((bx, by), (cx, cy), sy);
        let (a1, d1) = edge_line((cx, cy), (ax, ay), sy);
        let (a2, d2) = edge_line((ax, ay), (bx, by), sy);
        edge_run(a0, d0, &mut lo, &mut hi);
        edge_run(a1, d1, &mut lo, &mut hi);
        edge_run(a2, d2, &mut lo, &mut hi);
        if lo >= hi {
            continue;
        }
        let row = (py * stride) as usize;
        if flat_opaque {
            target.fill_opaque(row + lo as usize, (hi - lo) as usize, r0, g0, b0);
            continue;
        }
        let (mut w0, mut w1, mut w2) = (a0 + d0 * lo, a1 + d1 * lo, a2 + d2 * lo);
        for px in lo as i32..hi as i32 {
            if flat {
                target.blend(row + px as usize, r0, g0, b0, a0_alpha);
            } else {
                // Integer barycentric interpolation, round-to-nearest.
                let mix = |v0: u32, v1: u32, v2: u32| {
                    ((v0 as i64 * w0 + v1 as i64 * w1 + v2 as i64 * w2 + half) / area) as u32
                };
                target.blend(
                    row + px as usize,
                    mix(r0, r1, r2),
                    mix(g0, g1, g2),
                    mix(b0, b1, b2),
                    mix(a0_alpha, a1_alpha, a2_alpha),
                );
            }
            w0 += d0;
            w1 += d1;
            w2 += d2;
        }
    }
}

'''
text = replace_between(text, "    let half = area / 2;\n    for py in min_y..max_y {",
                       "// ---- GLYPH_RUN", tri_new, "tri")
# the colour channel alphas were called a0..a2, the edge offsets above use a0..a2 too: rename
text = text.replace("    let (r0, g0, b0, a0) = channels(c0);\n    let (r1, g1, b1, a1) = channels(c1);\n    let (r2, g2, b2, a2) = channels(c2);\n    let flat = c0 == c1 && c1 == c2;\n    let flat_opaque = flat && a0 >= 255;",
                    "    let (r0, g0, b0, a0_alpha) = channels(c0);\n    let (r1, g1, b1, a1_alpha) = channels(c1);\n    let (r2, g2, b2, a2_alpha) = channels(c2);\n    let flat = c0 == c1 && c1 == c2;\n    let flat_opaque = flat && a0_alpha >= 255;", 1)

# ---- tex_tri ---------------------------------------------------------------------------
a = text.index("fn tex_tri<T: RenderTarget>(")
b = text.index("\n}\n", a) + 3
tex_tri_wide = text[a:b].replace("fn tex_tri<", "fn tex_tri_wide<", 1)
tex_tri_new = '''    // inside the triangle 0 <= w <= area: an area below 2^31 lets the weights convert as i32
    // (a branch between the two conversions does not help, the compiler evaluates both)
    if area > i32::MAX as i64 {
        return tex_tri_wide(ui, target, stride, scale, clip, p);
    }
    let inv_area = 1.0f32 / area as f32;
    let to_f32 = |w: i64| w as i32 as f32;
    for py in min_y..max_y {
        let sy = 2 * py as i64 + 1;
        let (mut lo, mut hi) = (min_x as i64, max_x as i64);
        let (a0, d0) = edge_line((bx, by), (cx, cy), sy);
        let (a1, d1) = edge_line((cx, cy), (ax, ay), sy);
        let (a2, d2) = edge_line((ax, ay), (bx, by), sy);
        edge_run(a0, d0, &mut lo, &mut hi);
        edge_run(a1, d1, &mut lo, &mut hi);
        edge_run(a2, d2, &mut lo, &mut hi);
        if lo >= hi {
            continue;
        }
        let (mut w0, mut w1, mut w2) = (a0 + d0 * lo, a1 + d1 * lo, a2 + d2 * lo);
        for px in lo as i32..hi as i32 {
            let (f0, f1, f2) = (
                to_f32(w0) * inv_area,
                to_f32(w1) * inv_area,
                to_f32(w2) * inv_area,
            );
            w0 += d0;
            w1 += d1;
            w2 += d2;
            let u = u0 * f0 + u1 * f1 + u2 * f2;
            let v = v0 * f0 + v1 * f1 + v2 * f2;
            // Nearest is the golden-pinned default path; linear is opt-in
            // per texture (spec::img::FLAG_LINEAR).
            let sample = if view.linear {
                sample_linear(&view, u, v)
            } else {
                let tx = ((u * twf) as i32).clamp(0, tw_max);
                let ty = ((v * thf) as i32).clamp(0, th_max);
                texel(&view, (ty * view.w as i32 + tx) as usize)
            };
            let Some((mut r, mut g, mut b, mut a)) = sample else {
                return;
            };
            if !identity {
                r = (r * mr + 127) / 255;
                g = (g * mg + 127) / 255;
                b = (b * mb + 127) / 255;
                a = (a * ma + 127) / 255;
            }
            target.blend((py * stride + px) as usize, r, g, b, a);
        }
    }
}

'''
text = replace_between(text, "    let inv_area = 1.0f32 / area as f32;\n    for py in min_y..max_y {",
                       "// ---- TEX_QUAD", tex_tri_new, "tex_tri")
text = text.replace("// ---- TEX_QUAD", tex_tri_wide + "\n// ---- TEX_QUAD", 1)

# ---- tex_quad --------------------------------------------------------------------------
tex_quad_new = '''    let inv_w = 1.0f32 / w as f32;
    let inv_h = 1.0f32 / h as f32;
    // x part of the sample per column, y part per row (the same f32 expressions as for a pixel)
    let columns: alloc::vec::Vec<(i32, Option<LinearSample>)> = (c.x0..c.x1)
        .map(|px| {
            let u = u0 + (u1 - u0) * ((px - x) as f32 + 0.5) * inv_w;
            let linear = if view.linear {
                linear_sample_coordinates(view.w, view.h, u, 0.0)
            } else {
                None
            };
            (((u * twf) as i32).clamp(0, tw_max), linear)
        })
        .collect();
    for py in c.y0..c.y1 {
        let v = v0 + (v1 - v0) * ((py - y) as f32 + 0.5) * inv_h;
        let ty = ((v * thf) as i32).clamp(0, th_max);
        let row = if view.linear {
            linear_sample_coordinates(view.w, view.h, 0.0, v)
        } else {
            None
        };
        for (column, px) in (c.x0..c.x1).enumerate() {
            let (tx, ref linear) = columns[column];
            // Nearest is the golden-pinned default path; linear is opt-in
            // per texture (spec::img::FLAG_LINEAR).
            let sample = if view.linear {
                match (linear, &row) {
                    (Some(cx), Some(ry)) => sample_linear_split(&view, cx, ry),
                    _ => None,
                }
            } else {
                texel(&view, (ty * view.w as i32 + tx) as usize)
            };
            let Some((mut r, mut g, mut b, mut a)) = sample else {
                return;
            };
            if !identity {
                // Integer modulate, round-to-nearest (includes alpha).
                r = (r * mr + 127) / 255;
                g = (g * mg + 127) / 255;
                b = (b * mb + 127) / 255;
                a = (a * ma + 127) / 255;
            }
            target.blend((py * stride + px) as usize, r, g, b, a);
        }
    }
}

'''
text = replace_between(text, "    let inv_w = 1.0f32 / w as f32;\n    let inv_h = 1.0f32 / h as f32;\n    for py in c.y0..c.y1 {",
                       "#[cfg(test)]\nmod tests", tex_quad_new, "tex_quad")

split_fn = '''/// sample_linear() with the x part (x0, x1, fx) and the y part (y0, y1, fy) given separately.
#[inline]
fn sample_linear_split(
    view: &TexView,
    cx: &LinearSample,
    ry: &LinearSample,
) -> Option<(u32, u32, u32, u32)> {
    let w = view.w as usize;
    let lerp8 = |a: u32, b: u32, f: u32| (a * (256 - f) + b * f) >> 8;
    if view.psm == spec::psm::PSM_8888 {
        // four texels as words; when they are equal every lerp returns the texel itself
        let px = |y: u32, x: u32| {
            let o = (y as usize * w + x as usize) * 4;
            u32::from_le_bytes([
                view.pixels[o],
                view.pixels[o + 1],
                view.pixels[o + 2],
                view.pixels[o + 3],
            ])
        };
        let (t00, t01) = (px(ry.y0, cx.x0), px(ry.y0, cx.x1));
        let (t10, t11) = (px(ry.y1, cx.x0), px(ry.y1, cx.x1));
        let ch = |t: u32, s: u32| (t >> s) & 255;
        if t00 == t01 && t10 == t11 && t00 == t10 {
            return Some((ch(t00, 0), ch(t00, 8), ch(t00, 16), ch(t00, 24)));
        }
        let mix = |s: u32| {
            lerp8(
                lerp8(ch(t00, s), ch(t01, s), cx.fx),
                lerp8(ch(t10, s), ch(t11, s), cx.fx),
                ry.fy,
            )
        };
        return Some((mix(0), mix(8), mix(16), mix(24)));
    }
    let c00 = texel(view, ry.y0 as usize * w + cx.x0 as usize)?;
    let c01 = texel(view, ry.y0 as usize * w + cx.x1 as usize)?;
    let c10 = texel(view, ry.y1 as usize * w + cx.x0 as usize)?;
    let c11 = texel(view, ry.y1 as usize * w + cx.x1 as usize)?;
    let mix = |c0: u32, c1: u32, c2: u32, c3: u32| {
        lerp8(lerp8(c0, c1, cx.fx), lerp8(c2, c3, cx.fx), ry.fy)
    };
    Some((
        mix(c00.0, c01.0, c10.0, c11.0),
        mix(c00.1, c01.1, c10.1, c11.1),
        mix(c00.2, c01.2, c10.2, c11.2),
        mix(c00.3, c01.3, c10.3, c11.3),
    ))
}

'''
marker = "// ---- TEX_TRI: barycentric textured triangle"
assert text.count(marker) == 1, "TEX_TRI marker"
text = text.replace(marker, split_fn + marker, 1)


# ---- glyph_run -------------------------------------------------------------------------
old = """        let rows = atlas.glyph_rows(gid);
        for py in y0..y1 {
            let sy = coverage_index(py - gy, output_scale, atlas_density, coverage_h);
            let row = &rows[sy * bpr..];
            for px in x0..x1 {
                let cx = coverage_index(px - gx, output_scale, atlas_density, coverage_w);
                let cov = row[cx] as u32;
"""
new = """        let rows = atlas.glyph_rows(gid);
        // the coverage column of a pixel depends on the column only
        let mut columns = [0u16; 64];
        let cached = ((x1 - x0) as usize) <= columns.len();
        if cached {
            for (i, px) in (x0..x1).enumerate() {
                columns[i] =
                    coverage_index(px - gx, output_scale, atlas_density, coverage_w) as u16;
            }
        }
        for py in y0..y1 {
            let sy = coverage_index(py - gy, output_scale, atlas_density, coverage_h);
            let row = &rows[sy * bpr..];
            for px in x0..x1 {
                let cx = if cached {
                    columns[(px - x0) as usize] as usize
                } else {
                    coverage_index(px - gx, output_scale, atlas_density, coverage_w)
                };
                let cov = row[cx] as u32;
"""
if text.count(old) != 1:
    sys.exit("glyph_run loop not found")
text = text.replace(old, new, 1)

if "--with-reference" in sys.argv:
    # host check: the original rasterizer functions as *_ref and a test that compares them
    import re

    original = Path(sys.argv[2]).read_text()
    refs = ""
    for name in ("tri", "tex_tri", "tex_quad", "glyph_run"):
        a = re.search(r"\nfn %s<" % name, original).start() + 1
        b = original.index("\n}\n", a) + 3
        refs += original[a:b].replace("fn %s<" % name, "fn %s_ref<" % name, 1) + "\n"
    text += "\n" + refs + (Path(__file__).parent / "raster_diff_test.rs.in").read_text()

path.write_text(text)
