#!/usr/bin/env python3
"""Speed up the RGB565 renderer on a CPU without an FPU (patches a copy of the crate).

alpha_quad_into_mask computed the texture coordinates of every pixel in f32, which is
soft-float code here. A coordinate depends only on the column (u) or the row (v), so
they are computed once per column and per row with the same expressions, the result
is unchanged.
"""
import sys
from pathlib import Path

path = Path(sys.argv[1])
text = path.read_text()

old = """    for py in physical.y..physical.y + physical.h {
        let v = v0 + (v1 - v0) * ((py as i32 - y * scale_i) as f32 + 0.5) / (h * scale_i) as f32;
        for px in physical.x..physical.x + physical.w {
            let u =
                u0 + (u1 - u0) * ((px as i32 - x * scale_i) as f32 + 0.5) / (w * scale_i) as f32;
            let alpha = (sample_alpha(view, u, v) as u32 * global_alpha as u32 + 127) / 255;
"""
new = """    // x part of the sample per column, y part per row: the same f32 expressions as
    // linear_sample_coordinates() evaluates for a pixel, just not once per pixel
    let columns: alloc::vec::Vec<(f32, i32, Option<pocketjs_core::raster::LinearSample>)> =
        (physical.x..physical.x + physical.w)
            .map(|px| {
                let u = u0
                    + (u1 - u0) * ((px as i32 - x * scale_i) as f32 + 0.5) / (w * scale_i) as f32;
                let linear = if view.linear {
                    linear_sample_coordinates(view.w, view.h, u, 0.0)
                } else {
                    None
                };
                (u, (u * view.w as f32) as i32, linear)
            })
            .collect();
    let lerp = |a: u32, b: u32, f: u32| (a * (256 - f) + b * f) >> 8;
    for py in physical.y..physical.y + physical.h {
        let v = v0 + (v1 - v0) * ((py as i32 - y * scale_i) as f32 + 0.5) / (h * scale_i) as f32;
        let texel_y = (v * view.h as f32) as i32;
        let row = if view.linear {
            linear_sample_coordinates(view.w, view.h, 0.0, v)
        } else {
            None
        };
        for (column, px) in (physical.x..physical.x + physical.w).enumerate() {
            let (_, texel_x, ref linear) = columns[column];
            let sample = if view.linear {
                match (linear, &row) {
                    (Some(c), Some(r)) => {
                        let top = lerp(
                            texel_alpha(view, c.x0 as i32, r.y0 as i32),
                            texel_alpha(view, c.x1 as i32, r.y0 as i32),
                            c.fx,
                        );
                        let bottom = lerp(
                            texel_alpha(view, c.x0 as i32, r.y1 as i32),
                            texel_alpha(view, c.x1 as i32, r.y1 as i32),
                            c.fx,
                        );
                        lerp(top, bottom, r.fy) as u8
                    }
                    _ => 0,
                }
            } else {
                texel_alpha(view, texel_x, texel_y) as u8
            };
            let alpha = (sample as u32 * global_alpha as u32 + 127) / 255;
"""
if text.count(old) != 1:
    sys.exit("alpha_quad_into_mask not found in the renderer")
path.write_text(text.replace(old, new))
