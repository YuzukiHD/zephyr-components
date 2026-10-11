/*
 * A USB CDC ACM (virtual serial port) device on the OTG controller of the YuzukiNeko, built on CherryUSB.
 * Everything the host sends to the port is sent back (cdc_echo.c); this file starts it and reports the
 * byte counts on the console.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* the register base of the OTG controller, from the glue code of the board */
extern uintptr_t usb_sunxi_otg_base(void);

extern void cdc_echo_init(uintptr_t reg_base);
extern uint32_t cdc_echo_rx_bytes(void);
extern uint32_t cdc_echo_tx_bytes(void);
extern uint32_t cdc_echo_transfers(void);

int main(void)
{
	cdc_echo_init(usb_sunxi_otg_base());
	printk("usb_api started\n");

	/* report once a second while there is traffic; stop after the host has gone quiet */
	uint32_t last = 0;
	int quiet = 0;

	while (quiet < 5) {
		k_msleep(1000);
		if (cdc_echo_rx_bytes() != last) {
			last = cdc_echo_rx_bytes();
			printk("[echo] received %u bytes in %u transfer(s), sent back %u bytes\n", last,
			       cdc_echo_transfers(), cdc_echo_tx_bytes());
			quiet = 0;
		} else if (last != 0) {
			quiet++;
		}
	}

	printk("usb_api done\n");
	return 0;
}
