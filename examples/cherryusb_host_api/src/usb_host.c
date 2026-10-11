/*
 * The CherryUSB side of cherryusb_host_api: the host stack on the EHCI/OHCI controller. It starts
 * the stack, reports the events and the device on the root port, and has one small function per
 * class driver call (serial, mass storage, HID). This file includes no Zephyr header: the
 * CherryUSB headers and the Zephyr ones define some of the same names. USB_LOG_RAW is mapped to
 * printk by the board's usb_config.h.
 */
#include <string.h>

#include "usbh_core.h"
#include "usbh_msc.h"
#include "usbh_hid.h"
#include "usbh_serial.h"

static const char *event_name(uint8_t event)
{
	switch (event) {
	case USBH_EVENT_DEVICE_RESET:
		return "DEVICE_RESET";
	case USBH_EVENT_DEVICE_CONNECTED:
		return "DEVICE_CONNECTED";
	case USBH_EVENT_DEVICE_DISCONNECTED:
		return "DEVICE_DISCONNECTED";
	case USBH_EVENT_DEVICE_CONFIGURED:
		return "DEVICE_CONFIGURED";
	case USBH_EVENT_INTERFACE_UNSUPPORTED:
		return "INTERFACE_UNSUPPORTED";
	case USBH_EVENT_INTERFACE_START:
		return "INTERFACE_START";
	case USBH_EVENT_INTERFACE_STOP:
		return "INTERFACE_STOP";
	case USBH_EVENT_INIT:
		return "INIT";
	case USBH_EVENT_DEINIT:
		return "DEINIT";
	default:
		return "other";
	}
}

/* usbh_event_handler_t: events of the bus, the devices behind it and their interfaces */
static void usbh_event_handler(uint8_t busid, uint8_t hub_index, uint8_t hub_port, uint8_t intf, uint8_t event)
{
	USB_LOG_RAW("[host] event %s (bus %u, hub %u, port %u, interface %u)\r\n", event_name(event), busid,
		    hub_index, hub_port, intf);
}

/* usbh_initialize: bring up the controller and the root hub. From here on a device on the port is enumerated */
int usb_host_start(uintptr_t reg_base)
{
	return usbh_initialize(0, reg_base, usbh_event_handler);
}

/* usbh_deinitialize: stop the root hub thread and release the controller */
int usb_host_stop(void)
{
	return usbh_deinitialize(0);
}

/* lsusb: list the devices in a tree (the arguments are those of the shell command) */
void usb_host_lsusb(void)
{
	char *argv[] = { "lsusb", "-t" };

	lsusb(2, argv);
}

/* usbh_find_hubport: the port of a hub; the root hub is hub 1, ports count from 1. NULL when no device is connected there */
struct usbh_hubport *usb_host_hubport(uint8_t port)
{
	return usbh_find_hubport(0, 1, port);
}

/* print what the enumeration has learned about the device on a port */
void usb_host_report_port(uint8_t port)
{
	struct usbh_hubport *hport = usbh_find_hubport(0, 1, port);

	if (hport == NULL || !hport->connected) {
		USB_LOG_RAW("[host] port %u: no device\r\n", port);
		return;
	}
	USB_LOG_RAW("[host] port %u: address %u, speed %u, VID %04x PID %04x, class %02x, %s %s\r\n", port,
		    hport->dev_addr, hport->speed, hport->device_desc.idVendor, hport->device_desc.idProduct,
		    hport->device_desc.bDeviceClass, hport->iManufacturer ? hport->iManufacturer : "",
		    hport->iProduct ? hport->iProduct : "");
}

/* usbh_get_hport_active_config_index: which configuration the stack selected */
uint8_t usb_host_config_index(struct usbh_hubport *hport)
{
	return usbh_get_hport_active_config_index(hport);
}

/* usbh_control_transfer: a GET_DESCRIPTOR(device) request on endpoint 0 by hand; the buffer must be aligned */
int usb_host_read_device_descriptor(struct usbh_hubport *hport)
{
	static struct usb_setup_packet setup;
	static uint8_t buffer[18] USB_MEM_ALIGNX;
	int ret;

	setup.bmRequestType = USB_REQUEST_DIR_IN | USB_REQUEST_STANDARD | USB_REQUEST_RECIPIENT_DEVICE;
	setup.bRequest = USB_REQUEST_GET_DESCRIPTOR;
	setup.wValue = (USB_DESCRIPTOR_TYPE_DEVICE << 8) | 0;
	setup.wIndex = 0;
	setup.wLength = sizeof(buffer);

	ret = usbh_control_transfer(hport, &setup, buffer);
	if (ret < 0) {
		USB_LOG_RAW("[host] control transfer failed: %d\r\n", ret);
		return ret;
	}
	USB_LOG_RAW("[host] device descriptor: bcdUSB %02x%02x, VID %02x%02x, PID %02x%02x\r\n", buffer[3],
		    buffer[2], buffer[9], buffer[8], buffer[11], buffer[10]);
	return 0;
}

