/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * Display and keyboard of Bochs on the board: the guest picture is drawn into an RGB565 buffer
 * (text mode with the VGA font, graphics mode from the palette indexed tiles) and put on the
 * scaling video plane of the display engine; the keyboard is the serial console.
 * Takes the place of the "nogui" display library.
 */

#define BX_PLUGGABLE

#include "bochs.h"
#include "gui.h"
#include "iodev/iodev.h"
#include "plugin.h"
#include "param_names.h"

#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/drivers/uart.h>

class bx_nogui_gui_c : public bx_gui_c {
public:
  bx_nogui_gui_c(void) {}
  DECLARE_GUI_VIRTUAL_METHODS()
  virtual void set_font(bool lg) {}
  virtual void draw_char(Bit8u ch, Bit8u fc, Bit8u bc, Bit16u xc, Bit16u yc,
                         Bit8u fw, Bit8u fh, Bit8u fx, Bit8u fy,
                         bool gfxcharw9, Bit8u cs, Bit8u ce, bool curs, bool font2);
};

static bx_nogui_gui_c *theGui = NULL;
IMPLEMENT_GUI_PLUGIN_CODE(nogui)

#define LOG_THIS theGui->

#define MAX_W 720
#define MAX_H 480

static const struct device *disp;
static const struct device *console;
static Bit16u *pic;           /* the picture the display engine scans out */
static unsigned pic_w, pic_h; /* guest resolution */
static Bit16u pal[256];
static bool dirty;

#ifdef CONFIG_BOCHS_TEXT_MIRROR
/* the text screen as characters, mirrored to the serial console while the guest is in text mode */
#define TXT_COLS 100
#define TXT_ROWS 60
static char txt[TXT_ROWS][TXT_COLS + 1];
static char txt_sent[TXT_ROWS][TXT_COLS + 1];
static bool txt_active;

static void txt_flush(unsigned rows)
{
  for (unsigned r = 0; r < rows && r < TXT_ROWS; r++) {
    if (memcmp(txt[r], txt_sent[r], sizeof(txt[r])) == 0) {
      continue;
    }
    memcpy(txt_sent[r], txt[r], sizeof(txt[r]));
    int len = TXT_COLS;
    while (len > 0 && txt[r][len - 1] == ' ') {
      len--;
    }
    if (len > 0) {
      printk("|%02u|%.*s\n", r, len, txt[r]);
    }
  }
}
#endif

