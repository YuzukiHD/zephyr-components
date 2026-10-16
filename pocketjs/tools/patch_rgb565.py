#!/usr/bin/env python3
"""Speed up the RGB565 renderer (patches a copy of the crates).

patch_rgb565.py <backend lib.rs>

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

# A rectangle with a fixed alpha has the same coverage everywhere: let the host blend it as a
# solid colour (an empty mask means that) before a mask of the rectangle is built and filled
old = """        } else if rect.area() >= self.config.min_blend_pixels {
            let mask = self.mask_mut();
            fill_mask_rect(mask, width, rect, a as u8);
"""
new = """        } else if rect.area() >= self.config.min_blend_pixels {
            if ppa.blend_a8_rgb565(
                destination,
                width,
                height,
                &[],
                rect,
                [r as u8, g as u8, b as u8],
                a as u8,
            ) {
                stats.ppa_blends += 1;
                return true;
            }
            let mask = self.mask_mut();
            fill_mask_rect(mask, width, rect, a as u8);
"""
text = path.read_text()
if text.count(old) != 1:
    sys.exit("try_rect blend branch not found in the renderer")
path.write_text(text.replace(old, new))

# The classification of a palette (T8) texture was never cached: every draw of an icon scanned all
# its texels (4096 for a 64x64 icon, a palette lookup each). An in-place update of such a texture
# (update_texture_t8) and a free bump the raster revision, which empties the cache at the start of
# every render, so the result of the handle can be kept like the others.
old = """        // T8 video planes can replace their palette and indices in place, so
        // their classification is intentionally never cached.
        if view.psm == spec::psm::PSM_T8 {
            return is_white_alpha_texture(view);
        }
"""
text = path.read_text()
if text.count(old) != 1:
    sys.exit("T8 classification bypass not found in the renderer")
path.write_text(text.replace(old, "", 1))

# The fake host of the tests does not know the empty mask (see above): it answers false, so the
# renderer takes the path with a mask and the tests keep checking that one.
old = """            global_alpha: u8,
        ) -> bool {
            self.blends += 1;
            self.last_surface_width = width;"""
new = """            global_alpha: u8,
        ) -> bool {
            if mask.is_empty() {
                return false;
            }
            self.blends += 1;
            self.last_surface_width = width;"""
text = path.read_text()
if text.count(old) != 1:
    sys.exit("test host blend method not found in the renderer")
path.write_text(text.replace(old, new, 1))

