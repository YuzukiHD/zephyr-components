/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Yuzuki Tsuru
 */
/* gameplaySP - RISC-V dynamic recompiler emit header
 *
 * Based on mips_emit.h by Exophase <exophase@gmail.com>
 * RISC-V back end (rv32imac)
 *
 * Key differences from MIPS:
 *   - No delay slots (simplifies all branch/call patterns)
 *   - No movz/movn (use branch-over patterns instead)
 *   - No hi:lo multiply (use mul/mulh directly)
 *   - No nor (use xori -1 for NOT)
 *   - 12-bit signed immediates (vs 16-bit on MIPS)
 *   - Function calls via lui+jalr (no absolute jal)
 */

#ifndef RISCV_EMIT_H
#define RISCV_EMIT_H

#include "common.h"
#include "riscv/riscv_asm.h"
#include <zephyr/cache.h>

/* Stub routines */
/* Routines between C and the translated code, generated at start up (riscv_stubs.c). The macros
 * below give the call sites the address of each one. */
struct rv_stub_table {
  u32 update_gba, cheat_hook;
  u32 indirect_arm, indirect_thumb, indirect_dual;
  u32 io_epilogue, smc;
  u32 read_cpsr, read_spsr, swi;
  u32 spsr_restore, store_cpsr, store_spsr;
  u32 (*entry)(u32 cycles, u32 *regs);
};
extern struct rv_stub_table rv_stubs;
void rv_stubs_init(rv_code *c);

#define riscv_update_gba          (rv_stubs.update_gba)
#define riscv_cheat_hook          (rv_stubs.cheat_hook)
#define riscv_indirect_branch_arm   (rv_stubs.indirect_arm)
#define riscv_indirect_branch_thumb (rv_stubs.indirect_thumb)
#define riscv_indirect_branch_dual  (rv_stubs.indirect_dual)
#define write_io_epilogue         (rv_stubs.io_epilogue)
#define smc_write                 (rv_stubs.smc)
#define execute_read_cpsr         (rv_stubs.read_cpsr)
#define execute_read_spsr         (rv_stubs.read_spsr)
#define execute_swi               (rv_stubs.swi)
#define execute_spsr_restore      (rv_stubs.spsr_restore)
#define execute_store_cpsr        (rv_stubs.store_cpsr)
#define execute_store_spsr        (rv_stubs.store_spsr)

/* Memory handler pointer tables (filled by stub init) */
extern u32 tmemld[11][16];
extern u32 tmemst[4][16];
extern u32 thnjal[15 * 16];

/* Default handlers - IWRAM assumed, aligned by default */
#define execute_load_u8 tmemld[0][3]
#define execute_load_s8 tmemld[1][3]
#define execute_load_u16 tmemld[2][3]
#define execute_load_s16 tmemld[4][3]
#define execute_load_u32 tmemld[6][3]
#define execute_aligned_load32 tmemld[10][3]
#define execute_store_u8 tmemst[0][3]
#define execute_store_u16 tmemst[1][3]
#define execute_store_u32 tmemst[2][3]
#define execute_aligned_store32 tmemst[3][3]

/* ==================================================================
 * Register Mapping
 * ================================================================== */

/* System registers (callee-saved) */
#define reg_base rv_s0    /* GBA state array base pointer */
#define reg_cycles rv_s1  /* Cycle counter (counts down) */
#define reg_pc rv_s2      /* Stored PC value */
#define reg_n_cache rv_s3 /* N flag */
#define reg_z_cache rv_s4 /* Z flag */
#define reg_c_cache rv_s5 /* C flag */
#define reg_v_cache rv_s6 /* V flag */

/* Temporaries and function args */
#define reg_a0 rv_a0     /* fn arg0 / return value / r15 target */
#define reg_a1 rv_a1     /* fn arg1 */
#define reg_a2 rv_a2     /* fn arg2 */
#define reg_rv rv_t6     /* secondary scratch / return value copy */
#define reg_temp rv_t5   /* primary scratch */
#define reg_zero rv_zero /* constant zero */

/* GBA r0-r14 mapped to host registers */
#define reg_r0 rv_t0
#define reg_r1 rv_t1
#define reg_r2 rv_t2
#define reg_r3 rv_a3
#define reg_r4 rv_a4
#define reg_r5 rv_a5
#define reg_r6 rv_a6
#define reg_r7 rv_a7
#define reg_r8 rv_s7
#define reg_r9 rv_s8
#define reg_r10 rv_s9
#define reg_r11 rv_s10
#define reg_r12 rv_s11
#define reg_r13 rv_t3
#define reg_r14 rv_t4

/* GBA register index -> host register lookup */
extern u32 arm_to_mips_reg[];

#define arm_reg_a0 15
#define arm_reg_a1 16
#define arm_reg_a2 17

/* The code being generated is written through this pointer (the translate
 * functions keep it in a local called translation_ptr); the instruction
 * functions of riscv_asm.h take it by address. */
#define RV (&translation_ptr)

/* ==================================================================
 * Core code generation macros
 * ================================================================== */

#define generate_load_reg(ireg, reg_index)                                     \
  rv_mv(RV, ireg, arm_to_mips_reg[reg_index])

#define generate_store_reg(ireg, reg_index)                                    \
  rv_mv(RV, arm_to_mips_reg[reg_index], ireg)

#define generate_load_imm(ireg, imm) rv_li(RV, ireg, (u32)(imm))

#define generate_load_pc(ireg, new_pc)                                         \
  {                                                                            \
    s32 _delta = (s32)((new_pc) - (stored_pc));                                \
    if (rv_imm12_fits(_delta)) {                                               \
      rv_addi(RV, ireg, reg_pc, _delta);                                       \
    } else {                                                                   \
      generate_load_imm(ireg, (new_pc));                                       \
    }                                                                          \
  }

#define generate_mov(ireg_dest, ireg_src) rv_mv(RV, ireg_dest, ireg_src)

/* ==================================================================
 * ALU immediate helpers
 *
 * RISC-V addi/andi/ori/xori have 12-bit signed immediates.
 * If the immediate doesn't fit, load into reg_temp first.
 * ================================================================== */

/* rd = rs <op> imm, op one of RV_ADD, RV_AND, RV_OR, RV_XOR */
#define generate_alu_imm(op, ireg_dest, ireg_src, imm)                         \
  rv_alu_imm(RV, op, ireg_dest, ireg_src, (s32)(imm), reg_temp)

/* rd = rs + imm for the cycle counter */
#define rv_addi_safe(rd, rs, imm)                                              \
  rv_alu_imm(RV, RV_ADD, rd, rs, (s32)(imm), reg_temp)

/* ==================================================================
 * Multiply
 * ================================================================== */

/* For multiply_long: compute 64-bit product directly using mul/mulh */
#define generate_multiply_s64()                                                \
  rv_mul(RV, reg_a0, arm_to_mips_reg[rm], arm_to_mips_reg[rs]);                \
  rv_mulh(RV, reg_a1, arm_to_mips_reg[rm], arm_to_mips_reg[rs])

#define generate_multiply_u64()                                                \
  rv_mul(RV, reg_a0, arm_to_mips_reg[rm], arm_to_mips_reg[rs]);                \
  rv_mulhu(RV, reg_a1, arm_to_mips_reg[rm], arm_to_mips_reg[rs])

/* For multiply-accumulate, we add to rdlo:rdhi */
#define generate_multiply_s64_add()                                            \
  rv_mul(RV, reg_temp, arm_to_mips_reg[rm], arm_to_mips_reg[rs]);              \
  rv_mulh(RV, reg_rv, arm_to_mips_reg[rm], arm_to_mips_reg[rs]);               \
  rv_add(RV, arm_to_mips_reg[rdlo], arm_to_mips_reg[rdlo], reg_temp);          \
  rv_sltu(RV, reg_temp, arm_to_mips_reg[rdlo], reg_temp);                      \
  rv_add(RV, arm_to_mips_reg[rdhi], arm_to_mips_reg[rdhi], reg_rv);            \
  rv_add(RV, arm_to_mips_reg[rdhi], arm_to_mips_reg[rdhi], reg_temp)

#define generate_multiply_u64_add()                                            \
  rv_mul(RV, reg_temp, arm_to_mips_reg[rm], arm_to_mips_reg[rs]);              \
  rv_mulhu(RV, reg_rv, arm_to_mips_reg[rm], arm_to_mips_reg[rs]);              \
  rv_add(RV, arm_to_mips_reg[rdlo], arm_to_mips_reg[rdlo], reg_temp);          \
  rv_sltu(RV, reg_temp, arm_to_mips_reg[rdlo], reg_temp);                      \
  rv_add(RV, arm_to_mips_reg[rdhi], arm_to_mips_reg[rdhi], reg_rv);            \
  rv_add(RV, arm_to_mips_reg[rdhi], arm_to_mips_reg[rdhi], reg_temp)

/* ==================================================================
 * Function calls
 *
 * RISC-V has no absolute jump; use lui+jalr for all far calls.
 * No delay slots — arguments set up BEFORE the call.
 * ================================================================== */

#define genccall(fnptr) rv_call_abs(RV, rv_ra, reg_temp, (u32)(fnptr))

#define generate_function_call(function_location) genccall(function_location)

/* On MIPS, swap_delay moves the prior instruction into the delay slot.
 * On RISC-V, there are no delay slots — just call normally. */
#define generate_function_call_swap_delay(function_location)                   \
  generate_function_call(function_location)

#define generate_function_return_swap_delay() rv_ret(RV)

/* No-op on RISC-V (MIPS delay slot reordering) */
#define generate_swap_delay()

#define generate_raw_u32(value) rv_put(RV, (u32)(value))

/* ==================================================================
 * Cycle counter updates
 * ================================================================== */

#define generate_cycle_update()                                                \
  if (cycle_count != 0) {                                                      \
    rv_addi_safe(reg_cycles, reg_cycles, -(s32)cycle_count);                   \
    cycle_count = 0;                                                           \
  }

#define generate_cycle_update_force()                                          \
  rv_addi_safe(reg_cycles, reg_cycles, -(s32)cycle_count);                     \
  cycle_count = 0

/* ==================================================================
 * Branch patching
 *
 * Conditional: B-type branch (±4KB), patch entire instruction
 * Unconditional: lui+jalr pair (full 32-bit range), patch both
 * ================================================================== */

#define generate_branch_patch_conditional(dest, offset)                        \
  rv_patch_branch(dest, offset)

#define generate_branch_patch_unconditional(dest, offset)                      \
  rv_patch_jump_abs(dest, reg_temp, (u32)(offset))

/* ==================================================================
 * Branch generation
 * ================================================================== */

