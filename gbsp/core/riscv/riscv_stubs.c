/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * The routines that move between C and the translated code. They cannot be C:
 * the guest registers live in fixed host registers while translated code runs,
 * and these routines save and restore them around calls into the emulator. They
 * are generated once at start up, with the same emitter as the translated code,
 * in front of the translation cache (see rv_stubs_init()), and are called
 * through the addresses in `rv_stubs`.
 *
 * Host register use (see riscv_emit.h): s0 = &reg[0], s1 = cycle counter, s2 =
 * stored pc, s3..s6 = N, Z, C, V flags (0 or 1), guest r0..r14 in t0-t2, a3-a7,
 * s7-s11, t3, t4; a0 = r15 / argument / result, a1, a2 arguments, t5 and t6
 * scratch.
 */

#include "common.h"
#include "riscv/riscv_emit.h"

/* The register file: the three arrays are kept together in one section, in this order. */
#define REGFILE __attribute__((section(".bss.regfile")))
u32 reg[64] REGFILE __attribute__((aligned(64)));
u32 spsr[6] REGFILE;
u32 reg_mode[7][7] REGFILE;

struct rv_stub_table rv_stubs;

#define STACK_FRAME 64 /* s0-s11 and ra, kept 16 byte aligned */

enum { CPSR_N = 31, CPSR_Z = 30, CPSR_C = 29, CPSR_V = 28 };

#define FLAG_SLOTS 4
static const rv_reg flag_reg[FLAG_SLOTS] = {reg_n_cache, reg_z_cache,
                                            reg_c_cache, reg_v_cache};
static const u8 flag_bit[FLAG_SLOTS] = {CPSR_N, CPSR_Z, CPSR_C, CPSR_V};

/* ---- building blocks ---------------------------------------------------- */

static void save_registers(rv_code *c) {
  for (int i = 0; i < 15; i++)
    rv_sw(c, arm_to_mips_reg[i], reg_base, 4 * i);
}

static void restore_registers(rv_code *c) {
  for (int i = 0; i < 15; i++)
    rv_lw(c, arm_to_mips_reg[i], reg_base, 4 * i);
}

/* reg[CPSR] = (reg[CPSR] & 0x0fffffff) | flags << 28; the new value is also
 * left in `scratch` */
static void collapse_flags(rv_code *c, rv_reg scratch) {
  rv_lw(c, scratch, reg_base, REG_CPSR * 4);
  rv_li(c, reg_temp, 0x0FFFFFFF);
  rv_and(c, scratch, scratch, reg_temp);
  for (int i = 0; i < FLAG_SLOTS; i++) {
    rv_slli(c, reg_temp, flag_reg[i], flag_bit[i]);
    rv_or(c, scratch, scratch, reg_temp);
  }
  rv_sw(c, scratch, reg_base, REG_CPSR * 4);
}

/* N, Z, C and V registers from the CPSR value in `src` */
static void extract_flags(rv_code *c, rv_reg src) {
  for (int i = 0; i < FLAG_SLOTS; i++) {
    rv_srli(c, flag_reg[i], src, flag_bit[i]);
    if (flag_bit[i] != CPSR_N) /* the top bit needs no mask */
      rv_andi(c, flag_reg[i], flag_reg[i], 1);
  }
}

static void load_cpsr_flags(rv_code *c, rv_reg scratch) {
  rv_lw(c, scratch, reg_base, REG_CPSR * 4);
  extract_flags(c, scratch);
}

/* Call a C function; only ra is clobbered */
static void call_c(rv_code *c, const void *fn) {
  rv_call_abs(c, rv_ra, rv_ra, (u32)fn);
}

static void jump_to(rv_code *c, const u8 *target) {
  rv_jal(c, rv_zero, (s32)(target - *c));
}

static void branch_to(rv_code *c, enum rv_branch_op op, rv_reg a, rv_reg b,
                      const u8 *target) {
  rv_branch(c, op, a, b, (s32)(target - *c));
}

