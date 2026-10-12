/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * RISC-V back end of the gpSP recompiler: the register map, the memory access
 * stubs and the entry point. The macros that translate single ARM/Thumb
 * instructions are in riscv_emit.h (they are expanded inside cpu_threaded.c);
 * everything that is a plain function lives here and is compiled on its own.
 */

#include "riscv/riscv_emit.h"
#include "common.h"

#define INITIAL_ROM_WATERMARK                                                  \
  16 /* as in cpu_threaded.c, keeps block offset 0 unused */

extern u32 rom_cache_watermark;

u32 arm_to_mips_reg[] =
    {
        reg_r0,  reg_r1,  reg_r2,  reg_r3, reg_r4,  reg_r5,
        reg_r6,  reg_r7,  reg_r8,  reg_r9, reg_r10, reg_r11,
        reg_r12, reg_r13, reg_r14, reg_a0, /* r15: PC writes go to a0 for branch
                                              dispatch */
        reg_a1,                            /* arm_reg_a1: scratch */
        reg_a2                             /* arm_reg_a2: scratch */
};

u32 execute_spsr_restore_body(u32 address) {
  set_cpu_mode(cpu_modes[reg[REG_CPSR] & 0xF]);
  if ((io_registers[REG_IE] & io_registers[REG_IF]) && io_registers[REG_IME] &&
      ((reg[REG_CPSR] & 0x80) == 0)) {
    REG_MODE(MODE_IRQ)[6] = address + 4;
    REG_SPSR(MODE_IRQ) = reg[REG_CPSR];
    reg[REG_CPSR] = 0xD2;
    address = 0x00000018;
    set_cpu_mode(MODE_IRQ);
  }
  if (reg[REG_CPSR] & 0x20)
    address |= 0x01;
  return address;
}

u32 execute_store_cpsr_body(u32 _cpsr, u32 address) {
  set_cpu_mode(cpu_modes[_cpsr & 0xF]);
  if ((io_registers[REG_IE] & io_registers[REG_IF]) && io_registers[REG_IME] &&
      ((_cpsr & 0x80) == 0)) {
    REG_MODE(MODE_IRQ)[6] = address + 4;
    REG_SPSR(MODE_IRQ) = _cpsr;
    reg[REG_CPSR] = 0xD2;
    set_cpu_mode(MODE_IRQ);
    return 0x00000018;
  }
  return 0;
}

u32 *openld_core_ptrs[11];

const u8 ldopmap[6][2] = {{0, 1}, {1, 2}, {2, 4}, {4, 6}, {6, 10}, {10, 11}};
const u8 ldhldrtbl[11] = {0, 1, 2, 2, 3, 3, 4, 4, 4, 4, 5};

/* Memory access stub generation (minimal for initial port)
 *
 * The full MIPS version generates optimized per-region stubs at
 * init time. For the initial RISC-V port, we use generic C function
 * handlers (read_memory/write_memory) and wrap them in small
 * JIT stubs that save/restore GBA regs around the C call.
 *
 * Each load stub:  save regs, call read_memory_X(a0), restore, ret
 * Each store stub: save regs, call write_memory_X(a0,a1), check alert, restore,
 * ret
 *
 * TODO: Generate optimized per-region RISC-V stubs.
 */

/* Emit a load stub: saves GBA regs, calls C read fn, restores, returns value in
 * a0 */
static u8 *emit_load_stub(u8 *translation_ptr, void *c_fn) {
  u8 *entry = translation_ptr;

  /* Save ra and GBA regs */
  rv_sw(RV, rv_ra, reg_base, REG_SAVE * 4);
  emit_save_regs(true);

  /* Store current block's PC for BIOS read protection.
   * reg_pc (s2) holds stored_pc; BIOS blocks have stored_pc < 0x4000
   * so read_memory* will allow BIOS reads when executing from BIOS. */
  rv_sw(RV, reg_pc, reg_base, REG_PC * 4);

  /* Call C function: a0 = address (already set), returns value in a0 */
  genccall(c_fn);

  /* Restore GBA regs and ra */
  emit_restore_regs(true);
  rv_lw(RV, rv_ra, reg_base, REG_SAVE * 4);
  rv_ret(RV);

  return entry;
}

