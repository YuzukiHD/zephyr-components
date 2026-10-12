/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * RISC-V instruction encoders and the code emitter of the gpSP dynamic
 * recompiler.
 *
 * Every instruction is a function: `rv_add(cp, rd, rs1, rs2)` writes one
 * instruction at
 * `*cp`, advances `*cp` by four bytes and returns the address it wrote (that is
 * what a branch that is patched later needs). The encoders (`rv_enc_*`) are
 * pure functions of their fields.
 *
 * Formats:
 *   R: funct7[31:25] rs2[24:20] rs1[19:15] funct3[14:12] rd[11:7] opcode[6:0]
 *   I: imm[31:20] rs1 funct3 rd opcode
 *   S: imm[31:25] rs2 rs1 funct3 imm[11:7] opcode
 *   B: imm[12|10:5] rs2 rs1 funct3 imm[4:1|11] opcode
 *   U: imm[31:12] rd opcode
 *   J: imm[20|10:1|11|19:12] rd opcode
 */

#ifndef RISCV_ASM_H
#define RISCV_ASM_H

typedef enum rv_reg {
  rv_zero,
  rv_ra,
  rv_sp,
  rv_gp,
  rv_tp,
  rv_t0,
  rv_t1,
  rv_t2,
  rv_s0,
  rv_s1,
  rv_a0,
  rv_a1,
  rv_a2,
  rv_a3,
  rv_a4,
  rv_a5,
  rv_a6,
  rv_a7,
  rv_s2,
  rv_s3,
  rv_s4,
  rv_s5,
  rv_s6,
  rv_s7,
  rv_s8,
  rv_s9,
  rv_s10,
  rv_s11,
  rv_t3,
  rv_t4,
  rv_t5,
  rv_t6
} rv_reg;

enum rv_opcode {
  RV_OP_LOAD = 0x03,
  RV_OP_FENCE = 0x0F,
  RV_OP_IMM = 0x13,
  RV_OP_AUIPC = 0x17,
  RV_OP_STORE = 0x23,
  RV_OP_REG = 0x33,
  RV_OP_LUI = 0x37,
  RV_OP_BRANCH = 0x63,
  RV_OP_JALR = 0x67,
  RV_OP_JAL = 0x6F,
};

enum rv_branch_op {
  RV_BEQ = 0,
  RV_BNE = 1,
  RV_BLT = 4,
  RV_BGE = 5,
  RV_BLTU = 6,
  RV_BGEU = 7
};
enum rv_load_op { RV_LB = 0, RV_LH = 1, RV_LW = 2, RV_LBU = 4, RV_LHU = 5 };
enum rv_store_op { RV_SB = 0, RV_SH = 1, RV_SW = 2 };

/* funct3 of the ALU instructions (funct7 tells add from sub and srl from sra)
 */
enum rv_alu_op {
  RV_ADD = 0,
  RV_SLL = 1,
  RV_SLT = 2,
  RV_SLTU = 3,
  RV_XOR = 4,
  RV_SRL = 5,
  RV_OR = 6,
  RV_AND = 7,
};

#define RV_FUNCT7_ALT 0x20 /* sub, sra */
#define RV_FUNCT7_MUL 0x01 /* M extension */

static inline int rv_imm12_fits(s32 v) { return v >= -2048 && v <= 2047; }

/* ---- encoders ---------------------------------------------------- */

static inline u32 rv_enc_r(u32 funct7, u32 rs2, u32 rs1, u32 funct3, u32 rd,
                           u32 opcode) {
  return funct7 << 25 | rs2 << 20 | rs1 << 15 | funct3 << 12 | rd << 7 | opcode;
}

static inline u32 rv_enc_i(s32 imm12, u32 rs1, u32 funct3, u32 rd, u32 opcode) {
  return ((u32)imm12 & 0xFFF) << 20 | rs1 << 15 | funct3 << 12 | rd << 7 |
         opcode;
}