static void jump_reg(rv_code *c, rv_reg r) { rv_jalr(c, rv_zero, r, 0); }

/* cycles = result & 0x7fff */
static void take_cycles(rv_code *c) {
  rv_li(c, reg_temp, 0x7FFF);
  rv_and(c, reg_cycles, rv_a0, reg_temp);
}

/* a0 = address of a word of a C array indexed by the register `index` (in
 * words) */
static void array_slot(rv_code *c, rv_reg dst, rv_reg index,
                       const void *array) {
  rv_slli(c, dst, index, 2);
  rv_li(c, reg_rv, (u32)array);
  rv_add(c, dst, dst, reg_rv);
}

/* ---- the stubs ---------------------------------------------------------- */

static const rv_reg callee_saved[12] = {
    rv_s0, rv_s1, rv_s2, rv_s3, rv_s4,  rv_s5,
    rv_s6, rv_s7, rv_s8, rv_s9, rv_s10, rv_s11,
};

/* Leave the translated code: restore the registers of the C caller and return
 * to it. The guest registers must have been saved already. */
static const u8 *emit_return_to_main(rv_code *c) {
  const u8 *at = *c;

  for (int i = 0; i < 12; i++)
    rv_lw(c, callee_saved[i], rv_sp, 4 * i);
  rv_lw(c, rv_ra, rv_sp, 4 * 12);
  rv_addi(c, rv_sp, rv_sp, STACK_FRAME);
  rv_ret(c);
  return at;
}

/* Find the translated block of reg[PC] (ARM or Thumb by the CPSR) and run it.
 * Entered at `sleep_loop` the pending cycles are first handed to the hardware
 * emulation. */
struct lookup_labels {
  const u8 *sleep_loop;
  const u8 *lookup_pc;
  const u8 *lookup_pc_noflags;
};

static void emit_lookup(rv_code *c, const u8 *to_main,
                        struct lookup_labels *l) {
  /* cpu_sleep_loop */
  l->sleep_loop = *c;
  rv_mv(c, rv_a0, reg_cycles);
  call_c(c, update_gba);
  branch_to(c, RV_BLT, rv_a0, rv_zero, to_main); /* frame complete */
  take_cycles(c);

  /* lookup_pc: flags from the CPSR first, then the block of reg[PC] */
  l->lookup_pc = *c;
  load_cpsr_flags(c, reg_temp);
  l->lookup_pc_noflags = *c;
  rv_lw(c, reg_temp, reg_base, REG_CPSR * 4);
  rv_andi(c, reg_temp, reg_temp, 0x20); /* Thumb bit */
  rv_lw(c, rv_a0, reg_base, REG_PC * 4);

  u8 *thumb = rv_bne(c, reg_temp, rv_zero, 0);
  call_c(c, block_lookup_address_arm);
  u8 *done = rv_jal(c, rv_zero, 0);
  rv_patch_branch(thumb, *c);
  call_c(c, block_lookup_address_thumb);
  rv_patch_jal(done, *c);

  branch_to(c, RV_BEQ, rv_a0, rv_zero, to_main); /* cannot translate this pc */
  restore_registers(c);
  jump_reg(c, rv_a0);
}

/* update_gba(): the cycle counter ran out. a0 = next guest pc. */
static u32 emit_update_gba(rv_code *c, const u8 *to_main,
                           const struct lookup_labels *l) {
  u32 at = (u32)*c;

  rv_sw(c, rv_a0, reg_base, REG_PC * 4);
  rv_sw(c, rv_ra, reg_base, REG_SAVE2 * 4);
  collapse_flags(c, rv_a0);
  save_registers(c);

  rv_mv(c, rv_a0, reg_cycles);
  call_c(c, update_gba);
  branch_to(c, RV_BLT, rv_a0, rv_zero, to_main);
  take_cycles(c);

  rv_slli(c, reg_temp, rv_a0, 1); /* bit 30: the pc changed */
  branch_to(c, RV_BLT, reg_temp, rv_zero, l->lookup_pc);

  rv_lw(c, rv_ra, reg_base, REG_SAVE2 * 4);
  restore_registers(c);
  rv_ret(c);
  return at;
}

