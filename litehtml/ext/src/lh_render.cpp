/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * The drawing side of litehtml for a plain RGB565 frame: a document container that fills
 * rectangles and draws text with a scaled bitmap font. See litehtml_zephyr/lh.h.
 */

#include <litehtml.h>
#include <litehtml_zephyr/lh.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

/* the image loader and the extra glyphs, set by the application */
lh_image_fn g_image_fn;
void *g_image_user;
const uint8_t *g_cjk;
uint32_t g_cjk_count, g_cjk_cell;

const uint32_t *cjk_codes()
{
	return reinterpret_cast<const uint32_t *>(g_cjk + 16);
}

/* the alpha of the glyph of a code point, NULL when the font has none */
const uint8_t *cjk_glyph(uint32_t cp)
{
	uint32_t lo = 0, hi = g_cjk_count;

	if (g_cjk == nullptr) {
		return nullptr;
	}
	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;
		uint32_t c = cjk_codes()[mid];

		if (c == cp) {
			return g_cjk + 16 + (size_t)g_cjk_count * 4 + (size_t)mid * g_cjk_cell * g_cjk_cell;
		}
		if (c < cp) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return nullptr;
}

/* the next code point of a UTF-8 text, p moves on */
uint32_t next_cp(const unsigned char *&p)
{
	uint32_t c = *p++;

	if (c < 0x80) {
		return c;
	}
	int n = (c >= 0xf0) ? 3 : (c >= 0xe0) ? 2 : (c >= 0xc0) ? 1 : 0;

	c &= (0x3f >> n);
	while (n-- > 0 && (*p & 0xc0) == 0x80) {
		c = (c << 6) | (*p++ & 0x3f);
	}
	return c;
}

struct font_data {
	int size;
	int weight;
	bool italic;
	unsigned int decoration;
	int cell_w; /* advance of one character */
};

struct rect {
	int x0, y0, x1, y1; /* x1, y1 exclusive */
};

rect intersect(const rect &a, const rect &b)
{
	rect r = {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1),
		  std::min(a.y1, b.y1)};

	if (r.x1 < r.x0) {
		r.x1 = r.x0;
	}
	if (r.y1 < r.y0) {
		r.y1 = r.y0;
	}
	return r;
}

