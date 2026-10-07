/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * setjmp/longjmp for rv32: the CPU core of Bochs leaves an instruction by
 * longjmp on an exception. Saves ra, sp, s0..s11 and, with the F/D
 * extension, fs0..fs11.
 */
#if __riscv_xlen == 64
#define SZ 8
#define SAVE sd
#define LOAD ld
#else
#define SZ 4
#define SAVE sw
#define LOAD lw
#endif

	.text
	.globl setjmp
	.type setjmp, @function
setjmp:
	SAVE ra, 0*SZ(a0)
	SAVE sp, 1*SZ(a0)
	SAVE s0, 2*SZ(a0)
	SAVE s1, 3*SZ(a0)
	SAVE s2, 4*SZ(a0)
	SAVE s3, 5*SZ(a0)
	SAVE s4, 6*SZ(a0)
	SAVE s5, 7*SZ(a0)
	SAVE s6, 8*SZ(a0)
	SAVE s7, 9*SZ(a0)
	SAVE s8, 10*SZ(a0)
	SAVE s9, 11*SZ(a0)
	SAVE s10, 12*SZ(a0)
	SAVE s11, 13*SZ(a0)
#if __riscv_flen == 64
	fsd fs0, 14*SZ+0*8(a0)
	fsd fs1, 14*SZ+1*8(a0)
	fsd fs2, 14*SZ+2*8(a0)
	fsd fs3, 14*SZ+3*8(a0)
	fsd fs4, 14*SZ+4*8(a0)
	fsd fs5, 14*SZ+5*8(a0)
	fsd fs6, 14*SZ+6*8(a0)
	fsd fs7, 14*SZ+7*8(a0)
	fsd fs8, 14*SZ+8*8(a0)
	fsd fs9, 14*SZ+9*8(a0)
	fsd fs10, 14*SZ+10*8(a0)
	fsd fs11, 14*SZ+11*8(a0)
#endif
	li a0, 0
	ret
	.size setjmp, . - setjmp

	.globl longjmp
	.type longjmp, @function
longjmp:
	LOAD ra, 0*SZ(a0)
	LOAD sp, 1*SZ(a0)
	LOAD s0, 2*SZ(a0)
	LOAD s1, 3*SZ(a0)
	LOAD s2, 4*SZ(a0)
	LOAD s3, 5*SZ(a0)
	LOAD s4, 6*SZ(a0)
	LOAD s5, 7*SZ(a0)
	LOAD s6, 8*SZ(a0)
	LOAD s7, 9*SZ(a0)
	LOAD s8, 10*SZ(a0)
	LOAD s9, 11*SZ(a0)
	LOAD s10, 12*SZ(a0)
	LOAD s11, 13*SZ(a0)
#if __riscv_flen == 64
	fld fs0, 14*SZ+0*8(a0)
	fld fs1, 14*SZ+1*8(a0)
	fld fs2, 14*SZ+2*8(a0)
	fld fs3, 14*SZ+3*8(a0)
	fld fs4, 14*SZ+4*8(a0)
	fld fs5, 14*SZ+5*8(a0)
	fld fs6, 14*SZ+6*8(a0)
	fld fs7, 14*SZ+7*8(a0)
	fld fs8, 14*SZ+8*8(a0)
	fld fs9, 14*SZ+9*8(a0)
	fld fs10, 14*SZ+10*8(a0)
	fld fs11, 14*SZ+11*8(a0)
#endif
	seqz a0, a1
	add a0, a0, a1
	ret
	.size longjmp, . - longjmp