static inline u32 rv_enc_s(s32 imm12, u32 rs2, u32 rs1, u32 funct3,
                           u32 opcode) {
  return (((u32)imm12 >> 5) & 0x7F) << 25 | rs2 << 20 | rs1 << 15 |
         funct3 << 12 | ((u32)imm12 & 0x1F) << 7 | opcode;
}

static inline u32 rv_enc_b(s32 imm13, u32 rs2, u32 rs1, u32 funct3,
                           u32 opcode) {
  u32 o = (u32)imm13;
  return ((o >> 12) & 1) << 31 | ((o >> 5) & 0x3F) << 25 | rs2 << 20 |
         rs1 << 15 | funct3 << 12 | ((o >> 1) & 0xF) << 8 |
         ((o >> 11) & 1) << 7 | opcode;
}

static inline u32 rv_enc_u(u32 imm20, u32 rd, u32 opcode) {
  return (imm20 & 0xFFFFF) << 12 | rd << 7 | opcode;
}

static inline u32 rv_enc_j(s32 imm21, u32 rd, u32 opcode) {
  u32 o = (u32)imm21;
  return ((o >> 20) & 1) << 31 | ((o >> 1) & 0x3FF) << 21 |
         ((o >> 11) & 1) << 20 | ((o >> 12) & 0xFF) << 12 | rd << 7 | opcode;
}

/* ---- emitter ----------------------------------------------------- */

typedef u8 *rv_code; /* the write pointer of the code being generated */

static inline u8 *rv_put(rv_code *cp, u32 inst) {
  u8 *at = *cp;

  *(u32 *)at = inst;
  *cp = at + 4;
  return at;
}

static inline u8 *rv_lui(rv_code *cp, rv_reg rd, u32 imm20) {
  return rv_put(cp, rv_enc_u(imm20, rd, RV_OP_LUI));
}
static inline u8 *rv_auipc(rv_code *cp, rv_reg rd, u32 imm20) {
  return rv_put(cp, rv_enc_u(imm20, rd, RV_OP_AUIPC));
}
static inline u8 *rv_jal(rv_code *cp, rv_reg rd, s32 off) {
  return rv_put(cp, rv_enc_j(off, rd, RV_OP_JAL));
}
static inline u8 *rv_jalr(rv_code *cp, rv_reg rd, rv_reg rs1, s32 off) {
  return rv_put(cp, rv_enc_i(off, rs1, 0, rd, RV_OP_JALR));
}

static inline u8 *rv_alu_i(rv_code *cp, enum rv_alu_op op, rv_reg rd,
                           rv_reg rs1, s32 imm) {
  return rv_put(cp, rv_enc_i(imm, rs1, op, rd, RV_OP_IMM));
}

static inline u8 *rv_alu_r(rv_code *cp, enum rv_alu_op op, u32 funct7,
                           rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_put(cp, rv_enc_r(funct7, rs2, rs1, op, rd, RV_OP_REG));
}

static inline u8 *rv_addi(rv_code *cp, rv_reg rd, rv_reg rs1, s32 imm) {
  return rv_alu_i(cp, RV_ADD, rd, rs1, imm);
}
static inline u8 *rv_slti(rv_code *cp, rv_reg rd, rv_reg rs1, s32 imm) {
  return rv_alu_i(cp, RV_SLT, rd, rs1, imm);
}
static inline u8 *rv_sltiu(rv_code *cp, rv_reg rd, rv_reg rs1, s32 imm) {
  return rv_alu_i(cp, RV_SLTU, rd, rs1, imm);
}
static inline u8 *rv_xori(rv_code *cp, rv_reg rd, rv_reg rs1, s32 imm) {
  return rv_alu_i(cp, RV_XOR, rd, rs1, imm);
}
static inline u8 *rv_ori(rv_code *cp, rv_reg rd, rv_reg rs1, s32 imm) {
  return rv_alu_i(cp, RV_OR, rd, rs1, imm);
}
static inline u8 *rv_andi(rv_code *cp, rv_reg rd, rv_reg rs1, s32 imm) {
  return rv_alu_i(cp, RV_AND, rd, rs1, imm);
}

