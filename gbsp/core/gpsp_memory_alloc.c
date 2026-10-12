/* The arrays of the core. The small ones are static. The big ones (about 1.1 MiB) are one block
 * that is allocated when a game is opened and given back when it is closed: the memory is the
 * arena the rest of the application (video decoder, pictures) uses while no game runs.
 * reg[64], spsr[6] and reg_mode[7][7] are in riscv_stubs.c. */
#include <stdlib.h>
#include <string.h>
#include "common.h"

u8 *memory_map_read[8 * 1024];
u16 oam_ram[512];
u16 palette_ram[512];
u16 palette_ram_converted[512];
u16 io_registers[512];

#ifdef HAVE_DYNAREC
/* Memory handler dispatch tables (referenced by the emitter):
   tmemld[11][16]: load handlers, tmemst[4][16]: store handlers, thnjal: thumb handler jumps */
u32 tmemld[11][16];
u32 tmemst[4][16];
u32 thnjal[15 * 16];
#endif

u8 *ewram;
u8 *iwram;
u8 *vram;
u8 *gamepak_backup;   /* up to 128 KiB: SRAM, flash ROM or EEPROM */
u32 *rom_branch_hash;
u8 (*obj_priority_list)[160][128];

#define OBJ_LIST_SIZE (5 * 160 * 128)
#define HASH_BYTES    (ROM_BRANCH_HASH_SIZE * sizeof(u32))

static void *block;

int gpsp_ram_alloc(void)
{
  u8 *p;

  if (block)
    return 0;
  block = aligned_alloc(64, EWRAM_SIZE + IWRAM_SIZE + VRAM_SIZE + BACKUP_SIZE + OBJ_LIST_SIZE + HASH_BYTES);
  if (!block)
    return -1;

  p = block;
  ewram = p;            p += EWRAM_SIZE;
  rom_branch_hash = (u32 *)p;   p += HASH_BYTES;
  obj_priority_list = (u8 (*)[160][128])p;   p += OBJ_LIST_SIZE;
  iwram = p;            p += IWRAM_SIZE;
  vram = p;             p += VRAM_SIZE;
  gamepak_backup = p;
  memset(block, 0, (size_t)(p + BACKUP_SIZE - (u8 *)block));
  return 0;
}

void gpsp_ram_free(void)
{
  free(block);
  block = NULL;
  ewram = iwram = vram = gamepak_backup = NULL;
  rom_branch_hash = NULL;
  obj_priority_list = NULL;
}