static u32 emit_cheat_hook(rv_code *c) {
  u32 at = (u32)*c;

  rv_sw(c, rv_ra, reg_base, REG_SAVE2 * 4);
  save_registers(c);
  call_c(c, process_cheats);
  rv_lw(c, rv_ra, reg_base, REG_SAVE2 * 4);
  restore_registers(c);
  rv_ret(c);
  return at;
}

/* a0 = guest address; run the block there. dual: bit 0 is stripped (the mode
 * comes from the CPSR) */
static u32 emit_indirect_branch(rv_code *c, const void *lookup, bool dual,
                                const struct lookup_labels *l) {
  u32 at = (u32)*c;

  save_registers(c);
  if (dual) {
    rv_andi(c, reg_temp, rv_a0, -2);
    rv_sw(c, reg_temp, reg_base, REG_PC * 4);
  } else {
    rv_sw(c, rv_a0, reg_base, REG_PC * 4);
  }
  call_c(c, lookup);
  branch_to(c, RV_BEQ, rv_a0, rv_zero, l->lookup_pc);
  rv_mv(c, reg_temp, rv_a0);
  restore_registers(c);
  jump_reg(c, reg_temp);
  return at;
}

/* after a store with side effects; a0 = alert flags */
static u32 emit_write_io_epilogue(rv_code *c, const struct lookup_labels *l) {
  u32 at = (u32)*c;

  rv_mv(c, reg_pc, rv_a0); /* reg_pc is reloaded by the lookup */
  collapse_flags(c, rv_a0);

  rv_andi(c, rv_a0, reg_pc, CPU_ALERT_SMC);
  u8 *no_smc = rv_beq(c, rv_a0, rv_zero, 0);
  call_c(c, flush_translation_cache_ram);
  rv_patch_branch(no_smc, *c);

  rv_andi(c, rv_a0, reg_pc, CPU_ALERT_IRQ);
  u8 *no_irq = rv_beq(c, rv_a0, rv_zero, 0);
  call_c(c, check_and_raise_interrupts);
  rv_patch_branch(no_irq, *c);

  rv_andi(c, rv_a0, reg_pc, CPU_ALERT_HALT);
  branch_to(c, RV_BEQ, rv_a0, rv_zero, l->lookup_pc);
  jump_to(c, l->sleep_loop);
  return at;
}

/* self modifying code was written: drop the RAM translations and carry on at a2
 */
static u32 emit_smc_write(rv_code *c, const struct lookup_labels *l) {
  u32 at = (u32)*c;

  rv_sw(c, rv_a2, reg_base, REG_PC * 4);
  save_registers(c);
  call_c(c, flush_translation_cache_ram);
  rv_lw(c, reg_temp, reg_base, REG_CPSR * 4);
  jump_to(c, l->lookup_pc_noflags);
  return at;
}

static u32 emit_read_cpsr(rv_code *c) {
  u32 at = (u32)*c;

  collapse_flags(c, rv_a0);
  rv_ret(c);
  return at;
}

static u32 emit_read_spsr(rv_code *c) {
  u32 at = (u32)*c;

  rv_lw(c, reg_temp, reg_base, CPU_MODE * 4);
  rv_andi(c, reg_temp, reg_temp, 0xF); /* without the privilege bits */
  array_slot(c, reg_temp, reg_temp, spsr);
  rv_lw(c, rv_a0, reg_temp, 0);
  rv_ret(c);
  return at;
}

/* software interrupt: a0 = return address. Enters supervisor mode with IRQs
 * off. */