static Bit16u rgb565(Bit8u r, Bit8u g, Bit8u b)
{
  return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

void bx_nogui_gui_c::specific_init(int argc, char **argv, unsigned headerbar_y)
{
  put("ZGUI");
  UNUSED(headerbar_y);
  UNUSED(argc);
  UNUSED(argv);

  disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
  if (!device_is_ready(disp)) {
    BX_PANIC(("the display is not ready"));
  }
  pic = (Bit16u *)aligned_alloc(64, MAX_W * MAX_H * sizeof(Bit16u));
  if (pic == NULL) {
    BX_PANIC(("no memory for the picture"));
  }
  memset(pic, 0, MAX_W * MAX_H * sizeof(Bit16u));
  pic_w = 640;
  pic_h = 400;
  for (int i = 0; i < 256; i++) {
    pal[i] = rgb565(i, i, i);
  }
  /* the base class calls draw_char() for the text cells */
  new_text_api = 1;
  /* the tile size of the VGA code */
  x_tilesize = 16;
  y_tilesize = 24;
}

/* serial console to scancodes: letters, digits, punctuation, enter, backspace, tab, escape */
static void send_key(Bit32u key, bool shift)
{
  if (shift) {
    DEV_kbd_gen_scancode(BX_KEY_SHIFT_L);
  }
  DEV_kbd_gen_scancode(key);
  DEV_kbd_gen_scancode(key | BX_KEY_RELEASED);
  if (shift) {
    DEV_kbd_gen_scancode(BX_KEY_SHIFT_L | BX_KEY_RELEASED);
  }
}

static void ascii_key(int c)
{
  static const char shifted[] = ")!@#$%^&*(";

  if (c >= 'a' && c <= 'z') {
    send_key(BX_KEY_A + (c - 'a'), false);
  } else if (c >= 'A' && c <= 'Z') {
    send_key(BX_KEY_A + (c - 'A'), true);
  } else if (c >= '0' && c <= '9') {
    send_key(BX_KEY_0 + (c - '0'), false);
  } else if (c != 0 && strchr(shifted, c) != NULL) {
    send_key(BX_KEY_0 + (strchr(shifted, c) - shifted), true);
  } else {
    switch (c) {
    case ' ':  send_key(BX_KEY_SPACE, false); break;
    case '\r':
    case '\n': send_key(BX_KEY_ENTER, false); break;
    case 0x7f:
    case 8:    send_key(BX_KEY_BACKSPACE, false); break;
    case '\t': send_key(BX_KEY_TAB, false); break;
    case 27:   send_key(BX_KEY_ESC, false); break;
    case '-':  send_key(BX_KEY_MINUS, false); break;
    case '=':  send_key(BX_KEY_EQUALS, false); break;
    case ',':  send_key(BX_KEY_COMMA, false); break;
    case '.':  send_key(BX_KEY_PERIOD, false); break;
    case '/':  send_key(BX_KEY_SLASH, false); break;
    case ';':  send_key(BX_KEY_SEMICOLON, false); break;
    case '\'': send_key(BX_KEY_SINGLE_QUOTE, false); break;
    case '\\': send_key(BX_KEY_BACKSLASH, false); break;
    case '[':  send_key(BX_KEY_LEFT_BRACKET, false); break;
    case '`':  send_key(BX_KEY_GRAVE, false); break;
    case '_':  send_key(BX_KEY_MINUS, true); break;
    case '+':  send_key(BX_KEY_EQUALS, true); break;
    case ':':  send_key(BX_KEY_SEMICOLON, true); break;
    case '?':  send_key(BX_KEY_SLASH, true); break;
    case '"':  send_key(BX_KEY_SINGLE_QUOTE, true); break;
    case '<':  send_key(BX_KEY_COMMA, true); break;
    case '>':  send_key(BX_KEY_PERIOD, true); break;
    default: break;
    }
  }
}

void bx_nogui_gui_c::handle_events(void)
{
  /*
   * The serial console delivers a whole line at once, the guest's keyboard queue holds a few
   * keys: they wait in a ring and go in one at a time.
   */
  static unsigned char ring[256];
  static unsigned head, tail;
  static int64_t last_ms;
  unsigned char c;
  int64_t now;

  while (uart_poll_in(console, &c) == 0) {
    if (((head + 1) & 255) != tail) {
      ring[head] = c;
      head = (head + 1) & 255;
    }
  }
  now = k_uptime_get();
  if (head != tail && now - last_ms >= 60) {
    last_ms = now;
    ascii_key(ring[tail]);
    tail = (tail + 1) & 255;
  }
}

void bx_nogui_gui_c::flush(void)
{
  struct display_sunxi_rgb img;

#ifdef CONFIG_BOCHS_TEXT_MIRROR
  if (guest_textmode && txt_active) {
    txt_flush(guest_yres / guest_fheight);
  }
#endif

  if (!dirty || pic == NULL) {
    return;
  }
  dirty = false;
  sys_cache_data_flush_range(pic, pic_w * pic_h * sizeof(Bit16u));
  img.data = pic;
  img.width = pic_w;
  img.height = pic_h;
  img.stride = pic_w * sizeof(Bit16u);
  img.xrgb8888 = false;
  img.nonblock = true;
  display_sunxi_show_rgb(disp, &img);
}

void bx_nogui_gui_c::clear_screen(void)
{
  memset(pic, 0, MAX_W * MAX_H * sizeof(Bit16u));
  dirty = true;
}

void bx_nogui_gui_c::draw_char(Bit8u ch, Bit8u fc, Bit8u bc, Bit16u xc, Bit16u yc,
                                Bit8u fw, Bit8u fh, Bit8u fx, Bit8u fy,
                                bool gfxcharw9, Bit8u cs, Bit8u ce, bool curs, bool font2)
{
  const Bit8u *glyph = &vga_charmap[font2 ? 1 : 0][ch * 32];

#ifdef CONFIG_BOCHS_TEXT_MIRROR
  if (guest_fwidth > 0 && guest_fheight > 0) {
    unsigned c = xc / guest_fwidth, r = yc / guest_fheight;

    if (c < TXT_COLS && r < TXT_ROWS) {
      txt[r][c] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : (ch == 0 ? ' ' : '.');
      txt_active = true;
    }
  }
#endif

  for (unsigned row = 0; row < fh; row++) {
    unsigned y = yc + row;
    Bit8u bits = glyph[row + fy];
    bool inv = curs && row >= cs && row <= ce;

    if (y >= pic_h) {
      break;
    }
    for (unsigned col = 0; col < fw; col++) {
      unsigned x = xc + col, idx = col + fx;
      bool on;

      if (x >= pic_w) {
        break;
      }
      if (idx < 8) {
        on = (bits >> (7 - idx)) & 1;
      } else {
        on = gfxcharw9 && ch >= 0xc0 && ch <= 0xdf && (bits & 1);
      }
      if (inv) {
        on = !on;
      }
      pic[y * pic_w + x] = pal[on ? fc : bc];
    }
  }
}

void bx_nogui_gui_c::text_update(Bit8u *old_text, Bit8u *new_text,
                                  unsigned long cursor_x, unsigned long cursor_y,
                                  bx_vga_tminfo_t *tm_info)
{
  /* not used: new_text_api makes the base class call draw_char() */
}

bool bx_nogui_gui_c::palette_change(Bit8u index, Bit8u red, Bit8u green, Bit8u blue)
{
  pal[index] = rgb565(red, green, blue);
  dirty = true;
  return 1;
}

void bx_nogui_gui_c::graphics_tile_update(Bit8u *tile, unsigned x0, unsigned y0)
{
  for (unsigned y = 0; y < y_tilesize && y0 + y < pic_h; y++) {
    for (unsigned x = 0; x < x_tilesize && x0 + x < pic_w; x++) {
      pic[(y0 + y) * pic_w + x0 + x] = pal[tile[y * x_tilesize + x]];
    }
  }
  dirty = true;
}

void bx_nogui_gui_c::dimension_update(unsigned x, unsigned y, unsigned fheight, unsigned fwidth, unsigned bpp)
{
#ifdef CONFIG_BOCHS_TEXT_MIRROR
  memset(txt, ' ', sizeof(txt));
  memset(txt_sent, 0, sizeof(txt_sent));
  for (unsigned r = 0; r < TXT_ROWS; r++) {
    txt[r][TXT_COLS] = 0;
    txt_sent[r][TXT_COLS] = 0;
  }
  txt_active = false;
#endif
  guest_textmode = (fheight > 0);
  if (guest_textmode) {
    guest_fwidth = fwidth;
    guest_fheight = fheight;
  }
  guest_xres = x;
  guest_yres = y;
  guest_bpp = bpp;
  if (x > MAX_W || y > MAX_H) {
    BX_PANIC(("guest resolution %ux%u is too big", x, y));
  }
  pic_w = x;
  pic_h = y;
  memset(pic, 0, MAX_W * MAX_H * sizeof(Bit16u));
  dirty = true;
}

unsigned bx_nogui_gui_c::create_bitmap(const unsigned char *bmap, unsigned xdim, unsigned ydim)
{
  return 0;
}

unsigned bx_nogui_gui_c::headerbar_bitmap(unsigned bmap_id, unsigned alignment, void (*f)(void))
{
  return 0;
}

void bx_nogui_gui_c::show_headerbar(void)
{
}

void bx_nogui_gui_c::replace_bitmap(unsigned hbar_id, unsigned bmap_id)
{
}

int bx_nogui_gui_c::get_clipboard_text(Bit8u **bytes, Bit32s *nbytes)
{
  return 0;
}

int bx_nogui_gui_c::set_clipboard_text(char *text_snapshot, Bit32u len)
{
  return 0;
}

void bx_nogui_gui_c::mouse_enabled_changed_specific(bool val)
{
}

void bx_nogui_gui_c::exit(void)
{
  /* the last picture stays on the panel */
  dirty = true;
  flush();
}