#define generate_branch_no_cycle_update(writeback_location, new_pc)            \
  if (pc == idle_loop_target_pc) {                                             \
    generate_load_pc(reg_a0, new_pc);                                          \
    rv_addi(RV, reg_cycles, rv_zero, 0);                                       \
    generate_function_call(riscv_update_gba);                                  \
    writeback_location = rv_jump_filler(RV, reg_temp);                         \
  } else {                                                                     \
    generate_load_pc(reg_a0, new_pc);                                          \
    /* If cycles < 0, call update trampoline */                                \
    rv_bge(RV, reg_cycles, rv_zero, 8);                                        \
    rv_jal(RV, rv_ra, (s32)(update_trampoline - translation_ptr));             \
    writeback_location = rv_jump_filler(RV, reg_temp);                         \
  }

#define generate_branch_cycle_update(writeback_location, new_pc)               \
  generate_cycle_update();                                                     \
  generate_branch_no_cycle_update(writeback_location, new_pc)

/* Indirect branches: a0 holds the GBA destination address */
#define generate_indirect_branch_cycle_update(type)                            \
  generate_cycle_update_force();                                               \
  generate_function_call(riscv_indirect_branch_##type)

#define generate_indirect_branch_no_cycle_update(type)                         \
  generate_function_call(riscv_indirect_branch_##type)

/* ==================================================================
 * Block prologue
 *
 * Layout (8 bytes):
 *   [0]: lui  reg_temp, hi(riscv_update_gba)
 *   [4]: jalr zero, reg_temp, lo(riscv_update_gba)
 * Then generate_load_imm for stored_pc (not counted in prologue size)
 * ================================================================== */

#define block_prologue_size 8

#define generate_block_prologue()                                              \
  update_trampoline = translation_ptr;                                         \
  rv_call_abs(RV, rv_zero, reg_temp, (u32)riscv_update_gba);                   \
  /* stored_pc is always the two instructions LUI+ADDI, never a single ADDI */                                                                            \
  rv_lui(RV, reg_pc, ((u32)stored_pc + 0x800) >> 12);                          \
  rv_addi(RV, reg_pc, reg_pc, (u32)stored_pc & 0xFFF)

#define generate_block_extra_vars()                                            \
  u32 stored_pc = pc;                                                          \
  u8 *update_trampoline

#define generate_block_extra_vars_arm() generate_block_extra_vars()

#define generate_block_extra_vars_thumb() generate_block_extra_vars()

/* ==================================================================
 * Flag check macros (compile-time, from flag_status)
 * ================================================================== */

#define check_generate_n_flag (flag_status & 0x08)
#define check_generate_z_flag (flag_status & 0x04)
#define check_generate_c_flag (flag_status & 0x02)
#define check_generate_v_flag (flag_status & 0x01)

/* ==================================================================
 * Register load/store with PC handling
 * ================================================================== */

#define generate_load_reg_pc(ireg, reg_index, pc_offset)                       \
  if (reg_index == REG_PC) {                                                   \
    generate_load_pc(ireg, (pc + pc_offset));                                  \
  } else {                                                                     \
    generate_load_reg(ireg, reg_index);                                        \
  }

#define check_load_reg_pc(arm_reg, reg_index, pc_offset)                       \
  if (reg_index == REG_PC) {                                                   \
    reg_index = arm_reg;                                                       \
    generate_load_pc(arm_to_mips_reg[arm_reg], (pc + pc_offset));              \
  }

#define check_store_reg_pc_no_flags(reg_index)                                 \
  if (reg_index == REG_PC) {                                                   \
    generate_indirect_branch_arm();                                            \
  }

#define check_store_reg_pc_flags(reg_index)                                    \
  if (reg_index == REG_PC) {                                                   \
    generate_function_call(execute_spsr_restore);                              \
    generate_indirect_branch_dual();                                           \
  }

/* ==================================================================
 * Bit manipulation helpers (no ins/ext on RISC-V)
 * ================================================================== */

#define extract_bits(rt, rs, pos, size)                                        \
  rv_slli(RV, rt, rs, 32 - ((pos) + (size)));                                  \
  rv_srli(RV, rt, rt, 32 - (size))

#define insert_bits(rdest, rsrc, rtemp, pos, size)                             \
  rv_slli(RV, rtemp, rsrc, 32 - (size));                                       \
  rv_srli(RV, rtemp, rtemp, 32 - (size) - (pos));                              \
  rv_or(RV, rdest, rdest, rtemp)

#define emit_align_reg(regv, numbits)                                          \
  rv_srli(RV, regv, regv, numbits);                                            \
  rv_slli(RV, regv, regv, numbits)

#define double_byte(regv, rtmp)                                                \
  rv_slli(RV, rtmp, regv, 8);                                                  \
  rv_andi(RV, regv, regv, 0xFF);                                               \
  rv_or(RV, regv, regv, rtmp)

#define extend_byte_signed(rd, rs)                                             \
  rv_slli(RV, rd, rs, 24);                                                     \
  rv_srai(RV, rd, rd, 24)

#define rotate_right(rdest, rsrc, rtemp, amount)                               \
  rv_slli(RV, rtemp, rsrc, 32 - (amount));                                     \
  rv_srli(RV, rdest, rsrc, (amount));                                          \
  rv_or(RV, rdest, rdest, rtemp)

#define rotate_right_var(rdest, rsrc, rtemp, ramount)                          \
  rv_andi(RV, rtemp, ramount, 0x1F);                                           \
  rv_srl(RV, rdest, rsrc, rtemp);                                              \
  rv_sub(RV, rtemp, rv_zero, rtemp);                                           \
  rv_addi(RV, rtemp, rtemp, 32);                                               \
  rv_sll(RV, rtemp, rsrc, rtemp);                                              \
  rv_or(RV, rdest, rdest, rtemp)

/* ==================================================================
 * Conditional move emulation (RISC-V has no movz/movn)
 * ================================================================== */

/* ==================================================================
 * Shift macros — immediate, no flags
 * ================================================================== */

#define generate_shift_imm_lsl_no_flags(arm_reg, _rm, _shift)                  \
  check_load_reg_pc(arm_reg, _rm, 8);                                          \
  if (_shift != 0) {                                                           \
    rv_slli(RV, arm_to_mips_reg[arm_reg], arm_to_mips_reg[_rm], _shift);       \
    _rm = arm_reg;                                                             \
  }

#define generate_shift_imm_lsr_no_flags(arm_reg, _rm, _shift)                  \
  if (_shift != 0) {                                                           \
    check_load_reg_pc(arm_reg, _rm, 8);                                        \
    rv_srli(RV, arm_to_mips_reg[arm_reg], arm_to_mips_reg[_rm], _shift);       \
  } else {                                                                     \
    rv_addi(RV, arm_to_mips_reg[arm_reg], rv_zero, 0);                         \
  }                                                                            \
  _rm = arm_reg

#define generate_shift_imm_asr_no_flags(arm_reg, _rm, _shift)                  \
  check_load_reg_pc(arm_reg, _rm, 8);                                          \
  if (_shift != 0) {                                                           \
    rv_srai(RV, arm_to_mips_reg[arm_reg], arm_to_mips_reg[_rm], _shift);       \
  } else {                                                                     \
    rv_srai(RV, arm_to_mips_reg[arm_reg], arm_to_mips_reg[_rm], 31);           \
  }                                                                            \
  _rm = arm_reg

#define generate_shift_imm_ror_no_flags(arm_reg, _rm, _shift)                  \
  check_load_reg_pc(arm_reg, _rm, 8);                                          \
  if (_shift != 0) {                                                           \
    rotate_right(arm_to_mips_reg[arm_reg], arm_to_mips_reg[_rm], reg_temp,     \
                 _shift);                                                      \
  } else { /* RRX: rotate right through carry */                               \
    rv_srli(RV, arm_to_mips_reg[arm_reg], arm_to_mips_reg[_rm], 1);            \
    insert_bits(arm_to_mips_reg[arm_reg], reg_c_cache, reg_temp, 31, 1);       \
  }                                                                            \
  _rm = arm_reg

/* ==================================================================
 * Shift macros — immediate, with flags (update C flag)
 * ================================================================== */

#define generate_shift_imm_lsl_flags(arm_reg, _rm, _shift)                     \
  check_load_reg_pc(arm_reg, _rm, 8);                                          \
  if (_shift != 0) {                                                           \
    extract_bits(reg_c_cache, arm_to_mips_reg[_rm], (32 - _shift), 1);         \
    rv_slli(RV, arm_to_mips_reg[arm_reg], arm_to_mips_reg[_rm], _shift);       \
    _rm = arm_reg;                                                             \
  }

#define generate_shift_imm_lsr_flags(arm_reg, _rm, _shift)                     \
  check_load_reg_pc(arm_reg, _rm, 8);                                          \
  if (_shift != 0) {                                                           \
    extract_bits(reg_c_cache, arm_to_mips_reg[_rm], (_shift - 1), 1);          \
    rv_srli(RV, arm_to_mips_reg[arm_reg], arm_to_mips_reg[_rm], _shift);       \
  } else {                                                                     \
    rv_srli(RV, reg_c_cache, arm_to_mips_reg[_rm], 31);                        \
    rv_addi(RV, arm_to_mips_reg[arm_reg], rv_zero, 0);                         \
  }                                                                            \
  _rm = arm_reg

#define generate_shift_imm_asr_flags(arm_reg, _rm, _shift)                     \
  check_load_reg_pc(arm_reg, _rm, 8);                                          \
  if (_shift != 0) {                                                           \
    extract_bits(reg_c_cache, arm_to_mips_reg[_rm], (_shift - 1), 1);          \
    rv_srai(RV, arm_to_mips_reg[arm_reg], arm_to_mips_reg[_rm], _shift);       \
  } else {                                                                     \
    rv_srai(RV, arm_to_mips_reg[arm_reg], arm_to_mips_reg[_rm], 31);           \
    rv_andi(RV, reg_c_cache, arm_to_mips_reg[arm_reg], 1);                     \
  }                                                                            \
  _rm = arm_reg

#define generate_shift_imm_ror_flags(arm_reg, _rm, _shift)                     \
  check_load_reg_pc(arm_reg, _rm, 8);                                          \
  if (_shift != 0) {                                                           \
    extract_bits(reg_c_cache, arm_to_mips_reg[_rm], (_shift - 1), 1);          \
    rotate_right(arm_to_mips_reg[arm_reg], arm_to_mips_reg[_rm], reg_temp,     \
                 _shift);                                                      \
  } else { /* RRX with carry update */                                         \
    rv_slli(RV, reg_temp, reg_c_cache, 31);                                    \
    rv_andi(RV, reg_c_cache, arm_to_mips_reg[_rm], 1);                         \
    rv_srli(RV, arm_to_mips_reg[arm_reg], arm_to_mips_reg[_rm], 1);            \
    rv_or(RV, arm_to_mips_reg[arm_reg], arm_to_mips_reg[arm_reg], reg_temp);   \
  }                                                                            \
  _rm = arm_reg

/* ==================================================================
 * Shift macros — register, no flags
 *
 * ARM shifts by register: if shift >= 32, result depends on type.
 * RISC-V SLL/SRL/SRA only use lower 5 bits (mod 32).
 * Must explicitly handle shift >= 32.
 * ================================================================== */

#define generate_shift_reg_lsl_no_flags(_rm, _rs)                              \
  rv_sll(RV, reg_a0, arm_to_mips_reg[_rm], arm_to_mips_reg[_rs]);              \
  rv_sltiu(RV, reg_temp, arm_to_mips_reg[_rs], 32);                            \
  rv_movz(RV, reg_a0, rv_zero, reg_temp)

#define generate_shift_reg_lsr_no_flags(_rm, _rs)                              \
  rv_srl(RV, reg_a0, arm_to_mips_reg[_rm], arm_to_mips_reg[_rs]);              \
  rv_sltiu(RV, reg_temp, arm_to_mips_reg[_rs], 32);                            \
  rv_movz(RV, reg_a0, rv_zero, reg_temp)

#define generate_shift_reg_asr_no_flags(_rm, _rs)                              \
  rv_sra(RV, reg_a0, arm_to_mips_reg[_rm], arm_to_mips_reg[_rs]);              \
  rv_sltiu(RV, reg_temp, arm_to_mips_reg[_rs], 32);                            \
  /* If shift >= 32, ASR gives sign-fill (bit 31 replicated) */                \
  rv_bne(RV, reg_temp, rv_zero, 8);                                            \
  rv_srai(RV, reg_a0, arm_to_mips_reg[_rm], 31)

#define generate_shift_reg_ror_no_flags(_rm, _rs)                              \
  rotate_right_var(reg_a0, arm_to_mips_reg[_rm], reg_temp, arm_to_mips_reg[_rs])

/* ==================================================================
 * Shift macros — register, with flags
 * ================================================================== */

#define generate_shift_reg_lsl_flags(_rm, _rs)                                 \
  {                                                                            \
    u32 shift_reg = _rs;                                                       \
    check_load_reg_pc(arm_reg_a1, shift_reg, 8);                               \
    generate_load_reg_pc(reg_a0, _rm, 12);                                     \
    /* If shift == 0, keep result and flags unchanged */                       \
    rv_beq(RV, arm_to_mips_reg[shift_reg], rv_zero, 7 * 4);                    \
    rv_addi(RV, reg_temp, arm_to_mips_reg[shift_reg], -1);                     \
    rv_sll(RV, reg_a0, reg_a0, reg_temp);                                      \
    rv_srli(RV, reg_c_cache, reg_a0, 31);                                      \
    rv_sltiu(RV, reg_temp, arm_to_mips_reg[shift_reg], 33);                    \
    rv_slli(RV, reg_a0, reg_a0, 1);                                            \
    rv_movz(RV, reg_c_cache, rv_zero, reg_temp);                               \
    rv_movz(RV, reg_a0, rv_zero, reg_temp);                                    \
  }

#define generate_shift_reg_lsr_flags(_rm, _rs)                                 \
  {                                                                            \
    u32 shift_reg = _rs;                                                       \
    check_load_reg_pc(arm_reg_a1, shift_reg, 8);                               \
    generate_load_reg_pc(reg_a0, _rm, 12);                                     \
    rv_beq(RV, arm_to_mips_reg[shift_reg], rv_zero, 7 * 4);                    \
    rv_addi(RV, reg_temp, arm_to_mips_reg[shift_reg], -1);                     \
    rv_srl(RV, reg_a0, reg_a0, reg_temp);                                      \
    rv_andi(RV, reg_c_cache, reg_a0, 1);                                       \
    rv_sltiu(RV, reg_temp, arm_to_mips_reg[shift_reg], 33);                    \
    rv_srli(RV, reg_a0, reg_a0, 1);                                            \
    rv_movz(RV, reg_c_cache, rv_zero, reg_temp);                               \
    rv_movz(RV, reg_a0, rv_zero, reg_temp);                                    \
  }

#define generate_shift_reg_asr_flags(_rm, _rs)                                 \
  generate_load_reg_pc(reg_a1, _rs, 8);                                        \
  generate_load_reg_pc(reg_a0, _rm, 12);                                       \
  rv_beq(RV, reg_a1, rv_zero, 7 * 4);                                          \
  /* Cap shift at 32 */                                                        \
  rv_addi(RV, reg_temp, rv_zero, 32);                                          \
  rv_srli(RV, reg_rv, reg_a1, 5);                                              \
  rv_movn(RV, reg_a1, reg_temp, reg_rv);                                       \
  rv_addi(RV, reg_temp, reg_a1, -1);                                           \
  rv_sra(RV, reg_a0, reg_a0, reg_temp);                                        \
  rv_andi(RV, reg_c_cache, reg_a0, 1);                                         \
  rv_srai(RV, reg_a0, reg_a0, 1)

#define generate_shift_reg_ror_flags(_rm, _rs)                                 \
  rv_beq(RV, arm_to_mips_reg[_rs], rv_zero, 4 * 4);                            \
  rv_addi(RV, reg_temp, arm_to_mips_reg[_rs], -1);                             \
  rv_srl(RV, reg_temp, arm_to_mips_reg[_rm], reg_temp);                        \
  rv_andi(RV, reg_c_cache, reg_temp, 1);                                       \
  rotate_right_var(reg_a0, arm_to_mips_reg[_rm], reg_temp, arm_to_mips_reg[_rs])

/* ==================================================================
 * Shift dispatchers
 * ================================================================== */

#define generate_shift_imm(arm_reg, name, flags_op)                            \
  u32 shift = (opcode >> 7) & 0x1F;                                            \
  generate_shift_imm_##name##_##flags_op(arm_reg, rm, shift)

#define generate_shift_reg(arm_reg, name, flags_op)                            \
  u32 rs = ((opcode >> 8) & 0x0F);                                             \
  generate_shift_reg_##name##_##flags_op(rm, rs);                              \
  rm = arm_reg

#define generate_load_rm_sh(rm, flags_op)                                      \
  {                                                                            \
    switch ((opcode >> 4) & 0x07) {                                            \
    case 0x0: {                                                                \
      generate_shift_imm(arm_reg_a0, lsl, flags_op);                           \
      break;                                                                   \
    }                                                                          \
    case 0x1: {                                                                \
      generate_shift_reg(arm_reg_a0, lsl, flags_op);                           \
      break;                                                                   \
    }                                                                          \
    case 0x2: {                                                                \
      generate_shift_imm(arm_reg_a0, lsr, flags_op);                           \
      break;                                                                   \
    }                                                                          \
    case 0x3: {                                                                \
      generate_shift_reg(arm_reg_a0, lsr, flags_op);                           \
      break;                                                                   \
    }                                                                          \
    case 0x4: {                                                                \
      generate_shift_imm(arm_reg_a0, asr, flags_op);                           \
      break;                                                                   \
    }                                                                          \
    case 0x5: {                                                                \
      generate_shift_reg(arm_reg_a0, asr, flags_op);                           \
      break;                                                                   \
    }                                                                          \
    case 0x6: {                                                                \
      generate_shift_imm(arm_reg_a0, ror, flags_op);                           \
      break;                                                                   \
    }                                                                          \
    case 0x7: {                                                                \
      generate_shift_reg(arm_reg_a0, ror, flags_op);                           \
      break;                                                                   \
    }                                                                          \
    }                                                                          \
  }

#define generate_load_offset_sh(rm)                                            \
  {                                                                            \
    switch ((opcode >> 5) & 0x03) {                                            \
    case 0x0: {                                                                \
      generate_shift_imm(arm_reg_a1, lsl, no_flags);                           \
      break;                                                                   \
    }                                                                          \
    case 0x1: {                                                                \
      generate_shift_imm(arm_reg_a1, lsr, no_flags);                           \
      break;                                                                   \
    }                                                                          \
    case 0x2: {                                                                \
      generate_shift_imm(arm_reg_a1, asr, no_flags);                           \
      break;                                                                   \
    }                                                                          \
    case 0x3: {                                                                \
      generate_shift_imm(arm_reg_a1, ror, no_flags);                           \
      break;                                                                   \
    }                                                                          \
    }                                                                          \
  }

/* ==================================================================
 * Indirect branch wrappers
 * ================================================================== */

#define generate_indirect_branch_arm()                                         \
  {                                                                            \
    if (condition == 0x0E) {                                                   \
      generate_indirect_branch_cycle_update(arm);                              \
    } else {                                                                   \
      generate_indirect_branch_no_cycle_update(arm);                           \
    }                                                                          \
  }

#define generate_indirect_branch_dual()                                        \
  {                                                                            \
    if (condition == 0x0E) {                                                   \
      generate_indirect_branch_cycle_update(dual);                             \
    } else {                                                                   \
      generate_indirect_branch_no_cycle_update(dual);                          \
    }                                                                          \
  }

/* ==================================================================
 * Condition macros
 *
 * These branch to SKIP the conditional code if the condition is
 * NOT met (opposite sense). The branch filler is patched later.
 *
 * On MIPS, cycle update was in the delay slot (always executed).
 * On RISC-V, put it before the branch (same effect).
 * ================================================================== */

#define generate_condition_eq()                                                \
  generate_cycle_update_force();                                               \
  backpatch_address = rv_beq(RV, reg_z_cache, rv_zero, 0)

#define generate_condition_ne()                                                \
  generate_cycle_update_force();                                               \
  backpatch_address = rv_bne(RV, reg_z_cache, rv_zero, 0)

#define generate_condition_cs()                                                \
  generate_cycle_update_force();                                               \
  backpatch_address = rv_beq(RV, reg_c_cache, rv_zero, 0)

#define generate_condition_cc()                                                \
  generate_cycle_update_force();                                               \
  backpatch_address = rv_bne(RV, reg_c_cache, rv_zero, 0)

#define generate_condition_mi()                                                \
  generate_cycle_update_force();                                               \
  backpatch_address = rv_beq(RV, reg_n_cache, rv_zero, 0)

#define generate_condition_pl()                                                \
  generate_cycle_update_force();                                               \
  backpatch_address = rv_bne(RV, reg_n_cache, rv_zero, 0)

#define generate_condition_vs()                                                \
  generate_cycle_update_force();                                               \
  backpatch_address = rv_beq(RV, reg_v_cache, rv_zero, 0)

#define generate_condition_vc()                                                \
  generate_cycle_update_force();                                               \
  backpatch_address = rv_bne(RV, reg_v_cache, rv_zero, 0)

#define generate_condition_hi()                                                \
  generate_cycle_update_force();                                               \
  rv_xori(RV, reg_temp, reg_c_cache, 1);                                       \
  rv_or(RV, reg_temp, reg_temp, reg_z_cache);                                  \
  backpatch_address = rv_bne(RV, reg_temp, rv_zero, 0)

#define generate_condition_ls()                                                \
  generate_cycle_update_force();                                               \
  rv_xori(RV, reg_temp, reg_c_cache, 1);                                       \
  rv_or(RV, reg_temp, reg_temp, reg_z_cache);                                  \
  backpatch_address = rv_beq(RV, reg_temp, rv_zero, 0)

#define generate_condition_ge()                                                \
  generate_cycle_update_force();                                               \
  backpatch_address = rv_bne(RV, reg_n_cache, reg_v_cache, 0)

#define generate_condition_lt()                                                \
  generate_cycle_update_force();                                               \
  backpatch_address = rv_beq(RV, reg_n_cache, reg_v_cache, 0)

#define generate_condition_gt()                                                \
  generate_cycle_update_force();                                               \
  rv_xor(RV, reg_temp, reg_n_cache, reg_v_cache);                              \
  rv_or(RV, reg_temp, reg_temp, reg_z_cache);                                  \
  backpatch_address = rv_bne(RV, reg_temp, rv_zero, 0)

#define generate_condition_le()                                                \
  generate_cycle_update_force();                                               \
  rv_xor(RV, reg_temp, reg_n_cache, reg_v_cache);                              \
  rv_or(RV, reg_temp, reg_temp, reg_z_cache);                                  \
  backpatch_address = rv_beq(RV, reg_temp, rv_zero, 0)

#define generate_condition()                                                   \
  switch (condition) {                                                         \
  case 0x0:                                                                    \
    generate_condition_eq();                                                   \
    break;                                                                     \
  case 0x1:                                                                    \
    generate_condition_ne();                                                   \
    break;                                                                     \
  case 0x2:                                                                    \
    generate_condition_cs();                                                   \
    break;                                                                     \
  case 0x3:                                                                    \
    generate_condition_cc();                                                   \
    break;                                                                     \
  case 0x4:                                                                    \
    generate_condition_mi();                                                   \
    break;                                                                     \
  case 0x5:                                                                    \
    generate_condition_pl();                                                   \
    break;                                                                     \
  case 0x6:                                                                    \
    generate_condition_vs();                                                   \
    break;                                                                     \
  case 0x7:                                                                    \
    generate_condition_vc();                                                   \
    break;                                                                     \
  case 0x8:                                                                    \
    generate_condition_hi();                                                   \
    break;                                                                     \
  case 0x9:                                                                    \
    generate_condition_ls();                                                   \
    break;                                                                     \
  case 0xA:                                                                    \
    generate_condition_ge();                                                   \
    break;                                                                     \
  case 0xB:                                                                    \
    generate_condition_lt();                                                   \
    break;                                                                     \
  case 0xC:                                                                    \
    generate_condition_gt();                                                   \
    break;                                                                     \
  case 0xD:                                                                    \
    generate_condition_le();                                                   \
    break;                                                                     \
  default:                                                                     \
    break;                                                                     \
  }

#define generate_branch()                                                      \
  {                                                                            \
    if (condition == 0x0E) {                                                   \
      generate_branch_cycle_update(                                            \
          block_exits[block_exit_position].branch_source,                      \
          block_exits[block_exit_position].branch_target);                     \
    } else {                                                                   \
      generate_branch_no_cycle_update(                                         \
          block_exits[block_exit_position].branch_source,                      \
          block_exits[block_exit_position].branch_target);                     \
    }                                                                          \
    block_exit_position++;                                                     \
  }

/* ==================================================================
 * ALU operations — register operand
 * ================================================================== */

#define generate_op_and_reg(_rd, _rn, _rm) rv_and(RV, _rd, _rn, _rm)

#define generate_op_orr_reg(_rd, _rn, _rm) rv_or(RV, _rd, _rn, _rm)

#define generate_op_eor_reg(_rd, _rn, _rm) rv_xor(RV, _rd, _rn, _rm)

#define generate_op_bic_reg(_rd, _rn, _rm)                                     \
  rv_not(RV, reg_temp, _rm);                                                   \
  rv_and(RV, _rd, _rn, reg_temp)

#define generate_op_sub_reg(_rd, _rn, _rm) rv_sub(RV, _rd, _rn, _rm)

#define generate_op_rsb_reg(_rd, _rn, _rm) rv_sub(RV, _rd, _rm, _rn)

#define generate_op_sbc_reg(_rd, _rn, _rm)                                     \
  rv_sub(RV, _rd, _rn, _rm);                                                   \
  rv_xori(RV, reg_temp, reg_c_cache, 1);                                       \
  rv_sub(RV, _rd, _rd, reg_temp)

#define generate_op_rsc_reg(_rd, _rn, _rm)                                     \
  rv_add(RV, reg_temp, _rm, reg_c_cache);                                      \
  rv_addi(RV, reg_temp, reg_temp, -1);                                         \
  rv_sub(RV, _rd, reg_temp, _rn)

#define generate_op_add_reg(_rd, _rn, _rm) rv_add(RV, _rd, _rn, _rm)

#define generate_op_adc_reg(_rd, _rn, _rm)                                     \
  rv_add(RV, reg_temp, _rm, reg_c_cache);                                      \
  rv_add(RV, _rd, _rn, reg_temp)

#define generate_op_mov_reg(_rd, _rn, _rm) rv_mv(RV, _rd, _rm)

#define generate_op_mvn_reg(_rd, _rn, _rm) rv_not(RV, _rd, _rm)

/* ==================================================================
 * ALU operations — immediate operand
 * ================================================================== */

#define generate_op_imm_wrapper(name, _rd, _rn)                                \
  if (imm != 0) {                                                              \
    generate_load_imm(reg_a0, imm);                                            \
    generate_op_##name##_reg(_rd, _rn, reg_a0);                                \
  } else {                                                                     \
    generate_op_##name##_reg(_rd, _rn, rv_zero);                               \
  }

#define generate_op_and_imm(_rd, _rn) generate_alu_imm(RV_AND, _rd, _rn, imm)

#define generate_op_orr_imm(_rd, _rn) generate_alu_imm(RV_OR, _rd, _rn, imm)

#define generate_op_eor_imm(_rd, _rn) generate_alu_imm(RV_XOR, _rd, _rn, imm)

#define generate_op_bic_imm(_rd, _rn) generate_alu_imm(RV_AND, _rd, _rn, (~imm))

#define generate_op_sub_imm(_rd, _rn)                                          \
  generate_alu_imm(RV_ADD, _rd, _rn, (-(s32)imm))

#define generate_op_rsb_imm(_rd, _rn)                                          \
  if (imm != 0) {                                                              \
    generate_load_imm(reg_temp, imm);                                          \
    rv_sub(RV, _rd, reg_temp, _rn);                                            \
  } else {                                                                     \
    rv_sub(RV, _rd, rv_zero, _rn);                                             \
  }

#define generate_op_sbc_imm(_rd, _rn) generate_op_imm_wrapper(sbc, _rd, _rn)

#define generate_op_rsc_imm(_rd, _rn) generate_op_imm_wrapper(rsc, _rd, _rn)

#define generate_op_add_imm(_rd, _rn) generate_alu_imm(RV_ADD, _rd, _rn, imm)

#define generate_op_adc_imm(_rd, _rn) generate_op_imm_wrapper(adc, _rd, _rn)

#define generate_op_mov_imm(_rd, _rn) generate_load_imm(_rd, imm)

#define generate_op_mvn_imm(_rd, _rn) generate_load_imm(_rd, (~imm))

/* ==================================================================
 * Logic flags: N and Z from result
 * ================================================================== */

#define generate_op_logic_flags(_rd)                                           \
  if (check_generate_n_flag) {                                                 \
    rv_srli(RV, reg_n_cache, _rd, 31);                                         \
  }                                                                            \
  if (check_generate_z_flag) {                                                 \
    rv_sltiu(RV, reg_z_cache, _rd, 1);                                         \
  }

/* ==================================================================
 * Flag-setting ALU — register operand
 * ================================================================== */

#define generate_op_ands_reg(_rd, _rn, _rm)                                    \
  rv_and(RV, _rd, _rn, _rm);                                                   \
  generate_op_logic_flags(_rd)

#define generate_op_orrs_reg(_rd, _rn, _rm)                                    \
  rv_or(RV, _rd, _rn, _rm);                                                    \
  generate_op_logic_flags(_rd)

#define generate_op_eors_reg(_rd, _rn, _rm)                                    \
  rv_xor(RV, _rd, _rn, _rm);                                                   \
  generate_op_logic_flags(_rd)

#define generate_op_bics_reg(_rd, _rn, _rm)                                    \
  rv_not(RV, reg_temp, _rm);                                                   \
  rv_and(RV, _rd, _rn, reg_temp);                                              \
  generate_op_logic_flags(_rd)

#define generate_op_subs_reg(_rd, _rn, _rm)                                    \
  if (check_generate_c_flag) {                                                 \
    rv_sltu(RV, reg_c_cache, _rn, _rm);                                        \
    rv_xori(RV, reg_c_cache, reg_c_cache, 1);                                  \
  }                                                                            \
  if (check_generate_v_flag) {                                                 \
    rv_slt(RV, reg_v_cache, _rn, _rm);                                         \
  }                                                                            \
  rv_sub(RV, _rd, _rn, _rm);                                                   \
  generate_op_logic_flags(_rd);                                                \
  if (check_generate_v_flag) {                                                 \
    if (!check_generate_n_flag) {                                              \
      rv_srli(RV, reg_n_cache, _rd, 31);                                       \
    }                                                                          \
    rv_xor(RV, reg_v_cache, reg_v_cache, reg_n_cache);                         \
  }

#define generate_op_rsbs_reg(_rd, _rn, _rm) generate_op_subs_reg(_rd, _rm, _rn)

#define generate_op_sbcs_reg(_rd, _rn, _rm)                                    \
  rv_xori(RV, reg_temp, reg_c_cache, 1);                                       \
  if (check_generate_c_flag) {                                                 \
    rv_sltu(RV, reg_c_cache, _rm, _rn);                                        \
    rv_sltu(RV, reg_rv, _rn, _rm);                                             \
    rv_xori(RV, reg_rv, reg_rv, 1);                                            \
    rv_movz(RV, reg_c_cache, reg_rv, reg_temp);                                \
  }                                                                            \
  if (check_generate_v_flag) {                                                 \
    rv_xor(RV, reg_v_cache, _rn, _rm);                                         \
    rv_not(RV, reg_rv, _rm);                                                   \
  }                                                                            \
  rv_sub(RV, _rd, _rn, _rm);                                                   \
  rv_sub(RV, _rd, _rd, reg_temp);                                              \
  if (check_generate_v_flag) {                                                 \
    rv_xor(RV, reg_rv, reg_rv, _rd);                                           \
    rv_and(RV, reg_v_cache, reg_v_cache, reg_rv);                              \
    rv_srli(RV, reg_v_cache, reg_v_cache, 31);                                 \
  }                                                                            \
  generate_op_logic_flags(_rd)

#define generate_op_rscs_reg(_rd, _rn, _rm) generate_op_sbcs_reg(_rd, _rm, _rn)

#define generate_op_adds_reg(_rd, _rn, _rm)                                    \
  if (check_generate_c_flag | check_generate_v_flag) {                         \
    rv_mv(RV, reg_c_cache, _rn);                                               \
  }                                                                            \
  if (check_generate_v_flag) {                                                 \
    rv_slt(RV, reg_v_cache, _rm, rv_zero);                                     \
  }                                                                            \
  rv_add(RV, _rd, _rn, _rm);                                                   \
  if (check_generate_v_flag) {                                                 \
    rv_slt(RV, reg_a0, _rd, reg_c_cache);                                      \
    rv_xor(RV, reg_v_cache, reg_v_cache, reg_a0);                              \
  }                                                                            \
  if (check_generate_c_flag) {                                                 \
    rv_sltu(RV, reg_c_cache, _rd, reg_c_cache);                                \
  }                                                                            \
  generate_op_logic_flags(_rd)

#define generate_op_adcs_reg(_rd, _rn, _rm)                                    \
  if (check_generate_v_flag) {                                                 \
    rv_xor(RV, reg_v_cache, _rn, _rm);                                         \
    rv_not(RV, reg_v_cache, reg_v_cache);                                      \
    rv_mv(RV, reg_rv, _rn);                                                    \
  }                                                                            \
  rv_add(RV, reg_a2, _rn, _rm);                                                \
  if (check_generate_c_flag) {                                                 \
    rv_sltu(RV, reg_temp, reg_a2, _rm);                                        \
  }                                                                            \
  rv_add(RV, _rd, reg_a2, reg_c_cache);                                        \
  if (check_generate_v_flag) {                                                 \
    rv_xor(RV, reg_rv, reg_rv, _rd);                                           \
    rv_and(RV, reg_v_cache, reg_rv, reg_v_cache);                              \
    rv_srli(RV, reg_v_cache, reg_v_cache, 31);                                 \
  }                                                                            \
  if (check_generate_c_flag) {                                                 \
    rv_sltu(RV, reg_c_cache, _rd, reg_c_cache);                                \
    rv_or(RV, reg_c_cache, reg_temp, reg_c_cache);                             \
  }                                                                            \
  generate_op_logic_flags(_rd)

#define generate_op_movs_reg(_rd, _rn, _rm)                                    \
  rv_mv(RV, _rd, _rm);                                                         \
  generate_op_logic_flags(_rd)

#define generate_op_mvns_reg(_rd, _rn, _rm)                                    \
  rv_not(RV, _rd, _rm);                                                        \
  generate_op_logic_flags(_rd)

#define generate_op_neg_reg(_rd, _rn, _rm)                                     \
  generate_op_subs_reg(_rd, rv_zero, _rm)

#define generate_op_muls_reg(_rd, _rn, _rm)                                    \
  rv_mul(RV, _rd, _rn, _rm);                                                   \
  generate_op_logic_flags(_rd)

#define generate_op_cmp_reg(_rd, _rn, _rm)                                     \
  generate_op_subs_reg(reg_temp, _rn, _rm)

#define generate_op_cmn_reg(_rd, _rn, _rm)                                     \
  generate_op_adds_reg(reg_temp, _rn, _rm)

#define generate_op_tst_reg(_rd, _rn, _rm)                                     \
  generate_op_ands_reg(reg_temp, _rn, _rm)

#define generate_op_teq_reg(_rd, _rn, _rm)                                     \
  generate_op_eors_reg(reg_temp, _rn, _rm)

/* ==================================================================
 * Flag-setting ALU — immediate operand
 * ================================================================== */

#define generate_op_ands_imm(_rd, _rn)                                         \
  generate_alu_imm(RV_AND, _rd, _rn, imm);                                     \
  generate_op_logic_flags(_rd)

#define generate_op_orrs_imm(_rd, _rn)                                         \
  generate_alu_imm(RV_OR, _rd, _rn, imm);                                      \
  generate_op_logic_flags(_rd)

#define generate_op_eors_imm(_rd, _rn)                                         \
  generate_alu_imm(RV_XOR, _rd, _rn, imm);                                     \
  generate_op_logic_flags(_rd)

#define generate_op_bics_imm(_rd, _rn)                                         \
  generate_alu_imm(RV_AND, _rd, _rn, (~imm));                                  \
  generate_op_logic_flags(_rd)

#define generate_op_subs_imm(_rd, _rn) generate_op_imm_wrapper(subs, _rd, _rn)

#define generate_op_rsbs_imm(_rd, _rn) generate_op_imm_wrapper(rsbs, _rd, _rn)

#define generate_op_sbcs_imm(_rd, _rn) generate_op_imm_wrapper(sbcs, _rd, _rn)

#define generate_op_rscs_imm(_rd, _rn) generate_op_imm_wrapper(rscs, _rd, _rn)

#define generate_op_adds_imm(_rd, _rn) generate_op_imm_wrapper(adds, _rd, _rn)

#define generate_op_adcs_imm(_rd, _rn) generate_op_imm_wrapper(adcs, _rd, _rn)

#define generate_op_movs_imm(_rd, _rn)                                         \
  generate_load_imm(_rd, imm);                                                 \
  generate_op_logic_flags(_rd)

#define generate_op_mvns_imm(_rd, _rn)                                         \
  generate_load_imm(_rd, (~imm));                                              \
  generate_op_logic_flags(_rd)

#define generate_op_cmp_imm(_rd, _rn) generate_op_imm_wrapper(cmp, _rd, _rn)

#define generate_op_cmn_imm(_rd, _rn) generate_op_imm_wrapper(cmn, _rd, _rn)

#define generate_op_tst_imm(_rd, _rn) generate_op_ands_imm(reg_temp, _rn)

#define generate_op_teq_imm(_rd, _rn) generate_op_eors_imm(reg_temp, _rn)

/* ==================================================================
 * ARM data processing dispatch macros
 * ================================================================== */

#define arm_generate_op_load_yes() generate_load_reg_pc(reg_a1, rn, 8)

#define arm_generate_op_load_no()

#define arm_op_check_yes() check_load_reg_pc(arm_reg_a1, rn, 8)

#define arm_op_check_no()

#define arm_generate_op_reg_flags(name, load_op)                               \
  arm_decode_data_proc_reg(opcode);                                            \
  if (check_generate_c_flag) {                                                 \
    generate_load_rm_sh(rm, flags);                                            \
  } else {                                                                     \
    generate_load_rm_sh(rm, no_flags);                                         \
  }                                                                            \
  arm_op_check_##load_op();                                                    \
  generate_op_##name##_reg(arm_to_mips_reg[rd], arm_to_mips_reg[rn],           \
                           arm_to_mips_reg[rm])

#define arm_generate_op_reg(name, load_op)                                     \
  arm_decode_data_proc_reg(opcode);                                            \
  generate_load_rm_sh(rm, no_flags);                                           \
  arm_op_check_##load_op();                                                    \
  generate_op_##name##_reg(arm_to_mips_reg[rd], arm_to_mips_reg[rn],           \
                           arm_to_mips_reg[rm])

