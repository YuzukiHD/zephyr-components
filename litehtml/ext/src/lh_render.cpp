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
		int n = 0;

		for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
			if ((*p & 0xc0) != 0x80) {
				n++;
			}
		}
		return n * f->cell_w;
	}

	void draw_text(litehtml::uint_ptr, const char *text, litehtml::uint_ptr h,
		       litehtml::web_color color, const litehtml::position &pos) override
	{
		const auto *f = reinterpret_cast<font_data *>(h);
		int x = pos.x;

		for (const unsigned char *p = (const unsigned char *)text; *p;) {
			unsigned char c = *p++;

			if (c >= 0x80) {
				/* one glyph for a whole UTF-8 sequence */
				while ((*p & 0xc0) == 0x80) {
					p++;
				}
				c = '?';
			}
			draw_glyph(f, c, x, pos.y, color);
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
	void get_image_size(const char *, const char *, litehtml::size &sz) override
	{
		sz.width = 0;
		sz.height = 0;
	}

	void draw_background(litehtml::uint_ptr, const std::vector<litehtml::background_paint> &bg) override
	{
		/* only the last (farthest) layer has the colour */
		const litehtml::background_paint &p = bg.back();

		if (p.color.alpha != 0) {
			const litehtml::position &b = p.clip_box;

			fill({b.x, b.y, b.x + b.width, b.y + b.height}, p.color);
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

void lh_free(struct lh_page *page)
{
	delete page;
}

} // extern "C"
