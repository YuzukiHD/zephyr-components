/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * The BIOS and the VGA BIOS of Bochs, linked into the image.
 */
	.section .rodata.bochs_roms, "a"
	.balign 16
	.globl bx_rom_bios
bx_rom_bios:
	.incbin BX_BIOS_FILE
	.globl bx_rom_bios_end
bx_rom_bios_end:
	.balign 16
	.globl bx_rom_vgabios
bx_rom_vgabios:
	.incbin BX_VGABIOS_FILE
	.globl bx_rom_vgabios_end
bx_rom_vgabios_end:
