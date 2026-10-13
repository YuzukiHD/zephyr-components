/*
 * The static data of the driver (its .data and .bss, not the glue to the Zephyr network
 * interface, the pool or the task bookkeeping) in two sections of their own, so that all of it can
 * be put back to its start values when the driver is shut down (src/aic_state.c). This snippet
 * comes before the sections .bss and .data of the kernel in the linker script, so the files named
 * here are taken by these rules first.
 */
#define AIC_KEEP \
	*libaic8800.a:aic_zephyr_if.c.obj *libaic8800.a:aic_mem.c.obj *libaic8800.a:aic_state.c.obj \
	*libaic8800.a:rtos_al.c.obj *libaic8800.a:aic_log.c.obj *libaic8800.a:aic_libc.c.obj \
	*libaic8800.a:aic_bt.c.obj

SECTION_DATA_PROLOGUE(.aic_bss, (NOLOAD),)
{
	. = ALIGN(8);
	__aic_bss_start = .;
	EXCLUDE_FILE(AIC_KEEP) *libaic8800.a:*(.bss .bss.* .sbss .sbss.*)
	. = ALIGN(8);
	__aic_bss_end = .;
} GROUP_NOLOAD_LINK_IN(RAMABLE_REGION, RAMABLE_REGION)

SECTION_DATA_PROLOGUE(.aic_data,,)
{
	. = ALIGN(8);
	__aic_data_start = .;
	EXCLUDE_FILE(AIC_KEEP) *libaic8800.a:*(.data .data.* .sdata .sdata.*)
	. = ALIGN(8);
	__aic_data_end = .;
} GROUP_DATA_LINK_IN(RAMABLE_REGION, ROMABLE_REGION)
__aic_data_load_start = LOADADDR(.aic_data);
