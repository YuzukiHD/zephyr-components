/*
 * A USB host on the EHCI/OHCI controller of the YuzukiNeko, built on CherryUSB. The CherryUSB side is
 * in usb_host.c. The board has no switch for the 5 V of the USB port (see the devicetree), so a
 * device on the port has to be powered from outside; with none connected the demo only shows the
 * start of the stack.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* the register base of the EHCI controller, from the glue code of the board */
extern uintptr_t usb_sunxi_ehci_base(void);

struct usbh_hubport;

extern int usb_host_start(uintptr_t reg_base);
extern int usb_host_stop(void);
extern void usb_host_lsusb(void);
extern struct usbh_hubport *usb_host_hubport(uint8_t port);
extern void usb_host_report_port(uint8_t port);
extern void usb_host_exercise(struct usbh_hubport *hport);
extern bool usb_host_connected(struct usbh_hubport *hport);

int main(void)
{
	int ret = usb_host_start(usb_sunxi_ehci_base());

	printk("usbh_initialize: %d\n", ret);

	/* look at the root port for 10 seconds */
	for (int i = 0; i < 10; i++) {
		struct usbh_hubport *hport = usb_host_hubport(1);

		if (hport != NULL && usb_host_connected(hport)) {
			usb_host_report_port(1);
			usb_host_exercise(hport);
			break;
		}
		k_msleep(1000);
	}

	usb_host_report_port(1);
	usb_host_lsusb();
	ret = usb_host_stop();
	printk("usbh_deinitialize: %d\n", ret);
	printk("cherryusb_host_api done\n");
	return 0;
}