static u32 emit_swi(rv_code *c) {
  u32 at = (u32)*c;

  rv_sw(c, rv_ra, reg_base, REG_SAVE3 * 4);
  rv_li(c, reg_rv, (u32)&reg_mode[MODE_SUPERVISOR & 0xF][6]);
  rv_sw(c, rv_a0, reg_rv, 0); /* supervisor lr */

  collapse_flags(c, rv_a0);
  rv_li(c, reg_rv, (u32)&spsr[MODE_SUPERVISOR & 0xF]);
  rv_sw(c, rv_a0, reg_rv, 0); /* supervisor spsr = cpsr */

  rv_srli(c, rv_a0, rv_a0, 6); /* clear the mode and the T and F bits */
  rv_slli(c, rv_a0, rv_a0, 6);
  rv_ori(c, rv_a0, rv_a0, 0x13 | 0x80);
  rv_sw(c, rv_a0, reg_base, REG_CPSR * 4);

  save_registers(c);
  rv_li(c, rv_a0, 0x13);
  call_c(c, set_cpu_mode);

  rv_lw(c, rv_ra, reg_base, REG_SAVE3 * 4);
  restore_registers(c);

  rv_li(c, rv_a0, 0xe3a02004); /* what the bus shows after the BIOS entry */
  rv_sw(c, rv_a0, reg_base, REG_BUS_VALUE * 4);
  rv_ret(c);
  return at;
}

/* return from an exception: cpsr = spsr[mode]. a0 = address, returns the
 * address to continue at. */
static u32 emit_spsr_restore(rv_code *c) {
  u32 at = (u32)*c;

  rv_lw(c, reg_temp, reg_base, CPU_MODE * 4);
  rv_andi(c, reg_temp, reg_temp, 0xF);
  u8 *user_mode =
      rv_beq(c, reg_temp, rv_zero, 0); /* user and system mode have none */

  array_slot(c, rv_a1, reg_temp, spsr);
  rv_lw(c, reg_temp, rv_a1, 0);
  rv_sw(c, reg_temp, reg_base, REG_CPSR * 4);
  extract_flags(c, reg_temp);

  rv_sw(c, rv_ra, reg_base, REG_SAVE3 * 4);
  save_registers(c);
  call_c(c, execute_spsr_restore_body);
  restore_registers(c);
  rv_lw(c, rv_ra, reg_base, REG_SAVE3 * 4);

  rv_patch_branch(user_mode, *c);
  rv_ret(c);
  return at;
}

/* write the cpsr: a0 = value. The call site is followed by two mask words
 * (user, privileged mode), the return address skips them. */
static u32 emit_store_cpsr(rv_code *c, const struct lookup_labels *l) {
  u32 at = (u32)*c;

  rv_lw(c, rv_a2, reg_base, CPU_MODE * 4);
  rv_andi(c, rv_a2, rv_a2,
          0x10); /* privilege bit: 0 or 4 bytes into the masks */
  rv_srli(c, rv_a2, rv_a2, 2);
  rv_add(c, rv_a2, rv_a2, rv_ra);
  rv_lw(c, rv_a2, rv_a2, 0); /* store mask */
  rv_addi(c, rv_ra, rv_ra, 8);

  rv_and(c, reg_temp, rv_a0, rv_a2); /* new bits */
  rv_lw(c, reg_rv, reg_base, REG_CPSR * 4);
  rv_not(c, rv_a0, rv_a2);
  rv_and(c, reg_rv, reg_rv, rv_a0); /* kept bits */
  rv_or(c, reg_temp, reg_temp, reg_rv);
  extract_flags(c, reg_temp);

  rv_andi(c, rv_a2, rv_a2, 0xFF); /* are the mode bits written? */
  rv_sw(c, reg_temp, reg_base, REG_CPSR * 4);
  u8 *no_mode = rv_beq(c, rv_a2, rv_zero, 0);

  rv_sw(c, rv_ra, reg_base, REG_SAVE3 * 4);
  save_registers(c);
  rv_mv(c, rv_a0, reg_temp);
  call_c(c,
         execute_store_cpsr_body); /* 0, or the address of an interrupt entry */
  u8 *same_pc = rv_beq(c, rv_a0, rv_zero, 0);

  call_c(c, block_lookup_address_arm);
  rv_mv(c, reg_temp, rv_a0);
  restore_registers(c);
  jump_reg(c, reg_temp);

  rv_patch_branch(same_pc, *c);
  restore_registers(c);
  rv_lw(c, rv_ra, reg_base, REG_SAVE3 * 4);
  rv_patch_branch(no_mode, *c);
  rv_ret(c);
  return at;
}

