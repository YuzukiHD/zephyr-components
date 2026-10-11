/*
 * The CDC ACM (virtual serial port) function of usb_api, built on CherryUSB. Everything the host sends
 * to the port is sent back. This file includes no Zephyr header: the CherryUSB headers and the
 * Zephyr ones define some of the same names, so they are kept in separate source files. CherryUSB
 * prints with USB_LOG_RAW, which the board's usb_config.h maps to printk.
 */
#include <string.h>

#include "usbd_core.h"
#include "usbd_cdc_acm.h"

/* endpoint addresses: bit 7 set is IN (device to host) */
#define CDC_IN_EP	0x81
#define CDC_OUT_EP	0x02
#define CDC_INT_EP	0x83

#define USBD_VID	0x1209	/* a vendor ID is needed for a product; this one is for test products */
#define USBD_PID	0x0001
#define USBD_MAX_POWER	100	/* in 2 mA units of bMaxPower: 100 mA */

#define USB_CONFIG_SIZE	(9 + CDC_ACM_DESCRIPTOR_LEN)

/* device descriptor: USB 2.0, class 0xEF/2/1 = interface association (a CDC function) */
static const uint8_t device_descriptor[] = {
	USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)
};

/* configuration descriptor: one configuration with the two CDC interfaces and three endpoints.
 * The bulk packet size is 512 bytes at high speed and 64 bytes at full speed.
 */
static const uint8_t config_descriptor_hs[] = {
	USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
	CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_HS, 0x02)
};

static const uint8_t config_descriptor_fs[] = {
	USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
	CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_FS, 0x02)
};

/* a high speed device also describes how it would look at full speed, and the other way round */
static const uint8_t device_quality_descriptor[] = {
	USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, 0x01),
};

static const uint8_t other_speed_config_descriptor_hs[] = {
	USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
	CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_FS, 0x02)
};

static const uint8_t other_speed_config_descriptor_fs[] = {
	USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
	CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_HS, 0x02)
};

/* strings: index 0 is the language (0x0409, English), then the three that the descriptors name */
static const char *string_descriptors[] = {
	(const char[]){ 0x09, 0x04 },
	"YuzukiHD",
	"YuzukiNeko CDC ACM",
	"F101-0001",
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
	(void)speed;
	return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
	return speed == USB_SPEED_HIGH ? config_descriptor_hs :
	       speed == USB_SPEED_FULL ? config_descriptor_fs : NULL;
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
	(void)speed;
	return device_quality_descriptor;
}

static const uint8_t *other_speed_config_descriptor_callback(uint8_t speed)
{
	return speed == USB_SPEED_HIGH ? other_speed_config_descriptor_hs :
	       speed == USB_SPEED_FULL ? other_speed_config_descriptor_fs : NULL;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
	(void)speed;
	return index < (sizeof(string_descriptors) / sizeof(string_descriptors[0])) ? string_descriptors[index] : NULL;
}

static const struct usb_descriptor cdc_descriptor = {
	.device_descriptor_callback = device_descriptor_callback,
	.config_descriptor_callback = config_descriptor_callback,
	.device_quality_descriptor_callback = device_quality_descriptor_callback,
	.other_speed_descriptor_callback = other_speed_config_descriptor_callback,
	.string_descriptor_callback = string_descriptor_callback
};

/* the transfer buffers live in non-cached memory and are aligned for the DMA/FIFO access */
#define BUF_SIZE	2048
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t read_buffer[BUF_SIZE];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t write_buffer[BUF_SIZE];

static volatile uint32_t rx_bytes, tx_bytes, rx_transfers;
static volatile bool configured, dtr;

uint32_t cdc_echo_rx_bytes(void)
{
	return rx_bytes;
}

uint32_t cdc_echo_tx_bytes(void)
{
	return tx_bytes;
}

uint32_t cdc_echo_transfers(void)
{
	return rx_transfers;
}

/* usbd_ep_start_read: wait for the host to send up to BUF_SIZE bytes; the OUT endpoint callback
 * runs when they have arrived
 */
static void cdc_start_read(uint8_t busid)
{
	usbd_ep_start_read(busid, CDC_OUT_EP, read_buffer, BUF_SIZE);
}

