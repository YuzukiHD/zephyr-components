/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* benchncnn of the ncnn library: loops, threads, powersave, gpu, cooling down */
extern int ncnn_bench_main(int argc, char **argv);

int main(void)
{
	char *args[] = {"benchncnn", "4", "1", "0", "-1", "0", NULL};

#ifdef CONFIG_RISCV_ISA_EXT_V
	unsigned long vlenb;

	__asm__ volatile("csrr %0, vlenb" : "=r"(vlenb));
	printk("ncnn: vector extension on, VLEN %lu bits\n", vlenb * 8);
#else
	printk("ncnn: no vector extension\n");
#endif
	printk("ncnn: free heap is all RAM above the image, starting\n");

	int ret = ncnn_bench_main(6, args);

	printk("ncnn benchmark done, ret %d\n", ret);
	return 0;
}
