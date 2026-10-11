/*
 * The CherryUSB side of cherryusb_api: a composite device with a CDC ACM function (virtual serial
 * port, echo) and a mass storage function (a 128 KiB RAM disk holding a FAT12 file system).
 * This file includes no Zephyr header: the CherryUSB headers and the Zephyr ones define some of the
 * same names, so they are kept in separate source files. USB_LOG_RAW is mapped to printk by the
 * board's usb_config.h.
 */
#include <string.h>

#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "usbd_msc.h"

/* endpoint addresses: bit 7 set is IN (device to host). The controller has endpoints 1 to 3 */
#define CDC_IN_EP	0x81
#define CDC_OUT_EP	0x02
#define CDC_INT_EP	0x83
#define MSC_OUT_EP	0x01
#define MSC_IN_EP	0x82

#define USBD_VID	0x1209	/* the vendor ID for test products */
#define USBD_PID	0x0002
#define USBD_MAX_POWER	100	/* in mA */
#define USBD_ATTRIBUTES	(USB_CONFIG_BUS_POWERED | USB_CONFIG_REMOTE_WAKEUP)	/* bus powered, can wake the host */

/* configuration descriptor: 3 interfaces (CDC control, CDC data, mass storage) */
#define USB_CONFIG_SIZE	(9 + CDC_ACM_DESCRIPTOR_LEN + MSC_DESCRIPTOR_LEN)

static const uint8_t device_descriptor[] = {
	USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor_hs[] = {
	USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x03, 0x01, USBD_ATTRIBUTES, USBD_MAX_POWER),
	CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_HS, 0x02),
	MSC_DESCRIPTOR_INIT(0x02, MSC_OUT_EP, MSC_IN_EP, USB_BULK_EP_MPS_HS, 0x00)
};

static const uint8_t config_descriptor_fs[] = {
	USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x03, 0x01, USBD_ATTRIBUTES, USBD_MAX_POWER),
	CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_FS, 0x02),
	MSC_DESCRIPTOR_INIT(0x02, MSC_OUT_EP, MSC_IN_EP, USB_BULK_EP_MPS_FS, 0x00)
};

static const uint8_t device_quality_descriptor[] = {
	USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, 0x01),
};

static const uint8_t other_speed_config_descriptor_hs[] = {
	USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x03, 0x01, USBD_ATTRIBUTES, USBD_MAX_POWER),
	CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_FS, 0x02),
	MSC_DESCRIPTOR_INIT(0x02, MSC_OUT_EP, MSC_IN_EP, USB_BULK_EP_MPS_FS, 0x00)
};

static const uint8_t other_speed_config_descriptor_fs[] = {
	USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x03, 0x01, USBD_ATTRIBUTES, USBD_MAX_POWER),
	CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_HS, 0x02),
	MSC_DESCRIPTOR_INIT(0x02, MSC_OUT_EP, MSC_IN_EP, USB_BULK_EP_MPS_HS, 0x00)
};

/* strings: index 0 is the language (0x0409, English), then manufacturer, product, serial number */
static const char *string_descriptors[] = {
	(const char[]){ 0x09, 0x04 },
	"YuzukiHD",
	"YuzukiNeko CDC+MSC",
	"F101-0002",
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

static const struct usb_descriptor composite_descriptor = {
	.device_descriptor_callback = device_descriptor_callback,
	.config_descriptor_callback = config_descriptor_callback,
	.device_quality_descriptor_callback = device_quality_descriptor_callback,
	.other_speed_descriptor_callback = other_speed_config_descriptor_callback,
	.string_descriptor_callback = string_descriptor_callback
};

/* ------------------------------ CDC ACM ------------------------------ */

#define BUF_SIZE	2048
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t read_buffer[BUF_SIZE];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t write_buffer[BUF_SIZE];

static volatile uint32_t rx_bytes, tx_bytes, rx_transfers, breaks;
static volatile bool configured, dtr;
static struct cdc_line_coding current_coding = { 115200, 0, 0, 8 };

uint32_t usb_dev_rx_bytes(void)
{
	return rx_bytes;
}

uint32_t usb_dev_tx_bytes(void)
{
	return tx_bytes;
}

uint32_t usb_dev_transfers(void)
{
	return rx_transfers;
}

uint32_t usb_dev_breaks(void)
{
	return breaks;
}

bool usb_dev_configured(void)
{
	return configured;
}

static void cdc_start_read(uint8_t busid)
{
	usbd_ep_start_read(busid, CDC_OUT_EP, read_buffer, BUF_SIZE);
}

static void cdc_start_write(uint8_t busid, uint32_t nbytes)
{
	usbd_ep_start_write(busid, CDC_IN_EP, write_buffer, nbytes);
}

/* data from the host arrived: copy it to the write buffer and send it back */
static void cdc_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
	rx_bytes += nbytes;
	rx_transfers++;
	memcpy(write_buffer, read_buffer, nbytes);
	cdc_start_write(busid, nbytes);
}

