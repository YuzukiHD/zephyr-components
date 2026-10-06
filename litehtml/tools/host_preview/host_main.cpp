#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <litehtml_zephyr/lh.h>
extern "C" {
}
extern const uint8_t cfb_font_1016[95][20];
static std::string dir;
static std::map<std::string, std::vector<uint16_t>> cache;
static std::map<std::string, std::pair<int,int>> dims;
static const uint16_t *img_cb(void *, const char *src, int *w, int *h) {
  std::string key = src;
  auto it = cache.find(key);
  if (it == cache.end()) {
    std::string path = dir + "/" + key;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) { cache[key] = {}; dims[key] = {0,0}; return nullptr; }
    unsigned char hd[8]; fread(hd, 1, 8, f);
    int iw = hd[4] | hd[5] << 8, ih = hd[6] | hd[7] << 8;
    std::vector<uint16_t> px((size_t)iw * ih); fread(px.data(), 2, px.size(), f); fclose(f);
    cache[key] = px; dims[key] = {iw, ih};
    it = cache.find(key);
  }
  if (it->second.empty()) return nullptr;
  *w = dims[key].first; *h = dims[key].second; return it->second.data();
}
int main(int argc, char **argv) {
  dir = argv[1]; int W = 1024, H = argc > 3 ? atoi(argv[3]) : 1800; int scroll = argc > 4 ? atoi(argv[4]) : 0;
  auto rd = [&](const std::string &p) { std::string s; FILE *f = fopen(p.c_str(), "rb"); if (!f) return s; char b[65536]; size_t n; while ((n = fread(b,1,sizeof b,f))>0) s.append(b,n); fclose(f); return s; };
  static std::string html = rd(dir + "/index.html"); static std::string font = rd(dir + "/font.lhf");
  lh_set_cjk_font(font.data(), font.size()); lh_set_image_loader(img_cb, nullptr);
  static lh_font f = {cfb_font_1016, 32, 126, 10, 16};
  lh_page *p = lh_load(&f, html.c_str(), W, 600);
  printf("height %d, title '%s'\n", lh_content_height(p), lh_title(p));
  std::vector<uint16_t> fb((size_t)W * H);
  lh_draw(p, fb.data(), W, W, H, scroll, 0xffff);
  FILE *o = fopen(argv[2], "wb"); fprintf(o, "P6\n%d %d\n255\n", W, H);
  for (auto v : fb) { unsigned char c[3] = {(unsigned char)((v >> 8) & 0xf8), (unsigned char)((v >> 3) & 0xfc), (unsigned char)((v << 3) & 0xf8)}; fwrite(c,1,3,o);} fclose(o);
  return 0; }
