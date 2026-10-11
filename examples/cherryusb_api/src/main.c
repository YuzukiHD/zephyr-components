/*
 * A composite USB device on the OTG controller of the YuzukiNeko, built on CherryUSB: a CDC ACM
 * virtual serial port that echoes, and a 128 KiB RAM disk. The CherryUSB side is in usb_dev.c.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* the register base of the OTG controller, from the glue code of the board */
extern uintptr_t usb_sunxi_otg_base(void);

extern int usb_dev_start(uintptr_t reg_base);
extern int usb_dev_stop(void);
extern bool usb_dev_configured(void);
extern uint32_t usb_dev_rx_bytes(void);
extern uint32_t usb_dev_tx_bytes(void);
extern uint32_t usb_dev_transfers(void);
extern uint32_t usb_dev_breaks(void);
extern void usb_dev_report_state(void);
extern void usb_dev_report_endpoints(void);
extern void usb_dev_try_remote_wakeup(void);
extern void usb_dev_set_readonly(bool readonly);
extern void usb_dev_msc_report(void);
extern bool usb_dev_suspended(void);

int main(void)
{
	int ret = usb_dev_start(usb_sunxi_otg_base());

	printk("usbd_initialize: %d\n", ret);

	/* wait for the host to configure the device */
	for (int i = 0; i < 60 && !usb_dev_configured(); i++) {
		k_msleep(1000);
	}
	if (!usb_dev_configured()) {
		printk("no host\n");
		return 0;
	}

	usb_dev_set_readonly(false);
	usb_dev_report_state();
	usb_dev_report_endpoints();
	usb_dev_try_remote_wakeup();

	/* report while there is traffic; stop after the serial port has been quiet for 8 seconds */
	uint32_t last = 0;
	int quiet = 0;

	for (int i = 0; i < 90 && quiet < 8; i++) {
		k_msleep(1000);
		if (usb_dev_rx_bytes() != last) {
			last = usb_dev_rx_bytes();
			printk("[echo] received %u bytes in %u transfer(s), sent back %u bytes, %u break(s)\n",
			       last, usb_dev_transfers(), usb_dev_tx_bytes(), usb_dev_breaks());
			quiet = 0;
		} else if (last != 0) {
			quiet++;
		}
	}

	usb_dev_msc_report();

	/* the host suspends an idle device; wake it from the device side when that happens */
	for (int i = 0; i < 40 && !usb_dev_suspended(); i++) {
		k_msleep(1000);
	}
	if (usb_dev_suspended()) {
		printk("suspended by the host\n");
		usb_dev_try_remote_wakeup();
	} else {
		printk("the host did not suspend the device\n");
	}

	ret = usb_dev_stop();
	printk("usbd_deinitialize: %d\n", ret);
	printk("cherryusb_api done\n");
	return 0;
}
