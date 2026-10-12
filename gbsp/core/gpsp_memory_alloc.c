/* The large arrays of the core when it runs with the recompiler: the generated code addresses
   them relative to each other, so they are defined here in one place (the interpreter defines
   the same ones in cpu.cpp). reg[64], spsr[6] and reg_mode[7][7] are in riscv_stubs.c. */
#include "common.h"

#ifdef HAVE_DYNAREC
u8 *memory_map_read[8 * 1024];
u16 oam_ram[512];
u16 palette_ram[512];
u16 palette_ram_converted[512];
u8 ewram[1024 * 256 * 2];
u8 iwram[1024 * 32 * 2];
u8 vram[1024 * 96];
u16 io_registers[512];

/* Memory handler dispatch tables (referenced by the emitter):
   tmemld[11][16]: load handlers, tmemst[4][16]: store handlers, thnjal: thumb handler jumps */
u32 tmemld[11][16];
u32 tmemst[4][16];
u32 thnjal[15 * 16];
#endif