/* the data went out: a transfer of whole packets ends with a zero length packet, then listen again */
static void cdc_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
	if ((nbytes % usbd_get_ep_mps(busid, ep)) == 0 && nbytes) {
		usbd_ep_start_write(busid, CDC_IN_EP, NULL, 0);
		return;
	}
	tx_bytes += nbytes;
	cdc_start_read(busid);
}

static struct usbd_endpoint cdc_out_ep = { .ep_addr = CDC_OUT_EP, .ep_cb = cdc_bulk_out };
static struct usbd_endpoint cdc_in_ep = { .ep_addr = CDC_IN_EP, .ep_cb = cdc_bulk_in };

void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool state)
{
	dtr = state;
	USB_LOG_RAW("[cdc] set_dtr: intf %u, DTR %d\r\n", intf, state);
}

void usbd_cdc_acm_set_rts(uint8_t busid, uint8_t intf, bool state)
{
	USB_LOG_RAW("[cdc] set_rts: intf %u, RTS %d\r\n", intf, state);
}

void usbd_cdc_acm_set_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
	current_coding = *line_coding;
	USB_LOG_RAW("[cdc] set_line_coding: %u baud, %u data bits, parity %u, stop bits %u\r\n",
		    (unsigned int)line_coding->dwDTERate, line_coding->bDataBits,
		    line_coding->bParityType, line_coding->bCharFormat);
}

void usbd_cdc_acm_get_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
	*line_coding = current_coding;
	USB_LOG_RAW("[cdc] get_line_coding: %u baud\r\n", (unsigned int)line_coding->dwDTERate);
}

void usbd_cdc_acm_send_break(uint8_t busid, uint8_t intf)
{
	breaks++;
	USB_LOG_RAW("[cdc] send_break: intf %u\r\n", intf);
}

/* ------------------------------ MSC RAM disk ------------------------------ */

#define MSC_BLOCK_SIZE	512
#define MSC_BLOCK_NUM	256

/* sectors of the FAT12 volume: boot sector, two FATs, 4 sectors of root directory, data */
#define FAT_SECTOR	1
#define FAT2_SECTOR	2
#define ROOT_SECTOR	3
#define DATA_SECTOR	7

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t ramdisk[MSC_BLOCK_NUM * MSC_BLOCK_SIZE];
static volatile uint32_t msc_read_ops, msc_write_ops, msc_read_bytes, msc_write_bytes;

static const char readme[] = "Hello from CherryUSB on YuzukiNeko\r\n";

/* put a FAT12 volume with one file, README.TXT, into the RAM disk */
static void msc_format(void)
{
	uint8_t *boot = ramdisk;
	uint8_t *root = ramdisk + ROOT_SECTOR * MSC_BLOCK_SIZE;
	static const uint8_t bpb[] = {
		0xEB, 0x3C, 0x90, 'M', 'S', 'D', 'O', 'S', '5', '.', '0',
		0x00, 0x02,	/* 512 bytes per sector */
		0x01,		/* 1 sector per cluster */
		0x01, 0x00,	/* 1 reserved sector */
		0x02,		/* 2 FATs */
		0x40, 0x00,	/* 64 root directory entries */
		0x00, 0x01,	/* 256 sectors */
		0xF8,		/* media descriptor: fixed disk */
		0x01, 0x00,	/* 1 sector per FAT */
		0x20, 0x00, 0x40, 0x00,	/* geometry */
		0x00, 0x00, 0x00, 0x00,	/* hidden sectors */
		0x00, 0x00, 0x00, 0x00,	/* 32 bit sector count, unused */
		0x80, 0x00, 0x29, 0x01, 0x02, 0x03, 0x04,
		'F', '1', '0', '1', 'D', 'I', 'S', 'K', ' ', ' ', ' ',
		'F', 'A', 'T', '1', '2', ' ', ' ', ' ',
	};
	static const uint8_t fat[] = { 0xF8, 0xFF, 0xFF, 0xFF, 0x0F, 0x00 };	/* cluster 2 ends the file */

	memset(ramdisk, 0, sizeof(ramdisk));
	memcpy(boot, bpb, sizeof(bpb));
	boot[510] = 0x55;
	boot[511] = 0xAA;
	memcpy(ramdisk + FAT_SECTOR * MSC_BLOCK_SIZE, fat, sizeof(fat));
	memcpy(ramdisk + FAT2_SECTOR * MSC_BLOCK_SIZE, fat, sizeof(fat));

	/* root directory: the volume label, then README.TXT in cluster 2 */
	memcpy(root, "F101DISK   ", 11);
	root[11] = 0x08;
	memcpy(root + 32, "README  TXT", 11);
	root[32 + 11] = 0x20;
	root[32 + 26] = 2;
	root[32 + 28] = sizeof(readme) - 1;
	memcpy(ramdisk + DATA_SECTOR * MSC_BLOCK_SIZE, readme, sizeof(readme) - 1);
}