static inline u8 *rv_slli(rv_code *cp, rv_reg rd, rv_reg rs1, u32 sh) {
  return rv_alu_i(cp, RV_SLL, rd, rs1, sh);
}
static inline u8 *rv_srli(rv_code *cp, rv_reg rd, rv_reg rs1, u32 sh) {
  return rv_alu_i(cp, RV_SRL, rd, rs1, sh);
}
static inline u8 *rv_srai(rv_code *cp, rv_reg rd, rv_reg rs1, u32 sh) {
  return rv_alu_i(cp, RV_SRL, rd, rs1, sh | 0x400);
}

static inline u8 *rv_add(rv_code *cp, rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_alu_r(cp, RV_ADD, 0, rd, rs1, rs2);
}
static inline u8 *rv_sub(rv_code *cp, rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_alu_r(cp, RV_ADD, RV_FUNCT7_ALT, rd, rs1, rs2);
}
static inline u8 *rv_sll(rv_code *cp, rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_alu_r(cp, RV_SLL, 0, rd, rs1, rs2);
}
static inline u8 *rv_slt(rv_code *cp, rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_alu_r(cp, RV_SLT, 0, rd, rs1, rs2);
}
static inline u8 *rv_sltu(rv_code *cp, rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_alu_r(cp, RV_SLTU, 0, rd, rs1, rs2);
}
static inline u8 *rv_xor(rv_code *cp, rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_alu_r(cp, RV_XOR, 0, rd, rs1, rs2);
}
static inline u8 *rv_srl(rv_code *cp, rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_alu_r(cp, RV_SRL, 0, rd, rs1, rs2);
}
static inline u8 *rv_sra(rv_code *cp, rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_alu_r(cp, RV_SRL, RV_FUNCT7_ALT, rd, rs1, rs2);
}
static inline u8 *rv_or(rv_code *cp, rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_alu_r(cp, RV_OR, 0, rd, rs1, rs2);
}
static inline u8 *rv_and(rv_code *cp, rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_alu_r(cp, RV_AND, 0, rd, rs1, rs2);
}

/* M extension: funct3 0 mul, 1 mulh, 3 mulhu */
static inline u8 *rv_mul(rv_code *cp, rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_put(cp, rv_enc_r(RV_FUNCT7_MUL, rs2, rs1, 0, rd, RV_OP_REG));
}
static inline u8 *rv_mulh(rv_code *cp, rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_put(cp, rv_enc_r(RV_FUNCT7_MUL, rs2, rs1, 1, rd, RV_OP_REG));
}
static inline u8 *rv_mulhu(rv_code *cp, rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_put(cp, rv_enc_r(RV_FUNCT7_MUL, rs2, rs1, 3, rd, RV_OP_REG));
}

static inline u8 *rv_load(rv_code *cp, enum rv_load_op op, rv_reg rd,
                          rv_reg rs1, s32 off) {
  return rv_put(cp, rv_enc_i(off, rs1, op, rd, RV_OP_LOAD));
}

static inline u8 *rv_store(rv_code *cp, enum rv_store_op op, rv_reg rs2,
                           rv_reg rs1, s32 off) {
  return rv_put(cp, rv_enc_s(off, rs2, rs1, op, RV_OP_STORE));
}

static inline u8 *rv_div(rv_code *cp, rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_put(cp, rv_enc_r(RV_FUNCT7_MUL, rs2, rs1, 4, rd, RV_OP_REG));
}
static inline u8 *rv_divu(rv_code *cp, rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_put(cp, rv_enc_r(RV_FUNCT7_MUL, rs2, rs1, 5, rd, RV_OP_REG));
}
static inline u8 *rv_rem(rv_code *cp, rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_put(cp, rv_enc_r(RV_FUNCT7_MUL, rs2, rs1, 6, rd, RV_OP_REG));
}
static inline u8 *rv_remu(rv_code *cp, rv_reg rd, rv_reg rs1, rv_reg rs2) {
  return rv_put(cp, rv_enc_r(RV_FUNCT7_MUL, rs2, rs1, 7, rd, RV_OP_REG));
}