#define arm_generate_op_imm(name, load_op)                                     \
  arm_decode_data_proc_imm(opcode);                                            \
  ror(imm, imm, imm_ror);                                                      \
  arm_op_check_##load_op();                                                    \
  generate_op_##name##_imm(arm_to_mips_reg[rd], arm_to_mips_reg[rn])

#define arm_generate_op_imm_flags(name, load_op)                               \
  arm_decode_data_proc_imm(opcode);                                            \
  ror(imm, imm, imm_ror);                                                      \
  if (check_generate_c_flag && (imm_ror != 0)) {                               \
    rv_addi(RV, reg_c_cache, rv_zero, ((imm) >> 31));                          \
  }                                                                            \
  arm_op_check_##load_op();                                                    \
  generate_op_##name##_imm(arm_to_mips_reg[rd], arm_to_mips_reg[rn])

#define arm_data_proc(name, type, flags_op)                                    \
  {                                                                            \
    arm_generate_op_##type(name, yes);                                         \
    check_store_reg_pc_##flags_op(rd);                                         \
  }

#define arm_data_proc_test(name, type)                                         \
  {                                                                            \
    arm_generate_op_##type(name, yes);                                         \
  }

#define arm_data_proc_unary(name, type, flags_op)                              \
  {                                                                            \
    arm_generate_op_##type(name, no);                                          \
    check_store_reg_pc_##flags_op(rd);                                         \
  }

