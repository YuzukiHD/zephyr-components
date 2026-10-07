/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * Devices of Bochs that are not built: the entry points that devices.cc calls.
 */

#include "bochs.h"
#include "plugin.h"
#include "cpu/cpu.h"
#include "pc_system.h"
#include "memory/memory-bochs.h"
#include "cpu/softfloat3e/include/softfloat.h"
#include <zephyr/kernel.h>

/* no network: the serial port code of Bochs links against these, every call fails */
#include <sys/socket.h>
#include <netdb.h>

extern "C" {
int socket(int domain, int type, int protocol) { return -1; }
int bind(int fd, const struct sockaddr *addr, socklen_t len) { return -1; }
int listen(int fd, int backlog) { return -1; }
int accept(int fd, struct sockaddr *addr, socklen_t *len) { return -1; }
int connect(int fd, const struct sockaddr *addr, socklen_t len) { return -1; }
ssize_t send(int fd, const void *buf, size_t n, int flags) { return -1; }
ssize_t recv(int fd, void *buf, size_t n, int flags) { return -1; }
struct hostent *gethostbyname(const char *name) { return NULL; }
}

/* firmware configuration interface and the disk image formats other than flat/growing */
#define STUB(name) \
  extern "C" int name(plugin_t *plugin, Bit16u type, Bit8u mode) { return 0; }

STUB(libfw_cfg_plugin_entry)
STUB(libvmware3_img_plugin_entry)
STUB(libvmware4_img_plugin_entry)
STUB(libvbox_img_plugin_entry)
STUB(libvpc_img_plugin_entry)
STUB(libvhdx_img_plugin_entry)
STUB(libvvfat_img_plugin_entry)

/* C++ runtime: static initialisation guards (single threaded) and aligned new/delete */
#include <stdint.h>
#include <stdlib.h>

extern "C" int __cxa_guard_acquire(uint64_t *g)
{
  return !*(char *)g;
}

extern "C" void __cxa_guard_release(uint64_t *g)
{
  *(char *)g = 1;
}

extern "C" void __cxa_guard_abort(uint64_t *g)
{
}

namespace std { enum class align_val_t : size_t {}; }

void operator delete(void *p, unsigned int, std::align_val_t) noexcept { free(p); }
void operator delete(void *p, std::align_val_t) noexcept { free(p); }

/* the main() of Bochs (renamed at build time), called from C */
int bochs_main(int argc, char *argv[]);

extern "C" char bx_rom_bios[], bx_rom_bios_end[], bx_rom_vgabios[], bx_rom_vgabios_end[];
extern "C" void bx_mem_file(const char *name, const char *data, size_t len);

#ifdef CONFIG_BOCHS_HEARTBEAT
/* timer callback (interrupt context): the interrupted host pc and the guest eip */
static void beat(struct k_timer *t)
{
  static unsigned last;
  unsigned now = (unsigned)bx_pc_system.time_ticks();

  /* guest instructions (virtual ticks) executed in the last second, and where the guest is */
  Bit8u code[16] = {0};
  bx_phy_address lin = (bx_phy_address)BX_CPU(0)->sregs[BX_SEG_REG_CS].cache.u.segment.base +
                       BX_CPU(0)->get_instruction_pointer();

  BX_MEM(0)->dbg_fetch_mem(BX_CPU(0), lin, 16, code);
  printk("beat: %u ticks/s cs=%04x eip=%08x code=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n", now - last,
         (unsigned)BX_CPU(0)->sregs[BX_SEG_REG_CS].selector.value,
         (unsigned)BX_CPU(0)->get_instruction_pointer(), code[0], code[1], code[2], code[3], code[4],
         code[5], code[6], code[7], code[8], code[9], code[10], code[11]);
  last = now;
}

static K_TIMER_DEFINE(beat_timer, beat, NULL);
#endif

#ifdef CONFIG_BOCHS_SELFTEST
/* floating point checks: the timer code of Bochs converts time with doubles (and a long double literal) */
static void fp_selftest(void)
{
  volatile unsigned ips = 3000000;
  volatile unsigned useconds = 2777;
  volatile unsigned long long ticks1 = 1234567;
  double m = double(ips) / 1000000.0L;
  unsigned long long a = (unsigned long long)(double(useconds) * m);
  unsigned long long b = (unsigned long long)(((double)(long long)ticks1) / m);
  unsigned long long c = (unsigned long long)(((double)(long long)ticks1) / m * 1000.0);
  volatile unsigned long long big = 5000000000ULL;
  unsigned long long d = (unsigned long long)(double(big) * m);

  printk("fp: m*1000=%u (3000) a=%u (8331) b=%u (411522) c=%u (411522333) d=%u (15000000000 mod 2^32 = 2820130816)\n",
         (unsigned)(m * 1000), (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)d);
}