/* Emit a store stub: saves GBA regs, calls C write fn, checks alert, returns */
static u8 *emit_store_stub(u8 *translation_ptr, void *c_fn) {
  u8 *entry = translation_ptr;

  /* Save ra and GBA regs */
  rv_sw(RV, rv_ra, reg_base, REG_SAVE * 4);
  emit_save_regs(true);

  /* Store current PC (a2 = pc+4 from JIT site) for write_io_epilogue.
   * check_and_raise_interrupts uses reg[REG_PC] for IRQ return address. */
  rv_sw(RV, reg_a2, reg_base, REG_PC * 4);

  /* Call C function: a0=address, a1=value, returns alert flags in a0 */
  genccall(c_fn);

  /* Check for alert (non-zero return = side effects) */
  rv_beq(RV, rv_a0, rv_zero,
         3 * 4); /* skip lui+jalr (2 insns) to normal return */

  /* Alert path: jump to write_io_epilogue (does not return normally) */
  rv_call_abs(RV, rv_zero, reg_temp, (u32)write_io_epilogue);

  /* Normal return */
  emit_restore_regs(true);
  rv_lw(RV, rv_ra, reg_base, REG_SAVE * 4);
  rv_ret(RV);

  return entry;
}

/* Fill all 16 region entries for a tmem row with the same stub address */
static void fill_tmem_row(u32 *row, u32 addr) {
  int i;
  for (i = 0; i < 16; i++)
    row[i] = addr;
}

void init_emitter(bool must_swap) {
  (void)must_swap;

  rom_cache_watermark = INITIAL_ROM_WATERMARK;
  u8 *translation_ptr = &rom_translation_cache[0];
  u8 *stub;

  rv_stubs_init(RV);

  /* Generate load stubs and fill tmemld table */
  /* tmemld[0] = load u8 */
  stub = emit_load_stub(translation_ptr, (void *)read_memory8);
  fill_tmem_row(tmemld[0], (u32)stub);
  translation_ptr = stub + 256; /* leave room between stubs */

  /* tmemld[1] = load s8 */
  stub = emit_load_stub(translation_ptr, (void *)read_memory8s);
  fill_tmem_row(tmemld[1], (u32)stub);
  translation_ptr = stub + 256;

  /* tmemld[2] = load u16 */
  stub = emit_load_stub(translation_ptr, (void *)read_memory16);
  fill_tmem_row(tmemld[2], (u32)stub);
  fill_tmem_row(tmemld[3], (u32)stub); /* u16 unaligned uses same handler */
  translation_ptr = stub + 256;

  /* tmemld[4] = load s16 */
  stub = emit_load_stub(translation_ptr, (void *)read_memory16s);
  fill_tmem_row(tmemld[4], (u32)stub);
  fill_tmem_row(tmemld[5], (u32)stub); /* s16 unaligned */
  translation_ptr = stub + 256;

  /* tmemld[6] = load u32 */
  stub = emit_load_stub(translation_ptr, (void *)read_memory32);
  fill_tmem_row(tmemld[6], (u32)stub);
  fill_tmem_row(tmemld[7], (u32)stub); /* u32 unaligned variants */
  fill_tmem_row(tmemld[8], (u32)stub);
  fill_tmem_row(tmemld[9], (u32)stub);
  translation_ptr = stub + 256;

  /* tmemld[10] = aligned load 32 (same as u32 for C fallback) */
  fill_tmem_row(tmemld[10], (u32)stub);

  /* Generate store stubs and fill tmemst table */
  /* tmemst[0] = store u8 */
  stub = emit_store_stub(translation_ptr, (void *)write_memory8);
  fill_tmem_row(tmemst[0], (u32)stub);
  translation_ptr = stub + 256;

  /* tmemst[1] = store u16 */
  stub = emit_store_stub(translation_ptr, (void *)write_memory16);
  fill_tmem_row(tmemst[1], (u32)stub);
  translation_ptr = stub + 256;

  /* tmemst[2] = store u32 */
  stub = emit_store_stub(translation_ptr, (void *)write_memory32);
  fill_tmem_row(tmemst[2], (u32)stub);
  translation_ptr = stub + 256;

  /* tmemst[3] = aligned store 32 (same as u32 for C fallback) */
  fill_tmem_row(tmemst[3], (u32)(translation_ptr - 256)); /* reuse u32 stub */

  /* thnjal table: unused in RISC-V (MIPS-specific JAL encoding), zero it */
  memset(thnjal, 0, sizeof(thnjal));

  /* Write the data cache back, then synchronise the instruction cache */
  {
    uintptr_t aligned_start = (uintptr_t)&rom_translation_cache[0] & ~63UL;
    uintptr_t aligned_end = ((uintptr_t)translation_ptr + 63) & ~63UL;
    sys_cache_data_flush_range((void *)aligned_start,
                               aligned_end - aligned_start);
    sys_cache_instr_invd_range((void *)aligned_start,
                               aligned_end - aligned_start);
  }
  __asm__ volatile("fence.i" ::: "memory");

  /* Set watermark so ROM cache flushes don't wipe our stubs */
  rom_cache_watermark = (u32)(translation_ptr - rom_translation_cache);

  init_bios_hooks();
}

/* Entry point wrapper
 * ================================================================== */

u32 execute_arm_translate(u32 cycles) {
  return rv_stubs.entry(cycles, &reg[0]);
}