/* usbd_ep_start_write: send nbytes of the write buffer to the host; the IN endpoint callback runs
 * when they are out
 */
static void cdc_start_write(uint8_t busid, uint32_t nbytes)
{
	usbd_ep_start_write(busid, CDC_IN_EP, write_buffer, nbytes);
}

/* the events of the bus */
static void usbd_event_handler(uint8_t busid, uint8_t event)
{
	switch (event) {
	case USBD_EVENT_RESET:
		USB_LOG_RAW("[usb] reset\r\n");
		break;
	case USBD_EVENT_CONNECTED:
		USB_LOG_RAW("[usb] connected\r\n");
		break;
	case USBD_EVENT_DISCONNECTED:
		USB_LOG_RAW("[usb] disconnected\r\n");
		configured = false;
		break;
	case USBD_EVENT_SUSPEND:
		USB_LOG_RAW("[usb] suspend\r\n");
		break;
	case USBD_EVENT_RESUME:
		USB_LOG_RAW("[usb] resume\r\n");
		break;
	case USBD_EVENT_CONFIGURED:
		USB_LOG_RAW("[usb] configured, bulk packet size %u\r\n", usbd_get_ep_mps(busid, CDC_OUT_EP));
		configured = true;
		/* the first read: wait for data from the host */
		cdc_start_read(busid);
		break;
	default:
		break;
	}
}

/* data from the host arrived: copy it to the write buffer and send it back */
static void usbd_cdc_acm_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
	rx_bytes += nbytes;
	rx_transfers++;
	memcpy(write_buffer, read_buffer, nbytes);
	cdc_start_write(busid, nbytes);
}

/* the data went out: end the transfer with a zero length packet when it filled whole packets, then
 * listen again
 */
static void usbd_cdc_acm_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
	if ((nbytes % usbd_get_ep_mps(busid, ep)) == 0 && nbytes) {
		usbd_ep_start_write(busid, CDC_IN_EP, NULL, 0);
		return;
	}
	tx_bytes += nbytes;
	cdc_start_read(busid);
}

static struct usbd_endpoint cdc_out_ep = {
	.ep_addr = CDC_OUT_EP,
	.ep_cb = usbd_cdc_acm_bulk_out
};

static struct usbd_endpoint cdc_in_ep = {
	.ep_addr = CDC_IN_EP,
	.ep_cb = usbd_cdc_acm_bulk_in
};

static struct usbd_interface intf0;
static struct usbd_interface intf1;

/* the class driver calls these when the host sets the control lines and the line coding */
void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool state)
{
	dtr = state;
	USB_LOG_RAW("[cdc] DTR %d\r\n", state);
}

void usbd_cdc_acm_set_rts(uint8_t busid, uint8_t intf, bool state)
{
	USB_LOG_RAW("[cdc] RTS %d\r\n", state);
}

void usbd_cdc_acm_set_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
	USB_LOG_RAW("[cdc] line coding: %u baud, %u data bits, parity %u, stop bits %u\r\n",
	       (unsigned int)line_coding->dwDTERate, line_coding->bDataBits, line_coding->bParityType,
	       line_coding->bCharFormat);
}

/* usbd_desc_register: hand the descriptors to the stack */
static void cdc_register_descriptors(void)
{
	usbd_desc_register(0, &cdc_descriptor);
}

/* usbd_add_interface: the two interfaces of a CDC function, control and data */
static void cdc_add_interfaces(void)
{
	usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &intf0));
	usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &intf1));
}

/* usbd_add_endpoint: the callbacks of the bulk endpoints */
static void cdc_add_endpoints(void)
{
	usbd_add_endpoint(0, &cdc_out_ep);
	usbd_add_endpoint(0, &cdc_in_ep);
}

/* usbd_initialize: start the controller; from here on the host can enumerate the device */
static void cdc_start(uintptr_t reg_base)
{
	usbd_initialize(0, reg_base, usbd_event_handler);
}

void cdc_echo_init(uintptr_t reg_base)
{
	cdc_register_descriptors();
	cdc_add_interfaces();
	cdc_add_endpoints();
	cdc_start(reg_base);
}