inline uint16_t pack565(unsigned r, unsigned g, unsigned b)
{
	return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

class canvas_container : public litehtml::document_container {
public:
	explicit canvas_container(const lh_font *font, int w, int h) : font_(font), vw_(w), vh_(h) {}

	/* the frame of the next draw; litehtml already hands out frame coordinates */
	void begin(uint16_t *fb, int stride, int w, int h)
	{
		fb_ = fb;
		stride_ = stride;
		fw_ = w;
		fh_ = h;
		scroll_ = 0;
		clips_.clear();
		clips_.push_back({0, 0, w, h});
	}

	const std::string &title() const { return title_; }

	litehtml::uint_ptr create_font(const char *, int size, int weight,
				       litehtml::font_style italic, unsigned int decoration,
				       litehtml::font_metrics *fm) override
	{
		auto *f = new font_data;

		f->size = std::max(size, 6);
		f->weight = weight;
		f->italic = italic == litehtml::font_style_italic;
		f->decoration = decoration;
		f->cell_w = std::max(1, (f->size * font_->cell_w + font_->cell_h / 2) / font_->cell_h);
		if (weight >= 600) {
			f->cell_w++;
		}
		if (fm) {
			fm->height = f->size;
			fm->ascent = f->size * 13 / 16;
			fm->descent = f->size - fm->ascent;
			fm->x_height = f->size / 2;
			fm->draw_spaces = (decoration != 0);
		}
		return reinterpret_cast<litehtml::uint_ptr>(f);
	}

	void delete_font(litehtml::uint_ptr h) override { delete reinterpret_cast<font_data *>(h); }

	int text_width(const char *text, litehtml::uint_ptr h) override
	{
		const auto *f = reinterpret_cast<font_data *>(h);
		int w = 0;

		for (const unsigned char *p = (const unsigned char *)text; *p;) {
			uint32_t cp = next_cp(p);

			w += (cp >= 0x80 && cjk_glyph(cp) != nullptr) ? f->size : f->cell_w;
		}
		return w;
	}

	void draw_text(litehtml::uint_ptr, const char *text, litehtml::uint_ptr h,
		       litehtml::web_color color, const litehtml::position &pos) override
	{
		const auto *f = reinterpret_cast<font_data *>(h);
		int x = pos.x;

		for (const unsigned char *p = (const unsigned char *)text; *p;) {
			uint32_t cp = next_cp(p);
			const uint8_t *g = cp >= 0x80 ? cjk_glyph(cp) : nullptr;

			if (g != nullptr) {
				draw_alpha_glyph(f, g, x, pos.y, color);
				x += f->size;
				continue;
			}
			draw_glyph(f, cp >= 0x80 ? '?' : (unsigned char)cp, x, pos.y, color);
			x += f->cell_w;
		}
		if (f->decoration & litehtml::font_decoration_underline) {
			fill({pos.x, pos.y + f->size - 1, x, pos.y + f->size}, color);
		}
		if (f->decoration & litehtml::font_decoration_linethrough) {
			fill({pos.x, pos.y + f->size / 2, x, pos.y + f->size / 2 + 1}, color);
		}
		if (f->decoration & litehtml::font_decoration_overline) {
			fill({pos.x, pos.y, x, pos.y + 1}, color);
		}
	}

	int pt_to_px(int pt) const override { return pt * 96 / 72; }
	int get_default_font_size() const override { return 16; }
	const char *get_default_font_name() const override { return "sans"; }

	void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker &m) override
	{
		const litehtml::position &p = m.pos;
		int d = std::max(4, std::min(p.width, p.height) / 2);
		int cx = p.x + (p.width - d) / 2, cy = p.y + (p.height - d) / 2;

		switch (m.marker_type) {
		case litehtml::list_style_type_circle:
			fill({cx, cy, cx + d, cy + 1}, m.color);
			fill({cx, cy + d - 1, cx + d, cy + d}, m.color);
			fill({cx, cy, cx + 1, cy + d}, m.color);
			fill({cx + d - 1, cy, cx + d, cy + d}, m.color);
			break;
		case litehtml::list_style_type_disc:
		case litehtml::list_style_type_square:
			fill({cx, cy, cx + d, cy + d}, m.color);
			break;
		case litehtml::list_style_type_none:
			break;
		default: {
			/* numbered: "n." right aligned in the marker box */
			char buf[16];
			std::snprintf(buf, sizeof(buf), "%d.", m.index);
			litehtml::position pos = p;
			pos.x = p.x + p.width - text_width(buf, m.font);
			draw_text(hdc, buf, m.font, m.color, pos);
			break;
		}
		}
	}

	/* images are not supported: the elements get no size */
	void load_image(const char *, const char *, bool) override {}
	void get_image_size(const char *src, const char *, litehtml::size &sz) override
	{
		int w = 0, h = 0;

		if (g_image_fn != nullptr && g_image_fn(g_image_user, src, &w, &h) != nullptr) {
			sz.width = w;
			sz.height = h;
		} else {
			sz.width = 0;
			sz.height = 0;
		}
	}

	void draw_background(litehtml::uint_ptr, const std::vector<litehtml::background_paint> &bg) override
	{
		/* only the last (farthest) layer has the colour */
		const litehtml::background_paint &p = bg.back();

		if (p.color.alpha != 0) {
			const litehtml::position &b = p.clip_box;

			fill({b.x, b.y, b.x + b.width, b.y + b.height}, p.color);
		}
		/* the images, farthest first */
		for (int i = (int)bg.size() - 1; i >= 0; i--) {
			if (!bg[i].image.empty()) {
				draw_image(bg[i]);
			}
		}
	}

	void draw_borders(litehtml::uint_ptr, const litehtml::borders &b,
			  const litehtml::position &r, bool) override
	{
		side(b.left, {r.x, r.y, r.x + b.left.width, r.y + r.height});
		side(b.right, {r.x + r.width - b.right.width, r.y, r.x + r.width, r.y + r.height});
		side(b.top, {r.x, r.y, r.x + r.width, r.y + b.top.width});
		side(b.bottom, {r.x, r.y + r.height - b.bottom.width, r.x + r.width, r.y + r.height});
	}

	void set_caption(const char *caption) override { title_ = caption ? caption : ""; }
	void set_base_url(const char *) override {}
	void link(const std::shared_ptr<litehtml::document> &, const litehtml::element::ptr &) override {}
	void on_anchor_click(const char *, const litehtml::element::ptr &) override {}
	void set_cursor(const char *) override {}

	void transform_text(litehtml::string &text, litehtml::text_transform tt) override
	{
		if (tt == litehtml::text_transform_uppercase) {
			for (auto &c : text) {
				if (c >= 'a' && c <= 'z') {
					c -= 32;
				}
			}
		} else if (tt == litehtml::text_transform_lowercase) {
			for (auto &c : text) {
				if (c >= 'A' && c <= 'Z') {
					c += 32;
				}
			}
		} else if (tt == litehtml::text_transform_capitalize) {
			bool start = true;

			for (auto &c : text) {
				if (start && c >= 'a' && c <= 'z') {
					c -= 32;
				}
				start = (c == ' ');
			}
		}
	}

	void import_css(litehtml::string &, const litehtml::string &, litehtml::string &) override {}

	void set_clip(const litehtml::position &pos, const litehtml::border_radiuses &) override
	{
		rect r = {pos.x, pos.y, pos.x + pos.width, pos.y + pos.height};

		clips_.push_back(intersect(clips_.back(), to_frame(r)));
	}

	void del_clip() override
	{
		if (clips_.size() > 1) {
			clips_.pop_back();
		}
	}

	void get_client_rect(litehtml::position &client) const override
	{
		client = litehtml::position(0, 0, vw_, vh_);
	}

	litehtml::element::ptr create_element(const char *, const litehtml::string_map &,
					      const std::shared_ptr<litehtml::document> &) override
	{
		return nullptr;
	}

	void get_media_features(litehtml::media_features &media) const override
	{
		media.type = litehtml::media_type_screen;
		media.width = vw_;
		media.height = vh_;
		media.device_width = vw_;
		media.device_height = vh_;
		media.color = 8;
		media.monochrome = 0;
		media.color_index = 256;
		media.resolution = 96;
	}

	void get_language(litehtml::string &language, litehtml::string &culture) const override
	{
		language = "en";
		culture = "";
	}

private:
	rect to_frame(const rect &r) const { return {r.x0, r.y0 - scroll_, r.x1, r.y1 - scroll_}; }

	void fill(const rect &page_rect, litehtml::web_color c)
	{
		rect r = intersect(to_frame(page_rect), clips_.back());
		unsigned a = c.alpha;

		if (r.x1 <= r.x0 || r.y1 <= r.y0 || a == 0) {
			return;
		}
		uint16_t solid = pack565(c.red, c.green, c.blue);

		for (int y = r.y0; y < r.y1; y++) {
			uint16_t *row = fb_ + (size_t)y * stride_;

			if (a == 255) {
				std::fill(row + r.x0, row + r.x1, solid);
				continue;
			}
			for (int x = r.x0; x < r.x1; x++) {
				row[x] = blend(row[x], c, a);
			}
		}
	}

	static uint16_t blend(uint16_t d, litehtml::web_color c, unsigned a)
	{
		unsigned dr = (d >> 8) & 0xf8, dg = (d >> 3) & 0xfc, db = (d << 3) & 0xf8;

		return pack565((c.red * a + dr * (255 - a)) / 255, (c.green * a + dg * (255 - a)) / 255,
			       (c.blue * a + db * (255 - a)) / 255);
	}

	void draw_alpha_glyph(const font_data *f, const uint8_t *g, int x, int y, litehtml::web_color c)
	{
		int cell = (int)g_cjk_cell;

		for (int dy = 0; dy < f->size; dy++) {
			int fy = y - scroll_ + dy;
			int sy = dy * cell / f->size;

			if (fy < clips_.back().y0 || fy >= clips_.back().y1) {
				continue;
			}
			for (int dx = 0; dx < f->size; dx++) {
				unsigned a = g[sy * cell + dx * cell / f->size];

				if (a == 0) {
					continue;
				}
				litehtml::web_color k = c;

				k.alpha = (uint8_t)(c.alpha * a / 255);
				put(x + dx, fy, k);
				if (f->weight >= 600) {
					put(x + dx + 1, fy, k);
				}
			}
		}
	}

	/* an image, scaled to image_size, at (position_x, position_y), repeated as the style says */
	void draw_image(const litehtml::background_paint &p)
	{
		int iw = 0, ih = 0;
		const uint16_t *px;

		if (g_image_fn == nullptr || p.image_size.width <= 0 || p.image_size.height <= 0) {
			return;
		}
		px = g_image_fn(g_image_user, p.image.c_str(), &iw, &ih);
		if (px == nullptr || iw <= 0 || ih <= 0) {
			return;
		}
		const litehtml::position &b = p.clip_box;
		rect r = intersect(to_frame({b.x, b.y, b.x + b.width, b.y + b.height}), clips_.back());
		int dw = p.image_size.width, dh = p.image_size.height;
		int ox = p.position_x, oy = p.position_y - scroll_;
		bool rx = p.repeat == litehtml::background_repeat_repeat ||
			  p.repeat == litehtml::background_repeat_repeat_x;
		bool ry = p.repeat == litehtml::background_repeat_repeat ||
			  p.repeat == litehtml::background_repeat_repeat_y;

		if (r.x1 <= r.x0 || r.y1 <= r.y0) {
			return;
		}
		/* the column of the source for every column of the frame */
		xmap_.assign(r.x1 - r.x0, -1);
		for (int x = r.x0; x < r.x1; x++) {
			int tx = x - ox;

			if (rx) {
				tx %= dw;
				if (tx < 0) {
					tx += dw;
				}
			} else if (tx < 0 || tx >= dw) {
				continue;
			}
			xmap_[x - r.x0] = tx * iw / dw;
		}
		for (int y = r.y0; y < r.y1; y++) {
			int ty = y - oy;

			if (ry) {
				ty %= dh;
				if (ty < 0) {
					ty += dh;
				}
			} else if (ty < 0 || ty >= dh) {
				continue;
			}
			const uint16_t *src = px + (size_t)(ty * ih / dh) * iw;
			uint16_t *dst = fb_ + (size_t)y * stride_;

			for (int x = r.x0; x < r.x1; x++) {
				int sx = xmap_[x - r.x0];

				if (sx >= 0) {
					dst[x] = src[sx];
				}
			}
		}
	}

	void side(const litehtml::border &b, const rect &r)
	{
		if (b.width > 0 && b.style != litehtml::border_style_none &&
		    b.style != litehtml::border_style_hidden) {
			fill(r, b.color);
		}
	}

	void put(int x, int y, litehtml::web_color c)
	{
		const rect &k = clips_.back();

		if (x < k.x0 || x >= k.x1 || y < k.y0 || y >= k.y1) {
			return;
		}
		uint16_t *p = fb_ + (size_t)y * stride_ + x;

		*p = c.alpha == 255 ? pack565(c.red, c.green, c.blue) : blend(*p, c, c.alpha);
	}

	/* the glyph scaled to the font size with the nearest pixel, bold smears one column */
	void draw_glyph(const font_data *f, unsigned char ch, int x, int y, litehtml::web_color c)
	{
		if (ch < font_->first || ch > font_->last || ch == ' ') {
			return;
		}
		const uint8_t *g = font_->glyphs[ch - font_->first];
		int gw = font_->cell_w, gh = font_->cell_h;
		int dw = f->cell_w - (f->weight >= 600 ? 1 : 0);

		for (int dy = 0; dy < f->size; dy++) {
			int sy = dy * gh / f->size;
			int fy = y - scroll_ + dy;
			int shear = f->italic ? (f->size - dy) / 4 : 0;

			if (fy < clips_.back().y0 || fy >= clips_.back().y1) {
				continue;
			}
			for (int dx = 0; dx < dw; dx++) {
				int sx = dx * gw / dw;
				unsigned bits = g[sx * 2] | (g[sx * 2 + 1] << 8);

				if (!((bits >> sy) & 1)) {
					continue;
				}
				put(x + dx + shear, fy, c);
				if (f->weight >= 600) {
					put(x + dx + shear + 1, fy, c);
				}
			}
		}
	}

	const lh_font *font_;
	int vw_, vh_;
	uint16_t *fb_ = nullptr;
	int stride_ = 0, fw_ = 0, fh_ = 0, scroll_ = 0;
	std::vector<rect> clips_;
	std::vector<int> xmap_;
	std::string title_;
};

} // namespace