static inline u8 *rv_lb(rv_code *cp, rv_reg rd, rv_reg rs1, s32 off) {
  return rv_load(cp, RV_LB, rd, rs1, off);
}
static inline u8 *rv_lh(rv_code *cp, rv_reg rd, rv_reg rs1, s32 off) {
  return rv_load(cp, RV_LH, rd, rs1, off);
}
static inline u8 *rv_lw(rv_code *cp, rv_reg rd, rv_reg rs1, s32 off) {
  return rv_load(cp, RV_LW, rd, rs1, off);
}
static inline u8 *rv_lbu(rv_code *cp, rv_reg rd, rv_reg rs1, s32 off) {
  return rv_load(cp, RV_LBU, rd, rs1, off);
}
static inline u8 *rv_lhu(rv_code *cp, rv_reg rd, rv_reg rs1, s32 off) {
  return rv_load(cp, RV_LHU, rd, rs1, off);
}
static inline u8 *rv_sb(rv_code *cp, rv_reg rs2, rv_reg rs1, s32 off) {
  return rv_store(cp, RV_SB, rs2, rs1, off);
}
static inline u8 *rv_sh(rv_code *cp, rv_reg rs2, rv_reg rs1, s32 off) {
  return rv_store(cp, RV_SH, rs2, rs1, off);
}
static inline u8 *rv_sw(rv_code *cp, rv_reg rs2, rv_reg rs1, s32 off) {
  return rv_store(cp, RV_SW, rs2, rs1, off);
}

/* Branch to pc + off; with off 0 it is a filler that rv_patch_branch() fixes
 * later */
static inline u8 *rv_branch(rv_code *cp, enum rv_branch_op op, rv_reg rs1,
                            rv_reg rs2, s32 off) {
  return rv_put(cp, rv_enc_b(off, rs2, rs1, op, RV_OP_BRANCH));
}

static inline u8 *rv_beq(rv_code *cp, rv_reg rs1, rv_reg rs2, s32 off) {
  return rv_branch(cp, RV_BEQ, rs1, rs2, off);
}
static inline u8 *rv_bne(rv_code *cp, rv_reg rs1, rv_reg rs2, s32 off) {
  return rv_branch(cp, RV_BNE, rs1, rs2, off);
}
static inline u8 *rv_blt(rv_code *cp, rv_reg rs1, rv_reg rs2, s32 off) {
  return rv_branch(cp, RV_BLT, rs1, rs2, off);
}
static inline u8 *rv_bge(rv_code *cp, rv_reg rs1, rv_reg rs2, s32 off) {
  return rv_branch(cp, RV_BGE, rs1, rs2, off);
}
static inline u8 *rv_bltu(rv_code *cp, rv_reg rs1, rv_reg rs2, s32 off) {
  return rv_branch(cp, RV_BLTU, rs1, rs2, off);
}
static inline u8 *rv_bgeu(rv_code *cp, rv_reg rs1, rv_reg rs2, s32 off) {
  return rv_branch(cp, RV_BGEU, rs1, rs2, off);
}

/* pseudo instructions */
static inline u8 *rv_nop(rv_code *cp) {
  return rv_addi(cp, rv_zero, rv_zero, 0);
}
static inline u8 *rv_mv(rv_code *cp, rv_reg rd, rv_reg rs) {
  return rv_addi(cp, rd, rs, 0);
}
static inline u8 *rv_not(rv_code *cp, rv_reg rd, rv_reg rs) {
  return rv_xori(cp, rd, rs, -1);
}
static inline u8 *rv_neg(rv_code *cp, rv_reg rd, rv_reg rs) {
  return rv_sub(cp, rd, rv_zero, rs);
}
static inline u8 *rv_seqz(rv_code *cp, rv_reg rd, rv_reg rs) {
  return rv_sltiu(cp, rd, rs, 1);
}
static inline u8 *rv_snez(rv_code *cp, rv_reg rd, rv_reg rs) {
  return rv_sltu(cp, rd, rv_zero, rs);
}
static inline u8 *rv_ret(rv_code *cp) { return rv_jalr(cp, rv_zero, rv_ra, 0); }
static inline u8 *rv_fence_i(rv_code *cp) {
  return rv_put(cp, rv_enc_i(0, 0, 1, 0, RV_OP_FENCE));
}