/* write the spsr of the current mode: a0 = value, a1 = mask */
static u32 emit_store_spsr(rv_code *c) {
  u32 at = (u32)*c;

  rv_lw(c, reg_temp, reg_base, CPU_MODE * 4);
  rv_andi(c, reg_temp, reg_temp, 0xF);
  array_slot(c, reg_temp, reg_temp, spsr);
  rv_lw(c, reg_rv, reg_temp, 0);
  rv_and(c, rv_a0, rv_a0, rv_a1);
  rv_not(c, rv_a1, rv_a1);
  rv_and(c, reg_rv, reg_rv, rv_a1);
  rv_or(c, rv_a0, rv_a0, reg_rv);
  rv_sw(c, rv_a0, reg_temp, 0);
  rv_ret(c);
  return at;
}

/* u32 entry(u32 cycles, u32 *reg): run the translated code until the frame is
 * done */
static u32 emit_entry(rv_code *c, const struct lookup_labels *l) {
  u32 at = (u32)*c;

  rv_addi(c, rv_sp, rv_sp, -STACK_FRAME);
  for (int i = 0; i < 12; i++)
    rv_sw(c, callee_saved[i], rv_sp, 4 * i);
  rv_sw(c, rv_ra, rv_sp, 4 * 12);

  rv_mv(c, reg_base, rv_a1);
  rv_mv(c, reg_cycles, rv_a0);
  load_cpsr_flags(c, reg_temp);

  rv_lw(c, reg_temp, reg_base, CPU_HALT_STATE * 4);
  branch_to(c, RV_BNE, reg_temp, rv_zero, l->sleep_loop);

  jump_to(c, l->lookup_pc_noflags);
  return at;
}

/* Start the next routine on a cache line */
static void align_line(rv_code *c)
{
  while ((u32)*c & 63)
    rv_nop(c);
}

/* Generate all of them at *c; the table gets their addresses. */
void rv_stubs_init(rv_code *c) {
  struct lookup_labels l;
  const u8 *to_main = emit_return_to_main(c);

  align_line(c);
  emit_lookup(c, to_main, &l);
  align_line(c);
  rv_stubs.update_gba = emit_update_gba(c, to_main, &l);
  align_line(c);
  rv_stubs.cheat_hook = emit_cheat_hook(c);
  align_line(c);
  rv_stubs.indirect_arm =
      emit_indirect_branch(c, block_lookup_address_arm, false, &l);
  align_line(c);
  rv_stubs.indirect_thumb =
      emit_indirect_branch(c, block_lookup_address_thumb, false, &l);
  align_line(c);
  rv_stubs.indirect_dual =
      emit_indirect_branch(c, block_lookup_address_dual, true, &l);
  align_line(c);
  rv_stubs.io_epilogue = emit_write_io_epilogue(c, &l);
  align_line(c);
  rv_stubs.smc = emit_smc_write(c, &l);
  align_line(c);
  rv_stubs.read_cpsr = emit_read_cpsr(c);
  align_line(c);
  rv_stubs.read_spsr = emit_read_spsr(c);
  align_line(c);
  rv_stubs.swi = emit_swi(c);
  align_line(c);
  rv_stubs.spsr_restore = emit_spsr_restore(c);
  align_line(c);
  rv_stubs.store_cpsr = emit_store_cpsr(c, &l);
  align_line(c);
  rv_stubs.store_spsr = emit_store_spsr(c);
  align_line(c);
  rv_stubs.entry = (u32 (*)(u32, u32 *))emit_entry(c, &l);
}