/* usbd_msc_get_cap: the stack asks for the size of the medium (called for READ CAPACITY and TEST UNIT READY) */
void usbd_msc_get_cap(uint8_t busid, uint8_t lun, uint32_t *block_num, uint32_t *block_size)
{
	*block_num = MSC_BLOCK_NUM;
	*block_size = MSC_BLOCK_SIZE;
}

/* usbd_msc_sector_read: copy `length` bytes starting at `sector` into the stack's buffer; 0 is success */
int usbd_msc_sector_read(uint8_t busid, uint8_t lun, uint32_t sector, uint8_t *buffer, uint32_t length)
{
	if (sector * MSC_BLOCK_SIZE + length > sizeof(ramdisk)) {
		return -1;
	}
	memcpy(buffer, ramdisk + sector * MSC_BLOCK_SIZE, length);
	msc_read_ops++;
	msc_read_bytes += length;
	return 0;
}

/* usbd_msc_sector_write: store `length` bytes at `sector`; 0 is success */
int usbd_msc_sector_write(uint8_t busid, uint8_t lun, uint32_t sector, uint8_t *buffer, uint32_t length)
{
	if (sector * MSC_BLOCK_SIZE + length > sizeof(ramdisk)) {
		return -1;
	}
	memcpy(ramdisk + sector * MSC_BLOCK_SIZE, buffer, length);
	msc_write_ops++;
	msc_write_bytes += length;
	return 0;
}

/* usbd_msc_set_readonly: make the medium read only (the host sees the write protect bit) */
static void msc_set_readonly(uint8_t busid, bool readonly)
{
	usbd_msc_set_readonly(busid, readonly);
	USB_LOG_RAW("[msc] set_readonly: %d\r\n", readonly);
}

/* usbd_msc_get_popup: true once the host has ejected the medium */
static void msc_report_popup(uint8_t busid)
{
	USB_LOG_RAW("[msc] get_popup: %d\r\n", usbd_msc_get_popup(busid));
}

/* print the MSC counters; usbd_msc_get_cap is also called by hand here */
void usb_dev_msc_report(void)
{
	uint32_t num = 0, size = 0;

	usbd_msc_get_cap(0, 0, &num, &size);
	USB_LOG_RAW("[msc] medium %u blocks of %u bytes, %u reads (%u bytes), %u writes (%u bytes)\r\n",
		    (unsigned int)num, (unsigned int)size, (unsigned int)msc_read_ops,
		    (unsigned int)msc_read_bytes, (unsigned int)msc_write_ops,
		    (unsigned int)msc_write_bytes);
	msc_report_popup(0);
}

/* ------------------------------ device core ------------------------------ */