struct lh_page {
	canvas_container container;
	litehtml::document::ptr doc;
	int content_h = 0;

	lh_page(const lh_font *font, int w, int h) : container(font, w, h) {}
};

extern "C" {

void lh_set_image_loader(lh_image_fn fn, void *user)
{
	g_image_fn = fn;
	g_image_user = user;
}

void lh_set_cjk_font(const void *data, size_t size)
{
	const uint8_t *d = static_cast<const uint8_t *>(data);

	g_cjk = nullptr;
	if (data == nullptr || size < 16 || memcmp(d, "LHF1", 4) != 0) {
		return;
	}
	memcpy(&g_cjk_count, d + 4, 4);
	memcpy(&g_cjk_cell, d + 8, 4);
	if (g_cjk_cell == 0 || size < 16 + (size_t)g_cjk_count * (4 + g_cjk_cell * g_cjk_cell)) {
		return;
	}
	g_cjk = d;
}

struct lh_page *lh_load(const struct lh_font *font, const char *html, int width, int height)
{
	auto *p = new lh_page(font, width, height);

	p->doc = litehtml::document::createFromString(html, &p->container);
	if (!p->doc) {
		delete p;
		return nullptr;
	}
	p->doc->render(width);
	p->content_h = p->doc->height();
	return p;
}

int lh_content_height(const struct lh_page *page)
{
	return page->content_h;
}

const char *lh_title(const struct lh_page *page)
{
	return page->container.title().c_str();
}

void lh_draw(struct lh_page *page, uint16_t *fb, int stride, int w, int h, int scroll_y, uint16_t bg)
{
	for (int y = 0; y < h; y++) {
		std::fill(fb + (size_t)y * stride, fb + (size_t)y * stride + w, bg);
	}
	page->container.begin(fb, stride, w, h);

	litehtml::position clip(0, 0, w, h);

	page->doc->draw(0, 0, -scroll_y, &clip);
}

void lh_draw_over(struct lh_page *page, uint16_t *fb, int stride, int w, int h, int scroll_y)
{
	page->container.begin(fb, stride, w, h);

	litehtml::position clip(0, 0, w, h);

	page->doc->draw(0, 0, -scroll_y, &clip);
}

void lh_free(struct lh_page *page)
{
	delete page;
}

} // extern "C"