/* ==================================================================
 * ARM multiply
 * ================================================================== */

#define arm_multiply_flags_yes(_rd) generate_op_logic_flags(_rd)

#define arm_multiply_flags_no(_rd)

#define arm_multiply_add_no()                                                  \
  rv_mul(RV, arm_to_mips_reg[rd], arm_to_mips_reg[rm], arm_to_mips_reg[rs])

#define arm_multiply_add_yes()                                                 \
  rv_mul(RV, reg_temp, arm_to_mips_reg[rm], arm_to_mips_reg[rs]);              \
  rv_add(RV, arm_to_mips_reg[rd], reg_temp, arm_to_mips_reg[rn])

#define arm_multiply(add_op, flags)                                            \
  {                                                                            \
    arm_decode_multiply();                                                     \
    arm_multiply_add_##add_op();                                               \
    arm_multiply_flags_##flags(arm_to_mips_reg[rd]);                           \
  }

#define arm_multiply_long_flags_yes(_rdlo, _rdhi)                              \
  rv_sltiu(RV, reg_z_cache, _rdlo, 1);                                         \
  rv_sltiu(RV, reg_a0, _rdhi, 1);                                              \
  rv_and(RV, reg_z_cache, reg_z_cache, reg_a0);                                \
  rv_srli(RV, reg_n_cache, _rdhi, 31)