/* usbd_event_handler_t: events from the bus; CONFIGURED means the host selected the configuration */
static void usbd_event_handler(uint8_t busid, uint8_t event)
{
	switch (event) {
	case USBD_EVENT_RESET:
		USB_LOG_RAW("[usb] event RESET\r\n");
		break;
	case USBD_EVENT_CONNECTED:
		USB_LOG_RAW("[usb] event CONNECTED\r\n");
		break;
	case USBD_EVENT_DISCONNECTED:
		USB_LOG_RAW("[usb] event DISCONNECTED\r\n");
		configured = false;
		break;
	case USBD_EVENT_SUSPEND:
		USB_LOG_RAW("[usb] event SUSPEND\r\n");
		break;
	case USBD_EVENT_RESUME:
		USB_LOG_RAW("[usb] event RESUME\r\n");
		break;
	case USBD_EVENT_CONFIGURED:
		USB_LOG_RAW("[usb] event CONFIGURED\r\n");
		configured = true;
		cdc_start_read(busid);
		break;
	case USBD_EVENT_SET_INTERFACE:
		USB_LOG_RAW("[usb] event SET_INTERFACE\r\n");
		break;
	case USBD_EVENT_INIT:
		USB_LOG_RAW("[usb] event INIT\r\n");
		break;
	case USBD_EVENT_DEINIT:
		USB_LOG_RAW("[usb] event DEINIT\r\n");
		break;
	default:
		break;
	}
}

static struct usbd_interface intf_cdc_ctrl;
static struct usbd_interface intf_cdc_data;
static struct usbd_interface intf_msc;

/* usbd_desc_register: hand the descriptors to the stack */
static void usb_dev_register_descriptors(void)
{
	usbd_desc_register(0, &composite_descriptor);
}

/* usbd_add_interface: CDC has two interfaces, mass storage one */
static void usb_dev_add_interfaces(void)
{
	usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &intf_cdc_ctrl));
	usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &intf_cdc_data));
	usbd_add_interface(0, usbd_msc_init_intf(0, &intf_msc, MSC_OUT_EP, MSC_IN_EP));
}

/* usbd_add_endpoint: the callbacks of the CDC bulk endpoints (the MSC class registers its own) */
static void usb_dev_add_endpoints(void)
{
	usbd_add_endpoint(0, &cdc_out_ep);
	usbd_add_endpoint(0, &cdc_in_ep);
}

/* usbd_initialize: start the controller; from here on the host can enumerate the device */
int usb_dev_start(uintptr_t reg_base)
{
	msc_format();
	usb_dev_register_descriptors();
	usb_dev_add_interfaces();
	usb_dev_add_endpoints();
	return usbd_initialize(0, reg_base, usbd_event_handler);
}

/* usbd_deinitialize: stop the controller and release the PHY */
int usb_dev_stop(void)
{
	return usbd_deinitialize(0);
}

/* usbd_get_device_speed, usb_device_is_configured, usb_device_is_suspend */
void usb_dev_report_state(void)
{
	USB_LOG_RAW("[usb] speed %u (3 = high), configured %d, suspended %d\r\n",
		    usbd_get_device_speed(0), usb_device_is_configured(0), usb_device_is_suspend(0));
}

/* usbd_get_ep_mps, usbd_get_ep_mult */
static void report_endpoint_mps(uint8_t ep)
{
	USB_LOG_RAW("[usb] ep 0x%02x: mps %u, mult %u\r\n", ep, usbd_get_ep_mps(0, ep), usbd_get_ep_mult(0, ep));
}

/* usbd_get_ep_info */
static void report_endpoint_info(uint8_t ep)
{
	struct usbd_endpoint_info info;
	int ret = usbd_get_ep_info(0, ep, &info);

	if (ret) {
		USB_LOG_RAW("[usb] ep 0x%02x: usbd_get_ep_info failed %d\r\n", ep, ret);
		return;
	}
	USB_LOG_RAW("[usb] ep 0x%02x: type %u, mps %u, interval %u\r\n", ep, info.ep_type, info.ep_mps,
		    info.ep_interval);
}

void usb_dev_report_endpoints(void)
{
	static const uint8_t eps[] = { CDC_IN_EP, CDC_OUT_EP, CDC_INT_EP, MSC_OUT_EP, MSC_IN_EP };

	for (unsigned int i = 0; i < sizeof(eps); i++) {
		report_endpoint_mps(eps[i]);
		report_endpoint_info(eps[i]);
	}
}

/* usbd_send_remote_wakeup: only works while suspended, with remote wakeup declared in the
 * configuration descriptor and enabled by the host
 */
void usb_dev_try_remote_wakeup(void)
{
	int ret = usbd_send_remote_wakeup(0);

	USB_LOG_RAW("[usb] usbd_send_remote_wakeup: %d\r\n", ret);
}

void usb_dev_set_readonly(bool readonly)
{
	msc_set_readonly(0, readonly);
}

bool usb_dev_suspended(void)
{
	return usb_device_is_suspend(0);
}