/* usbh_get_string_desc: a string descriptor, UTF-16LE with a two byte header */
int usb_host_read_string(struct usbh_hubport *hport, uint8_t index)
{
	static uint8_t buffer[64] USB_MEM_ALIGNX;
	int ret = usbh_get_string_desc(hport, index, buffer, sizeof(buffer));

	if (ret < 0) {
		USB_LOG_RAW("[host] string %u failed: %d\r\n", index, ret);
		return ret;
	}
	USB_LOG_RAW("[host] string %u: %u bytes\r\n", index, buffer[0]);
	return 0;
}

/* usbh_set_interface: select an alternate setting of an interface */
int usb_host_set_alt(struct usbh_hubport *hport, uint8_t intf, uint8_t alt)
{
	int ret = usbh_set_interface(hport, intf, alt);

	USB_LOG_RAW("[host] set interface %u alt %u: %d\r\n", intf, alt, ret);
	return ret;
}

/* usbh_find_class_instance: the class driver instance of a device name, NULL when there is none:
 * /dev/sda (mass storage), /dev/ttyACM0 (CDC ACM), /dev/ttyUSB0 (adapters), /dev/input0 (HID)
 */
void *usb_host_find(const char *name)
{
	void *inst = usbh_find_class_instance(name);

	USB_LOG_RAW("[host] %s: %s\r\n", name, inst ? "present" : "absent");
	return inst;
}

/* ------------------------------ serial ------------------------------ */

/* usbh_serial_open, usbh_serial_control, usbh_serial_write, usbh_serial_read, usbh_serial_close */
int usb_host_serial_loopback(const char *name)
{
	static uint8_t tx[32] USB_MEM_ALIGNX;
	static uint8_t rx[32] USB_MEM_ALIGNX;
	struct usbh_serial_termios termios = {
		.baudrate = 115200,
		.databits = USBH_SERIAL_DATABITS_8,
		.parity = USBH_SERIAL_PARITY_NONE,
		.stopbits = USBH_SERIAL_STOPBITS_1,
		.rtscts = false,
		.rx_timeout = 500,
	};
	struct usbh_serial *serial = usbh_serial_open(name, USBH_SERIAL_O_RDWR);
	int ret, got;

	if (serial == NULL) {
		USB_LOG_RAW("[serial] cannot open %s\r\n", name);
		return -1;
	}
	ret = usbh_serial_control(serial, USBH_SERIAL_CMD_SET_ATTR, &termios);
	USB_LOG_RAW("[serial] set 115200 8N1: %d\r\n", ret);

	strcpy((char *)tx, "hello serial");
	ret = usbh_serial_write(serial, tx, strlen((char *)tx));
	USB_LOG_RAW("[serial] write: %d\r\n", ret);

	memset(rx, 0, sizeof(rx));
	got = usbh_serial_read(serial, rx, sizeof(rx));
	USB_LOG_RAW("[serial] read: %d \"%s\"\r\n", got, got > 0 ? (char *)rx : "");

	usbh_serial_close(serial);
	return got;
}

/* usbh_serial_run, usbh_serial_stop: the class driver calls these when a serial device appears or goes */
void usbh_serial_run(struct usbh_serial *serial)
{
	USB_LOG_RAW("[serial] run: minor %d\r\n", serial->minor);
}

void usbh_serial_stop(struct usbh_serial *serial)
{
	USB_LOG_RAW("[serial] stop: minor %d\r\n", serial->minor);
}

/* ------------------------------ mass storage ------------------------------ */

/* usbh_msc_run, usbh_msc_stop */
void usbh_msc_run(struct usbh_msc *msc)
{
	USB_LOG_RAW("[msc] run: /dev/sd%c\r\n", msc->sdchar);
}

void usbh_msc_stop(struct usbh_msc *msc)
{
	USB_LOG_RAW("[msc] stop: /dev/sd%c\r\n", msc->sdchar);
}

/* usbh_msc_scsi_init: INQUIRY, TEST UNIT READY and READ CAPACITY; fills in blocknum and blocksize */
int usb_host_msc_init(struct usbh_msc *msc)
{
	int ret = usbh_msc_scsi_init(msc);

	USB_LOG_RAW("[msc] scsi init: %d, %u blocks of %u bytes\r\n", ret, (unsigned int)msc->blocknum,
		    msc->blocksize);
	return ret;
}

/* usbh_msc_scsi_read10: read sectors; the buffer must be aligned */
int usb_host_msc_read(struct usbh_msc *msc, uint32_t sector)
{
	static uint8_t block[512] USB_MEM_ALIGNX;
	int ret = usbh_msc_scsi_read10(msc, sector, block, 1);

	USB_LOG_RAW("[msc] read sector %u: %d, signature %02x%02x\r\n", (unsigned int)sector, ret, block[510],
		    block[511]);
	return ret;
}