#define arm_multiply_long_flags_no(_rdlo, _rdhi)

/* For non-accumulate: compute product directly into rdlo/rdhi */
#define arm_multiply_long_add_yes(name) generate_multiply_##name()

#define arm_multiply_long_add_no(name) generate_multiply_##name()

#define arm_multiply_long(name, add_op, flags)                                 \
  {                                                                            \
    arm_decode_multiply_long();                                                \
    arm_multiply_long_add_##add_op(name);                                      \
    /* For non-accumulate variants, copy results from a0/a1 to rdlo/rdhi */    \
    rv_mv(RV, arm_to_mips_reg[rdlo], reg_a0);                                  \
    rv_mv(RV, arm_to_mips_reg[rdhi], reg_a1);                                  \
    arm_multiply_long_flags_##flags(arm_to_mips_reg[rdlo],                     \
                                    arm_to_mips_reg[rdhi]);                    \
  }

/* ==================================================================
 * PSR read/write
 * ================================================================== */

#define arm_psr_read(op_type, psr_reg)                                         \
  generate_function_call(execute_read_##psr_reg);                              \
  rv_mv(RV, reg_rv, reg_a0);                                                   \
  generate_store_reg(reg_rv, rd)

#define arm_psr_load_new_reg() generate_load_reg(reg_a0, rm)

#define arm_psr_load_new_imm() generate_load_imm(reg_a0, imm)

#define arm_psr_store_spsr()                                                   \
  generate_load_imm(reg_a1, spsr_masks[psr_pfield]);                           \
  generate_function_call(execute_store_spsr)

#define arm_psr_store_cpsr()                                                   \
  generate_load_pc(reg_a1, (pc));                                              \
  generate_function_call(execute_store_cpsr);                                  \
  generate_raw_u32(cpsr_masks[psr_pfield][0]);                                 \
  generate_raw_u32(cpsr_masks[psr_pfield][1])

#define arm_psr_store(op_type, psr_reg)                                        \
  arm_psr_load_new_##op_type();                                                \
  arm_psr_store_##psr_reg()

#define arm_psr(op_type, transfer_type, psr_reg)                               \
  {                                                                            \
    arm_decode_psr_##op_type(opcode);                                          \
    arm_psr_##transfer_type(op_type, psr_reg);                                 \
  }

/* ==================================================================
 * Memory access — ARM mode
 *
 * On MIPS, memory handlers are called via jal with arguments
 * in delay slots. On RISC-V, set up args first, then call.
 * Return value is in a0; copy to reg_rv for compatibility.
 * ================================================================== */

/* Memory access cycles: the interpreter charges them from a table by region and
   size, ws_cyc_nseq[addr >> 24][(size - 8) / 16] (8 and 16 bit: 0, 32 bit: 1).
   The translated code does the same lookup at run time, so that the two engines
   count the same cycles and a game sees the same timing from both. */
#define rg_acc_idx_u8 0
#define rg_acc_idx_s8 0
#define rg_acc_idx_u16 0
#define rg_acc_idx_s16 0
#define rg_acc_idx_u32 1

#define rv_generate_access_charge(addr_reg, size_idx)                          \
  rv_srli(RV, reg_temp, addr_reg, 24);                                         \
  rv_andi(RV, reg_temp, reg_temp, 0xF);                                        \
  rv_slli(RV, reg_temp, reg_temp, 1);                                          \
  {                                                                            \
    u32 _ct = (u32)ws_cyc_nseq;                                                \
    rv_lui(RV, reg_rv, (_ct + 0x800) >> 12);                                   \
    rv_addi(RV, reg_rv, reg_rv, _ct & 0xFFF);                                  \
  }                                                                            \
  rv_add(RV, reg_temp, reg_temp, reg_rv);                                      \
  rv_lbu(RV, reg_temp, reg_temp, size_idx);                                    \
  rv_sub(RV, reg_cycles, reg_cycles, reg_temp)

#define arm_access_memory_load(mem_type)                                       \
  rv_generate_access_charge(reg_a0, rg_acc_idx_##mem_type);                    \
  generate_function_call(execute_load_##mem_type);                             \
  rv_mv(RV, reg_rv, reg_a0);                                                   \
  generate_store_reg(reg_rv, rd);                                              \
  check_store_reg_pc_no_flags(rd)

#define arm_access_memory_store(mem_type)                                      \
  rv_generate_access_charge(reg_a0, rg_acc_idx_##mem_type);                    \
  generate_load_pc(reg_a2, (pc + 4));                                          \
  generate_load_reg_pc(reg_a1, rd, 12);                                        \
  generate_function_call(execute_store_##mem_type)

/* Address computation: register offset, pre-indexed */
#define arm_access_memory_reg_pre_up()                                         \
  rv_add(RV, reg_a0, arm_to_mips_reg[rn], arm_to_mips_reg[rm])

#define arm_access_memory_reg_pre_down()                                       \
  rv_sub(RV, reg_a0, arm_to_mips_reg[rn], arm_to_mips_reg[rm])

#define arm_access_memory_reg_pre(adjust_dir)                                  \
  check_load_reg_pc(arm_reg_a0, rn, 8);                                        \
  arm_access_memory_reg_pre_##adjust_dir()

#define arm_access_memory_reg_pre_wb(adjust_dir)                               \
  arm_access_memory_reg_pre(adjust_dir);                                       \
  generate_store_reg(reg_a0, rn)

/* Post-indexed register offset */
#define arm_access_memory_reg_post_up()                                        \
  rv_add(RV, arm_to_mips_reg[rn], arm_to_mips_reg[rn], arm_to_mips_reg[rm])

#define arm_access_memory_reg_post_down()                                      \
  rv_sub(RV, arm_to_mips_reg[rn], arm_to_mips_reg[rn], arm_to_mips_reg[rm])

#define arm_access_memory_reg_post(adjust_dir)                                 \
  generate_load_reg(reg_a0, rn);                                               \
  arm_access_memory_reg_post_##adjust_dir()

/* Immediate offset */
#define arm_access_memory_imm_pre_up()                                         \
  rv_addi_safe(reg_a0, arm_to_mips_reg[rn], offset)

#define arm_access_memory_imm_pre_down()                                       \
  rv_addi_safe(reg_a0, arm_to_mips_reg[rn], -(s32)offset)

#define arm_access_memory_imm_pre(adjust_dir)                                  \
  check_load_reg_pc(arm_reg_a0, rn, 8);                                        \
  arm_access_memory_imm_pre_##adjust_dir()

#define arm_access_memory_imm_pre_wb(adjust_dir)                               \
  arm_access_memory_imm_pre(adjust_dir);                                       \
  generate_store_reg(reg_a0, rn)

#define arm_access_memory_imm_post_up()                                        \
  rv_addi_safe(arm_to_mips_reg[rn], arm_to_mips_reg[rn], offset)

#define arm_access_memory_imm_post_down()                                      \
  rv_addi_safe(arm_to_mips_reg[rn], arm_to_mips_reg[rn], -(s32)offset)

#define arm_access_memory_imm_post(adjust_dir)                                 \
  generate_load_reg(reg_a0, rn);                                               \
  arm_access_memory_imm_post_##adjust_dir()

/* Data transfer decode + address computation */
#define arm_data_trans_reg(adjust_op, adjust_dir)                              \
  arm_decode_data_trans_reg();                                                 \
  generate_load_offset_sh(rm);                                                 \
  arm_access_memory_reg_##adjust_op(adjust_dir)

#define arm_data_trans_imm(adjust_op, adjust_dir)                              \
  arm_decode_data_trans_imm();                                                 \
  arm_access_memory_imm_##adjust_op(adjust_dir)

#define arm_data_trans_half_reg(adjust_op, adjust_dir)                         \
  arm_decode_half_trans_r();                                                   \
  arm_access_memory_reg_##adjust_op(adjust_dir)

#define arm_data_trans_half_imm(adjust_op, adjust_dir)                         \
  arm_decode_half_trans_of();                                                  \
  arm_access_memory_imm_##adjust_op(adjust_dir)

#define arm_access_memory(access_type, direction, adjust_op, mem_type,         \
                          offset_type)                                         \
  {                                                                            \
    arm_data_trans_##offset_type(adjust_op, direction);                        \
    arm_access_memory_##access_type(mem_type);                                 \
  }

/* ==================================================================
 * Block memory — ARM mode (generic path, no SP optimization)
 * ================================================================== */

#define word_bit_count(word) (bit_count[word >> 8] + bit_count[word & 0xFF])

#define arm_block_memory_load()                                                \
  generate_function_call(execute_aligned_load32);                              \
  rv_mv(RV, reg_rv, reg_a0);                                                   \
  generate_store_reg(reg_rv, i)

#define arm_block_memory_store()                                               \
  generate_load_reg_pc(reg_a1, i, 8);                                          \
  generate_function_call(execute_aligned_store32)

#define arm_block_memory_final_load(writeback_type) arm_block_memory_load()

#define arm_block_memory_final_store(writeback_type)                           \
  generate_load_pc(reg_a2, (pc + 4));                                          \
  generate_load_reg(reg_a1, i);                                                \
  arm_block_memory_writeback_post_store(writeback_type);                       \
  generate_function_call(execute_store_u32)

#define arm_block_memory_adjust_pc_store()

#define arm_block_memory_adjust_pc_load()                                      \
  if (reg_list & 0x8000) {                                                     \
    generate_mov(reg_a0, reg_rv);                                              \
    generate_indirect_branch_arm();                                            \
  }

#define arm_block_memory_offset_down_a()                                       \
  rv_addi_safe(reg_a2, base_reg, (-((word_bit_count(reg_list) * 4) - 4)))

#define arm_block_memory_offset_down_b()                                       \
  rv_addi_safe(reg_a2, base_reg, (word_bit_count(reg_list) * -4))

#define arm_block_memory_offset_no() rv_mv(RV, reg_a2, base_reg)

#define arm_block_memory_offset_up() rv_addi(RV, reg_a2, base_reg, 4)

#define arm_block_memory_writeback_down()                                      \
  rv_addi_safe(base_reg, base_reg, (-(word_bit_count(reg_list) * 4)))

#define arm_block_memory_writeback_up()                                        \
  rv_addi_safe(base_reg, base_reg, (word_bit_count(reg_list) * 4))

#define arm_block_memory_writeback_no()

#define arm_block_memory_writeback_post_load(writeback_type)
#define arm_block_memory_writeback_pre_load(writeback_type)                    \
  if (!((reg_list >> rn) & 0x01)) {                                            \
    arm_block_memory_writeback_##writeback_type();                             \
  }

#define arm_block_memory_writeback_pre_store(writeback_type)
#define arm_block_memory_writeback_post_store(writeback_type)                  \
  arm_block_memory_writeback_##writeback_type()

#define arm_block_memory(access_type, offset_type, writeback_type, s_bit)      \
  {                                                                            \
    arm_decode_block_trans();                                                  \
    u32 i;                                                                     \
    u32 offset = 0;                                                            \
    u32 base_reg = arm_to_mips_reg[rn];                                        \
                                                                               \
    arm_block_memory_offset_##offset_type();                                   \
    arm_block_memory_writeback_pre_##access_type(writeback_type);              \
                                                                               \
    /* Always use generic (function-call) path */                              \
    emit_align_reg(reg_a2, 2);                                                 \
    for (i = 0; i < 16; i++) {                                                 \
      if ((reg_list >> i) & 0x01) {                                            \
        cycle_count++;                                                         \
        rv_addi_safe(reg_a0, reg_a2, offset);                                  \
        if (reg_list & ~((2 << i) - 1)) {                                      \
          arm_block_memory_##access_type();                                    \
          offset += 4;                                                         \
        } else {                                                               \
          arm_block_memory_final_##access_type(writeback_type);                \
          break;                                                               \
        }                                                                      \
      }                                                                        \
    }                                                                          \
    arm_block_memory_adjust_pc_##access_type();                                \
  }

/* ARM swap */
#define arm_swap(type)                                                         \
  {                                                                            \
    arm_decode_swap();                                                         \
    cycle_count += 3;                                                          \
    generate_load_reg(reg_a0, rn);                                             \
    generate_function_call(execute_load_##type);                               \
    rv_mv(RV, reg_rv, reg_a0);                                                 \
    generate_load_reg(reg_a0, rn);                                             \
    generate_load_reg(reg_a1, rm);                                             \
    generate_function_call(execute_store_##type);                              \
    generate_store_reg(reg_rv, rd);                                            \
  }

/* ==================================================================
 * Thumb mode macros
 * ================================================================== */

#define thumb_generate_op_load_yes(_rs) generate_load_reg(reg_a1, _rs)

#define thumb_generate_op_load_no(_rs)

#define thumb_generate_op_reg(name, _rd, _rs, _rn)                             \
  generate_op_##name##_reg(arm_to_mips_reg[_rd], arm_to_mips_reg[_rs],         \
                           arm_to_mips_reg[_rn])

#define thumb_generate_op_imm(name, _rd, _rs, _rn)                             \
  generate_op_##name##_imm(arm_to_mips_reg[_rd], arm_to_mips_reg[_rs])

#define thumb_data_proc(type, name, rn_type, _rd, _rs, _rn)                    \
  {                                                                            \
    thumb_decode_##type();                                                     \
    thumb_generate_op_##rn_type(name, _rd, _rs, _rn);                          \
  }

#define thumb_data_proc_test(type, name, rn_type, _rs, _rn)                    \
  {                                                                            \
    thumb_decode_##type();                                                     \
    thumb_generate_op_##rn_type(name, 0, _rs, _rn);                            \
  }

#define thumb_data_proc_unary(type, name, rn_type, _rd, _rn)                   \
  {                                                                            \
    thumb_decode_##type();                                                     \
    thumb_generate_op_##rn_type(name, _rd, 0, _rn);                            \
  }

#define check_store_reg_pc_thumb(_rd)                                          \
  if (_rd == REG_PC) {                                                         \
    generate_indirect_branch_cycle_update(thumb);                              \
  }

#define thumb_data_proc_hi(name)                                               \
  {                                                                            \
    thumb_decode_hireg_op();                                                   \
    u32 dest_rd = rd;                                                          \
    check_load_reg_pc(arm_reg_a0, rs, 4);                                      \
    check_load_reg_pc(arm_reg_a1, rd, 4);                                      \
    generate_op_##name##_reg(arm_to_mips_reg[dest_rd], arm_to_mips_reg[rd],    \
                             arm_to_mips_reg[rs]);                             \
    check_store_reg_pc_thumb(dest_rd);                                         \
  }

#define thumb_data_proc_test_hi(name)                                          \
  {                                                                            \
    thumb_decode_hireg_op();                                                   \
    check_load_reg_pc(arm_reg_a0, rs, 4);                                      \
    check_load_reg_pc(arm_reg_a1, rd, 4);                                      \
    generate_op_##name##_reg(reg_temp, arm_to_mips_reg[rd],                    \
                             arm_to_mips_reg[rs]);                             \
  }

#define thumb_data_proc_mov_hi()                                               \
  {                                                                            \
    thumb_decode_hireg_op();                                                   \
    check_load_reg_pc(arm_reg_a0, rs, 4);                                      \
    rv_mv(RV, arm_to_mips_reg[rd], arm_to_mips_reg[rs]);                       \
    check_store_reg_pc_thumb(rd);                                              \
  }

/* Thumb constant/address loading */
#define thumb_load_pc_pool_const(rd, value)                                    \
  generate_load_imm(arm_to_mips_reg[rd], (value))

#define thumb_load_pc(_rd)                                                     \
  {                                                                            \
    thumb_decode_imm();                                                        \
    generate_load_pc(arm_to_mips_reg[_rd], (((pc & ~2) + 4) + (imm * 4)));     \
  }

#define thumb_load_sp(_rd)                                                     \
  {                                                                            \
    thumb_decode_imm();                                                        \
    rv_addi_safe(arm_to_mips_reg[_rd], reg_r13, (imm * 4));                    \
  }

#define thumb_adjust_sp_up() rv_addi_safe(reg_r13, reg_r13, (imm * 4))

#define thumb_adjust_sp_down() rv_addi_safe(reg_r13, reg_r13, -(s32)(imm * 4))

#define thumb_adjust_sp(direction)                                             \
  {                                                                            \
    thumb_decode_add_sp();                                                     \
    thumb_adjust_sp_##direction();                                             \
  }

/* Thumb shifts */
#define thumb_generate_shift_imm(name)                                         \
  if (check_generate_c_flag) {                                                 \
    generate_shift_imm_##name##_flags(rd, rs, imm);                            \
  } else {                                                                     \
    generate_shift_imm_##name##_no_flags(rd, rs, imm);                         \
  }                                                                            \
  if (rs != rd) {                                                              \
    rv_mv(RV, arm_to_mips_reg[rd], arm_to_mips_reg[rs]);                       \
  }

#define thumb_generate_shift_reg(name)                                         \
  {                                                                            \
    u32 original_rd = rd;                                                      \
    if (check_generate_c_flag) {                                               \
      generate_shift_reg_##name##_flags(rd, rs);                               \
    } else {                                                                   \
      generate_shift_reg_##name##_no_flags(rd, rs);                            \
    }                                                                          \
    rv_mv(RV, arm_to_mips_reg[original_rd], reg_a0);                           \
  }

#define thumb_shift(decode_type, op_type, value_type)                          \
  {                                                                            \
    thumb_decode_##decode_type();                                              \
    thumb_generate_shift_##value_type(op_type);                                \
    generate_op_logic_flags(arm_to_mips_reg[rd]);                              \
  }

/* Thumb memory access */
#define thumb_access_memory_load(mem_type, reg_rd)                             \
  rv_generate_access_charge(reg_a0, rg_acc_idx_##mem_type);                    \
  generate_function_call(execute_load_##mem_type);                             \
  rv_mv(RV, reg_rv, reg_a0);                                                   \
  generate_store_reg(reg_rv, reg_rd)

#define thumb_access_memory_store(mem_type, reg_rd)                            \
  rv_generate_access_charge(reg_a0, rg_acc_idx_##mem_type);                    \
  generate_load_pc(reg_a2, (pc + 2));                                          \
  generate_load_reg(reg_a1, reg_rd);                                           \
  generate_function_call(execute_store_##mem_type)

#define thumb_access_memory_generate_address_pc_relative(offset, reg_rb,       \
                                                         reg_ro)               \
  generate_load_pc(reg_a0, (offset))

#define thumb_access_memory_generate_address_reg_imm(offset, reg_rb, reg_ro)   \
  rv_addi_safe(reg_a0, arm_to_mips_reg[reg_rb], (offset))

#define thumb_access_memory_generate_address_reg_imm_sp(offset, reg_rb,        \
                                                        reg_ro)                \
  rv_addi_safe(reg_a0, arm_to_mips_reg[reg_rb], (offset * 4))

#define thumb_access_memory_generate_address_reg_reg(offset, reg_rb, reg_ro)   \
  rv_add(RV, reg_a0, arm_to_mips_reg[reg_rb], arm_to_mips_reg[reg_ro])

#define thumb_access_memory(access_type, op_type, reg_rd, reg_rb, reg_ro,      \
                            address_type, offset, mem_type)                    \
  {                                                                            \
    thumb_decode_##op_type();                                                  \
    thumb_access_memory_generate_address_##address_type(offset, reg_rb,        \
                                                        reg_ro);               \
    thumb_access_memory_##access_type(mem_type, reg_rd);                       \
  }

/* Thumb block memory (generic path) */
#define thumb_block_address_preadjust_no(base_reg)                             \
  rv_mv(RV, reg_a2, arm_to_mips_reg[base_reg])

#define thumb_block_address_preadjust_down(base_reg)                           \
  rv_addi_safe(reg_a2, arm_to_mips_reg[base_reg], -(bit_count[reg_list] * 4)); \
  rv_mv(RV, arm_to_mips_reg[base_reg], reg_a2)

#define thumb_block_address_preadjust_push_lr(base_reg)                        \
  rv_addi_safe(reg_a2, arm_to_mips_reg[base_reg],                              \
               -((bit_count[reg_list] + 1) * 4));                              \
  rv_mv(RV, arm_to_mips_reg[base_reg], reg_a2)

#define thumb_block_address_postadjust_no(base_reg)

#define thumb_block_address_postadjust_up(base_reg)                            \
  rv_addi_safe(arm_to_mips_reg[base_reg], reg_a2, (bit_count[reg_list] * 4))

#define thumb_block_address_postadjust_pop_pc(base_reg)                        \
  rv_addi_safe(arm_to_mips_reg[base_reg], reg_a2,                              \
               ((bit_count[reg_list] * 4) + 4))

#define thumb_block_address_postadjust_push_lr(base_reg)

#define thumb_block_memory_load()                                              \
  generate_function_call(execute_aligned_load32);                              \
  rv_mv(RV, reg_rv, reg_a0);                                                   \
  generate_store_reg(reg_rv, i)

#define thumb_block_memory_store()                                             \
  generate_load_reg(reg_a1, i);                                                \
  generate_function_call(execute_aligned_store32)

#define thumb_block_memory_final_load() thumb_block_memory_load()

#define thumb_block_memory_final_store()                                       \
  generate_load_pc(reg_a2, (pc + 2));                                          \
  generate_load_reg(reg_a1, i);                                                \
  generate_function_call(execute_store_u32)

#define thumb_block_memory_final_no(access_type)                               \
  thumb_block_memory_final_##access_type()

#define thumb_block_memory_final_up(access_type)                               \
  thumb_block_memory_final_##access_type()

#define thumb_block_memory_final_down(access_type)                             \
  thumb_block_memory_final_##access_type()

#define thumb_block_memory_final_push_lr(access_type)                          \
  thumb_block_memory_##access_type()

#define thumb_block_memory_final_pop_pc(access_type)                           \
  thumb_block_memory_##access_type()

#define thumb_block_memory_extra_no()
#define thumb_block_memory_extra_up()
#define thumb_block_memory_extra_down()

#define thumb_block_memory_extra_push_lr()                                     \
  rv_addi_safe(reg_a0, reg_a2, (bit_count[reg_list] * 4));                     \
  generate_load_reg(reg_a1, REG_LR);                                           \
  generate_function_call(execute_aligned_store32)

#define thumb_block_memory_extra_pop_pc()                                      \
  rv_addi_safe(reg_a0, reg_a2, (bit_count[reg_list] * 4));                     \
  generate_function_call(execute_aligned_load32);                              \
  /* Return value in a0 = loaded PC */                                         \
  generate_indirect_branch_cycle_update(thumb)

#define thumb_block_memory(access_type, pre_op, post_op, base_reg)             \
  {                                                                            \
    thumb_decode_rlist();                                                      \
    u32 i;                                                                     \
    u32 offset = 0;                                                            \
                                                                               \
    thumb_block_address_preadjust_##pre_op(base_reg);                          \
    thumb_block_address_postadjust_##post_op(base_reg);                        \
                                                                               \
    /* Always use generic path */                                              \
    emit_align_reg(reg_a2, 2);                                                 \
    for (i = 0; i < 8; i++) {                                                  \
      if ((reg_list >> i) & 0x01) {                                            \
        cycle_count++;                                                         \
        rv_addi_safe(reg_a0, reg_a2, offset);                                  \
        if (reg_list & ~((2 << i) - 1)) {                                      \
          thumb_block_memory_##access_type();                                  \
          offset += 4;                                                         \
        } else {                                                               \
          thumb_block_memory_final_##post_op(access_type);                     \
          break;                                                               \
        }                                                                      \
      }                                                                        \
    }                                                                          \
    thumb_block_memory_extra_##post_op();                                      \
  }

/* ==================================================================
 * Conditional branch (Thumb)
 * ================================================================== */

#define thumb_conditional_branch(condition)                                    \
  {                                                                            \
    generate_condition_##condition();                                          \
    generate_branch_no_cycle_update(                                           \
        block_exits[block_exit_position].branch_source,                        \
        block_exits[block_exit_position].branch_target);                       \
    generate_branch_patch_conditional(backpatch_address, translation_ptr);     \
    block_exit_position++;                                                     \
  }

/* ==================================================================
 * ARM conditional block header
 * ================================================================== */

#define arm_conditional_block_header() generate_condition()

/* ==================================================================
 * Branch / call / SWI macros
 * ================================================================== */

#define arm_b() generate_branch()

#define arm_bl()                                                               \
  generate_load_pc(reg_r14, (pc + 4));                                         \
  generate_branch()

#define arm_bx()                                                               \
  arm_decode_branchx(opcode);                                                  \
  generate_load_reg_pc(reg_a0, rn, 8);                                         \
  generate_indirect_branch_dual()

#define arm_swi()                                                              \
  generate_load_pc(reg_a0, (pc + 4));                                          \
  generate_function_call(execute_swi);                                         \
  generate_branch()

#define thumb_b()                                                              \
  generate_branch_cycle_update(                                                \
      block_exits[block_exit_position].branch_source,                          \
      block_exits[block_exit_position].branch_target);                         \
  block_exit_position++

#define thumb_bl()                                                             \
  generate_load_pc(reg_r14, ((pc + 2) | 0x01));                                \
  generate_branch_cycle_update(                                                \
      block_exits[block_exit_position].branch_source,                          \
      block_exits[block_exit_position].branch_target);                         \
  block_exit_position++

#define thumb_blh()                                                            \
  {                                                                            \
    thumb_decode_branch();                                                     \
    rv_addi_safe(reg_a0, reg_r14, (offset * 2));                               \
    generate_load_pc(reg_r14, ((pc + 2) | 0x01));                              \
    generate_indirect_branch_cycle_update(thumb);                              \
  }

#define thumb_bx()                                                             \
  {                                                                            \
    thumb_decode_hireg_op();                                                   \
    generate_load_reg_pc(reg_a0, rs, 4);                                       \
    generate_indirect_branch_cycle_update(dual);                               \
  }

#define thumb_swi()                                                            \
  generate_load_pc(reg_a0, (pc + 2));                                          \
  generate_function_call(execute_swi);                                         \
  generate_branch_cycle_update(                                                \
      block_exits[block_exit_position].branch_source,                          \
      block_exits[block_exit_position].branch_target);                         \
  block_exit_position++

/* ==================================================================
 * HLE division (software divide optimization)
 * RISC-V has hardware div/rem.
 * ================================================================== */

#define arm_hle_div(cpu_mode)                                                  \
  /* the remainder comes from the original dividend: r1 = num % den */         \
  rv_rem(RV, reg_temp, reg_r0, reg_r1);                                        \
  rv_div(RV, reg_r0, reg_r0, reg_r1);                                          \
  rv_mv(RV, reg_r1, reg_temp);                                                 \
  rv_srai(RV, reg_a0, reg_r0, 31);                                             \
  rv_xor(RV, reg_r3, reg_r0, reg_a0);                                          \
  rv_sub(RV, reg_r3, reg_r3, reg_a0)

#define arm_hle_div_arm(cpu_mode)                                              \
  /* r1 = the old r1 %% the old r0 */                                          \
  rv_rem(RV, reg_temp, reg_r1, reg_r0);                                        \
  rv_div(RV, reg_r0, reg_r1, reg_r0);                                          \
  rv_mv(RV, reg_r1, reg_temp);                                                 \
  rv_srai(RV, reg_a0, reg_r0, 31);                                             \
  rv_xor(RV, reg_r3, reg_r0, reg_a0);                                          \
  rv_sub(RV, reg_r3, reg_r3, reg_a0)

/* ==================================================================
 * Miscellaneous
 * ================================================================== */

#define generate_translation_gate(type)                                        \
  generate_load_pc(reg_a0, pc);                                                \
  generate_indirect_branch_no_cycle_update(type)

#define generate_update_pc_reg()                                               \
  generate_load_pc(reg_a0, pc);                                                \
  rv_sw(RV, reg_a0, reg_base, (REG_PC * 4))

#define thumb_process_cheats() generate_function_call(riscv_cheat_hook)

#define arm_process_cheats() generate_function_call(riscv_cheat_hook)

/* Tracing (disabled by default) */
#ifdef TRACE_INSTRUCTIONS
void trace_instruction(u32 pc, u32 mode) {
  if (mode)
    printf("Executed arm %x\n", pc);
  else
    printf("Executed thumb %x\n", pc);
}
#define emit_trace_instruction(pc, mode)                                       \
  emit_save_regs(false);                                                       \
  generate_load_imm(reg_a0, pc);                                               \
  generate_load_imm(reg_a1, mode);                                             \
  genccall(&trace_instruction);                                                \
  emit_restore_regs(false)
#define emit_trace_thumb_instruction(pc) emit_trace_instruction(pc, 0)
#define emit_trace_arm_instruction(pc) emit_trace_instruction(pc, 1)
#else
#define emit_trace_thumb_instruction(pc)
#define emit_trace_arm_instruction(pc)
#endif

/* ==================================================================
 * Register save/restore offsets
 * ================================================================== */

#define ReOff_RegPC (REG_PC * 4)
#define ReOff_CPSR (REG_CPSR * 4)
#define ReOff_SaveR1 (REG_SAVE * 4)
#define ReOff_SaveR2 (REG_SAVE2 * 4)
#define ReOff_SaveR3 (REG_SAVE3 * 4)
#define ReOff_OamUpd (OAM_UPDATED * 4)
#define ReOff_GP_Save (REG_SAVE5 * 4)

/* Save all GBA registers from host regs to memory */
#define emit_save_regs(save_a2)                                                \
  {                                                                            \
    int _i;                                                                    \
    for (_i = 0; _i < 15; _i++) {                                              \
      rv_sw(RV, arm_to_mips_reg[_i], reg_base, 4 * _i);                        \
    }                                                                          \
    if (save_a2) {                                                             \
      rv_sw(RV, reg_a2, reg_base, ReOff_SaveR2);                               \
    }                                                                          \
  }

/* Restore all GBA registers from memory to host regs */
#define emit_restore_regs(restore_a2)                                          \
  {                                                                            \
    int _i;                                                                    \
    if (restore_a2) {                                                          \
      rv_lw(RV, reg_a2, reg_base, ReOff_SaveR2);                               \
    }                                                                          \
    for (_i = 0; _i < 15; _i++) {                                              \
      rv_lw(RV, arm_to_mips_reg[_i], reg_base, 4 * _i);                        \
    }                                                                          \
  }

/* Call a C function from within a generated stub */
#define emit_mem_call_ds(fnptr, mask)                                          \
  rv_sw(RV, rv_ra, reg_base, ReOff_SaveR1);                                    \
  emit_save_regs(true);                                                        \
  rv_andi(RV, reg_a0, reg_a0, (mask));                                         \
  genccall(fnptr);                                                             \
  rv_lw(RV, rv_ra, reg_base, ReOff_SaveR1);                                    \
  emit_restore_regs(true)

#define emit_mem_call(fnptr, mask)                                             \
  emit_mem_call_ds(fnptr, mask);                                               \
  rv_ret(RV)

/* Pointer table to stubs */
extern const u8 ldhldrtbl[11];

/* These will need adaptation for RISC-V address calculation */
#define ld_phndlr_branch(memop)                                                \
  (((u32 *)&rom_translation_cache[ldhldrtbl[(memop)] * 16 * 4]) -              \
   ((u32 *)translation_ptr + 1))

#define st_phndlr_branch(memop)                                                \
  (((u32 *)&rom_translation_cache[((memop) + 6) * 16 * 4]) -                   \
   ((u32 *)translation_ptr + 1))

#define branch_handlerid(phndlrid)                                             \
  (((u32 *)&rom_translation_cache[(phndlrid) * 16 * 4]) -                      \
   ((u32 *)translation_ptr + 1))

#define branch_offset(ptr) (((u32 *)ptr) - ((u32 *)translation_ptr + 1))

void init_emitter(bool must_swap);
u32 execute_arm_translate(u32 cycles);
u32 execute_spsr_restore_body(u32 address);
u32 execute_store_cpsr_body(u32 cpsr, u32 address);

#endif /* RISCV_EMIT_H */