/* if cond == 0 { rd = rs } / if cond != 0 { rd = rs }: skip the move with a
 * branch */
static inline void rv_movz(rv_code *cp, rv_reg rd, rv_reg rs, rv_reg cond) {
  rv_bne(cp, cond, rv_zero, 8);
  rv_mv(cp, rd, rs);
}

static inline void rv_movn(rv_code *cp, rv_reg rd, rv_reg rs, rv_reg cond) {
  rv_beq(cp, cond, rv_zero, 8);
  rv_mv(cp, rd, rs);
}

/* ---- constants and far operands ------------------------------------ */

/* rd = v: one addi when it fits in 12 bits, lui (+ addi) otherwise */
static inline void rv_li(rv_code *cp, rv_reg rd, u32 v) {
  if (rv_imm12_fits((s32)v)) {
    rv_addi(cp, rd, rv_zero, (s32)v);
  } else {
    rv_lui(cp, rd, (v + 0x800) >> 12);
    if (v & 0xFFF)
      rv_addi(cp, rd, rd, v & 0xFFF);
  }
}

/* rd = rs <op> imm for add/and/or/xor; an immediate that does not fit goes
 * through `tmp` */
static inline void rv_alu_imm(rv_code *cp, enum rv_alu_op op, rv_reg rd,
                              rv_reg rs, s32 imm, rv_reg tmp) {
  if (rv_imm12_fits(imm)) {
    rv_alu_i(cp, op, rd, rs, imm);
  } else {
    rv_li(cp, tmp, (u32)imm);
    rv_alu_r(cp, op, 0, rd, rs, tmp);
  }
}

/* call or jump to an absolute address through `tmp` (lui + jalr, 32 bit range)
 */
static inline void rv_call_abs(rv_code *cp, rv_reg link, rv_reg tmp, u32 addr) {
  rv_lui(cp, tmp, (addr + 0x800) >> 12);
  rv_jalr(cp, link, tmp, addr & 0xFFF);
}

/* jump filler of two instructions (lui + jalr), patched by rv_patch_jump_abs();
 * returns its address */
static inline u8 *rv_jump_filler(rv_code *cp, rv_reg tmp) {
  u8 *at = rv_lui(cp, tmp, 0);

  rv_jalr(cp, rv_zero, tmp, 0);
  return at;
}

/* ---- patching ----------------------------------------------------- */

static inline void rv_patch_branch(u8 *loc, const u8 *target) {
  u32 old = *(u32 *)loc;

  *(u32 *)loc = rv_enc_b((s32)(target - loc), (old >> 20) & 0x1F,
                         (old >> 15) & 0x1F, (old >> 12) & 7, RV_OP_BRANCH);
}

static inline void rv_patch_jal(u8 *loc, const u8 *target) {
  u32 old = *(u32 *)loc;

  *(u32 *)loc = rv_enc_j((s32)(target - loc), (old >> 7) & 0x1F, RV_OP_JAL);
}

/* point a lui + jalr pair (rv_jump_filler) at an absolute address */
static inline void rv_patch_jump_abs(u8 *loc, rv_reg tmp, u32 target) {
  ((u32 *)loc)[0] = rv_enc_u((target + 0x800) >> 12, tmp, RV_OP_LUI);
  ((u32 *)loc)[1] = rv_enc_i(target & 0xFFF, tmp, 0, rv_zero, RV_OP_JALR);
}

#endif /* RISCV_ASM_H */