/* usbh_msc_scsi_write10: write sectors */
int usb_host_msc_write(struct usbh_msc *msc, uint32_t sector)
{
	static uint8_t block[512] USB_MEM_ALIGNX;
	int ret;

	memset(block, 0xA5, sizeof(block));
	ret = usbh_msc_scsi_write10(msc, sector, block, 1);
	USB_LOG_RAW("[msc] write sector %u: %d\r\n", (unsigned int)sector, ret);
	return ret;
}

/* ------------------------------ HID ------------------------------ */

/* usbh_hid_run, usbh_hid_stop */
void usbh_hid_run(struct usbh_hid *hid)
{
	USB_LOG_RAW("[hid] run: /dev/input%u\r\n", hid->minor);
}

void usbh_hid_stop(struct usbh_hid *hid)
{
	USB_LOG_RAW("[hid] stop: /dev/input%u\r\n", hid->minor);
}

/* usbh_hid_get_report_descriptor */
int usb_host_hid_report_descriptor(struct usbh_hid *hid)
{
	static uint8_t desc[256] USB_MEM_ALIGNX;
	int ret = usbh_hid_get_report_descriptor(hid, desc, sizeof(desc));

	USB_LOG_RAW("[hid] report descriptor: %d\r\n", ret);
	return ret;
}

/* usbh_hid_set_idle, usbh_hid_get_idle */
int usb_host_hid_idle(struct usbh_hid *hid)
{
	static uint8_t idle[1] USB_MEM_ALIGNX;
	int ret = usbh_hid_set_idle(hid, 0, 0);

	USB_LOG_RAW("[hid] set idle: %d\r\n", ret);
	ret = usbh_hid_get_idle(hid, idle);
	USB_LOG_RAW("[hid] get idle: %d, %u\r\n", ret, idle[0]);
	return ret;
}

/* usbh_hid_set_protocol, usbh_hid_get_protocol: boot protocol (0) or report protocol (1) */
int usb_host_hid_protocol(struct usbh_hid *hid)
{
	static uint8_t protocol[1] USB_MEM_ALIGNX;
	int ret = usbh_hid_set_protocol(hid, HID_PROTOCOL_BOOT);

	USB_LOG_RAW("[hid] set protocol: %d\r\n", ret);
	ret = usbh_hid_get_protocol(hid, protocol);
	USB_LOG_RAW("[hid] get protocol: %d, %u\r\n", ret, protocol[0]);
	return ret;
}

/* usbh_hid_get_report, usbh_hid_set_report: an input report and an output report (the LEDs of a keyboard) */
int usb_host_hid_report(struct usbh_hid *hid)
{
	static uint8_t in[8] USB_MEM_ALIGNX;
	static uint8_t leds[1] USB_MEM_ALIGNX = { 0x02 };
	int ret = usbh_hid_get_report(hid, HID_REPORT_INPUT, 0, in, sizeof(in));

	USB_LOG_RAW("[hid] get report: %d\r\n", ret);
	ret = usbh_hid_set_report(hid, HID_REPORT_OUTPUT, 0, leds, sizeof(leds));
	USB_LOG_RAW("[hid] set report: %d\r\n", ret);
	return ret;
}

/* usbh_hid_parse_report_descriptor: split a report descriptor into items */
int usb_host_hid_parse(const uint8_t *desc, uint32_t len)
{
	static struct usbh_hid_report_info info;
	int ret = usbh_hid_parse_report_descriptor(desc, len, &info);

	USB_LOG_RAW("[hid] parse: %d, %u items\r\n", ret, (unsigned int)info.report_item_count);
	return ret;
}

/* what to do once the device on the port is known: only what its class driver offers */
void usb_host_exercise(struct usbh_hubport *hport)
{
	void *inst;

	usb_host_read_device_descriptor(hport);
	usb_host_read_string(hport, hport->device_desc.iProduct);
	USB_LOG_RAW("[host] active configuration index %u\r\n", usb_host_config_index(hport));
	usb_host_set_alt(hport, 0, 0);

	inst = usb_host_find("/dev/sda");
	if (inst) {
		usb_host_msc_init(inst);
		usb_host_msc_read(inst, 0);
		usb_host_msc_write(inst, 1);
	}
	if (usb_host_find("/dev/ttyACM0")) {
		usb_host_serial_loopback("/dev/ttyACM0");
	}
	inst = usb_host_find("/dev/input0");
	if (inst) {
		usb_host_hid_report_descriptor(inst);
		usb_host_hid_idle(inst);
		usb_host_hid_protocol(inst);
		usb_host_hid_report(inst);
	}
}

bool usb_host_connected(struct usbh_hubport *hport)
{
	return hport->connected;
}