/* memcpy/memmove/memset/memcmp of the minimal C library against byte loops, random sizes and alignments */
static void mem_selftest(void)
{
  static unsigned char a[2048], b[2048], ref[2048];
  unsigned seed = 12345;
  unsigned bad = 0, firstbad = 0, kind = 0, total = 0;

  for (int it = 0; it < 30000; it++) {
    seed = seed * 1103515245U + 12345U;
    unsigned sz = (seed >> 8) % 1000, so = (seed >> 3) & 7, d = (seed >> 20) & 7;
    unsigned op = (seed >> 24) % 4;

    for (int i = 0; i < 2048; i++) {
      a[i] = (unsigned char)(seed * (i + 7) >> 5);
      b[i] = a[i] ^ 0x5a;
    }
    memcpy(ref, b, sizeof(ref));
    total++;
    if (op == 0) {            /* memcpy between two buffers */
      memcpy(b + d, a + so, sz);
      for (unsigned i = 0; i < sz; i++) ref[d + i] = a[so + i];
    } else if (op == 1) {     /* memmove inside one buffer, overlapping */
      unsigned char tmp[1100];
      memcpy(tmp, b + so, sz);
      memmove(b + d + 3, b + so, sz);
      for (unsigned i = 0; i < sz; i++) ref[d + 3 + i] = tmp[i];
    } else if (op == 2) {     /* memset */
      memset(b + d, (int)(seed & 0xff), sz);
      for (unsigned i = 0; i < sz; i++) ref[d + i] = (unsigned char)(seed & 0xff);
    } else {                  /* memcmp */
      int r = memcmp(a + so, b + d, sz);
      int e = 0;
      for (unsigned i = 0; i < sz && !e; i++) e = (int)a[so + i] - (int)b[d + i];
      if ((r < 0) != (e < 0) || (r > 0) != (e > 0)) {
        if (!bad) { firstbad = it; kind = op; }
        bad++;
      }
      continue;
    }
    if (memcmp(b, ref, sizeof(ref)) != 0) {
      if (!bad) { firstbad = it; kind = op; }
      bad++;
    }
  }
  printk("memtest: %u cases, %u bad (first #%u op %u)\n", total, bad, firstbad, kind);
}

/* the x87 emulation of Bochs (softfloat) on small integers */
static void sf_selftest(void)
{
  struct softfloat_status_t st;
  extFloat80_t a, b, c, d;
  int32_t r1, r2, r3, r4;

  memset(&st, 0, sizeof(st));
  st.extF80_roundingPrecision = 80;
  st.softfloat_roundingMode = softfloat_round_near_even;
  st.softfloat_exceptionMasks = 0x3f;

  volatile int32_t x = 6, y = 9, z = 3, w = 400;
  a = i32_to_extF80(x);                                   /* 6 */
  b = i32_to_extF80(y);                                   /* 9 */
  c = extF80_mul(extF80_add(a, b, &st), i32_to_extF80(z), &st);   /* 45 */
  d = extF80_div(c, i32_to_extF80(5), &st);               /* 9 */
  r1 = extF80_to_i32(c, softfloat_round_near_even, true, &st);
  r2 = extF80_to_i32(d, softfloat_round_near_even, true, &st);
  r3 = extF80_to_i32(extF80_sqrt(i32_to_extF80(w), &st), softfloat_round_near_even, true, &st);  /* 20 */
  r4 = extF80_to_i32(extF80_sub(a, b, &st), softfloat_round_near_even, true, &st);               /* -3 */
  printk("softfloat: %d (45) %d (9) %d (20) %d (-3)\n", (int)r1, (int)r2, (int)r3, (int)r4);
}

#endif

extern "C" int bochs_run(int argc, char *argv[])
{
#ifdef CONFIG_BOCHS_SELFTEST
  sf_selftest();
  mem_selftest();
  fp_selftest();
#endif
#ifdef CONFIG_BOCHS_HEARTBEAT
  k_timer_start(&beat_timer, K_SECONDS(1), K_SECONDS(1));
#endif
  bx_mem_file("BIOS-bochs-legacy", bx_rom_bios, bx_rom_bios_end - bx_rom_bios);
  bx_mem_file("VGABIOS-lgpl-latest", bx_rom_vgabios, bx_rom_vgabios_end - bx_rom_vgabios);
  return bochs_main(argc, argv);
}
