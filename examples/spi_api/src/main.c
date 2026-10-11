/*
 * Calls the functions of <zephyr/drivers/spi.h> on SPI0 of the YuzukiNeko, where the 16 MiB SPI NOR flash
 * of the board sits on PC0..PC5. Only read commands are sent, the flash holds the boot loader.
 */
#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/spi/spi_sunxi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

static const struct device *const spi = DEVICE_DT_GET(DT_NODELABEL(spi0));

/* SPI NOR commands */
#define CMD_READ_STATUS	0x05
#define CMD_READ_DATA	0x03
#define CMD_JEDEC_ID	0x9f

/* chip select 0 (the hardware CS pin), mode 0, 8 bit words, most significant bit first */
static struct spi_config cfg_at(uint32_t hz, uint32_t mode)
{
	return (struct spi_config){
		.frequency = hz,
		.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | mode,
		.slave = 0,
	};
}

static const char *err(int ret)
{
	switch (ret) {
	case 0:		return "0";
	case -ENOTSUP:	return "-ENOTSUP";
	case -EINVAL:	return "-EINVAL";
	case -EIO:	return "-EIO";
	case -ETIMEDOUT: return "-ETIMEDOUT";
	default:	return "?";
	}
}

/*
 * spi_transceive with a command phase: tx is 1 byte, rx is 3 bytes. When the two lengths differ the
 * driver throws away what comes in while the tx bytes go out, so rx[0] is the first byte after the
 * command. (With the same length on both sides it is a plain full duplex transfer, see below.)
 */
static void demo_command_read(void)
{
	struct spi_config cfg = cfg_at(1000000, 0);
	uint8_t cmd = CMD_JEDEC_ID;
	uint8_t id[3] = { 0 };
	struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
	struct spi_buf rx_buf = { .buf = id, .len = sizeof(id) };
	struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };
	int ret;

	ret = spi_transceive(spi, &cfg, &tx, &rx);
	printk("[command] JEDEC ID (tx 1 byte, rx 3 bytes)   = %s: %02x %02x %02x\n", err(ret), id[0],
	       id[1], id[2]);
}

/*
 * spi_transceive with equal lengths is a plain full duplex transfer: every tx byte clocks one rx
 * byte in, so the byte received while the command goes out is not data.
 */
static void demo_full_duplex(void)
{
	struct spi_config cfg = cfg_at(1000000, 0);
	uint8_t out[4] = { CMD_JEDEC_ID, 0, 0, 0 };
	uint8_t in[4] = { 0xaa, 0xaa, 0xaa, 0xaa };
	struct spi_buf tx_buf = { .buf = out, .len = sizeof(out) };
	struct spi_buf rx_buf = { .buf = in, .len = sizeof(in) };
	struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };
	int ret;

	ret = spi_transceive(spi, &cfg, &tx, &rx);
	printk("[duplex] JEDEC ID (4 bytes both ways)         = %s: %02x %02x %02x %02x\n", err(ret),
	       in[0], in[1], in[2], in[3]);

	/* a command and one byte of answer: two bytes both ways, the answer is the second one */
	out[0] = CMD_READ_STATUS;
	out[1] = 0;
	tx_buf.len = 2;
	rx_buf.len = 2;
	ret = spi_transceive(spi, &cfg, &tx, &rx);
	printk("[duplex] status register (2 bytes both ways)  = %s: 0x%02x\n", err(ret), in[1]);
}

/* spi_write: only sends. A write of the command alone and then a separate read is not what a NOR flash
 * wants (it needs the command and the read in the same chip select), but it is what spi_write does.
 */
static void demo_spi_write(void)
{
	struct spi_config cfg = cfg_at(1000000, 0);
	uint8_t cmd = CMD_JEDEC_ID;
	struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
	struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
	int ret = spi_write(spi, &cfg, &tx);

	printk("[split] spi_write(command)                    = %s\n", err(ret));
}

/* spi_read: only receives, the chip select is a new one, so the flash has not seen a command */
static void demo_spi_read(void)
{
	struct spi_config cfg = cfg_at(1000000, 0);
	uint8_t id[3] = { 0 };
	struct spi_buf rx_buf = { .buf = id, .len = sizeof(id) };
	struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };
	int ret = spi_read(spi, &cfg, &rx);

	printk("[split] spi_read(3 bytes), chip select again  = %s: %02x %02x %02x\n", err(ret), id[0],
	       id[1], id[2]);
}

/* A longer read: the first 16 bytes of the flash, and the speed at several clocks.
 * Reads of 256 bytes or more into a 64 byte (cache line) aligned buffer go through the DMA controller.
 */
static void demo_read_flash(void)
{
	static uint8_t page[4096] __aligned(64);
	static uint8_t first[4096];
	struct spi_config cfg = cfg_at(1000000, 0);
	uint8_t cmd[4] = { CMD_READ_DATA, 0, 0, 0 };		/* 24 bit address 0 */
	struct spi_buf tx_buf = { .buf = cmd, .len = sizeof(cmd) };
	struct spi_buf rx_buf = { .buf = page, .len = sizeof(page) };
	struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };
	static const uint32_t rates[] = { 1000000, 6000000, 24000000, 48000000 };
	int ret;

	ret = spi_transceive(spi, &cfg, &tx, &rx);
	printk("[read] 4096 bytes at address 0 at 1 MHz        = %s\n", err(ret));
	printk("[read] first 16 bytes:");
	for (int i = 0; i < 16; i++) {
		printk(" %02x", page[i]);
	}
	printk("\n");
	memcpy(first, page, sizeof(page));

	for (int i = 0; i < ARRAY_SIZE(rates); i++) {
		cfg = cfg_at(rates[i], 0);
		memset(page, 0, sizeof(page));
		int64_t t0 = k_uptime_ticks();

		ret = spi_transceive(spi, &cfg, &tx, &rx);
		int64_t us = k_ticks_to_us_near64(k_uptime_ticks() - t0);

		printk("[read] 4096 bytes at %2u MHz = %s, %lld us, %s\n", rates[i] / 1000000, err(ret),
		       us, memcmp(first, page, sizeof(page)) == 0 ? "same data" : "DIFFERENT data");
	}

	/* a buffer that is not 64 byte aligned is read by the CPU */
	static uint8_t odd[1 + 512] __aligned(64);

	rx_buf.buf = odd + 1;
	rx_buf.len = 512;
	cfg = cfg_at(24000000, 0);
	ret = spi_transceive(spi, &cfg, &tx, &rx);
	printk("[read] 512 bytes into an unaligned buffer      = %s, %s\n", err(ret),
	       memcmp(first, odd + 1, 512) == 0 ? "same data" : "DIFFERENT data");
}

/* SPI mode 0 (CPOL 0, CPHA 0) and mode 3 (CPOL 1, CPHA 1) both work with a NOR flash */
static void demo_modes(void)
{
	uint8_t cmd = CMD_JEDEC_ID;
	uint8_t id[3];
	struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
	struct spi_buf rx_buf = { .buf = id, .len = sizeof(id) };
	struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };
	static const struct {
		const char *name;
		uint32_t mode;
	} modes[] = {
		{ "mode 0 (CPOL 0, CPHA 0)", 0 },
		{ "mode 1 (CPOL 0, CPHA 1)", SPI_MODE_CPHA },
		{ "mode 2 (CPOL 1, CPHA 0)", SPI_MODE_CPOL },
		{ "mode 3 (CPOL 1, CPHA 1)", SPI_MODE_CPOL | SPI_MODE_CPHA },
	};

	for (int i = 0; i < ARRAY_SIZE(modes); i++) {
		struct spi_config cfg = cfg_at(1000000, modes[i].mode);
		int ret;

		memset(id, 0, sizeof(id));
		ret = spi_transceive(spi, &cfg, &tx, &rx);
		printk("[mode] %s = %s: %02x %02x %02x\n", modes[i].name, err(ret), id[0], id[1], id[2]);
	}
}

/* what the driver refuses */
static void demo_unsupported(void)
{
	uint8_t cmd = CMD_JEDEC_ID;
	uint8_t id[3];
	struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
	struct spi_buf rx_buf = { .buf = id, .len = sizeof(id) };
	struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };
	struct spi_config cfg;
	int ret;

	cfg = cfg_at(1000000, 0);
	cfg.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(16);
	ret = spi_transceive(spi, &cfg, &tx, &rx);
	printk("[refuse] 16 bit words                        = %s\n", err(ret));

	cfg = cfg_at(1000000, SPI_MODE_LOOP);
	ret = spi_transceive(spi, &cfg, &tx, &rx);
	printk("[refuse] SPI_MODE_LOOP                       = %s\n", err(ret));

	cfg = cfg_at(1000000, SPI_HOLD_ON_CS);
	ret = spi_transceive(spi, &cfg, &tx, &rx);
	printk("[refuse] SPI_HOLD_ON_CS                      = %s\n", err(ret));

	cfg = cfg_at(1000000, 0);
	cfg.operation = SPI_OP_MODE_SLAVE | SPI_WORD_SET(8);
	ret = spi_transceive(spi, &cfg, &tx, &rx);
	printk("[refuse] slave mode                          = %s\n", err(ret));

	cfg = cfg_at(1000000, 0);
	cfg.slave = 4;
	ret = spi_transceive(spi, &cfg, &tx, &rx);
	printk("[refuse] chip select 4                       = %s\n", err(ret));

	cfg = cfg_at(1000, 0);
	ret = spi_transceive(spi, &cfg, &tx, &rx);
	printk("[refuse] 1 kHz                               = %s\n", err(ret));

	/* nothing to send or receive */
	struct spi_buf_set none = { .buffers = NULL, .count = 0 };

	cfg = cfg_at(1000000, 0);
	ret = spi_transceive(spi, &cfg, &none, &none);
	printk("[refuse] empty transfer                      = %s\n", err(ret));

}

/* sunxi_spi_set_sample: choose the sample point of the returning data (mode 0..6, delay 0..63) */
static void demo_set_sample(void)
{
	int ret;

	ret = sunxi_spi_set_sample(spi, 7, 0);
	printk("[sample] sunxi_spi_set_sample(mode 7)        = %s\n", err(ret));
	ret = sunxi_spi_set_sample(spi, 0, 64);
	printk("[sample] sunxi_spi_set_sample(delay 64)      = %s\n", err(ret));
	ret = sunxi_spi_set_sample(spi, 0, 0);
	printk("[sample] sunxi_spi_set_sample(mode 0, delay 0) = %s\n", err(ret));
}

/* spi_release unlocks the bus that a spi_config with SPI_LOCK_ON holds. */
static void demo_release(void)
{
	struct spi_config cfg = cfg_at(1000000, SPI_LOCK_ON);
	uint8_t cmd = CMD_JEDEC_ID;
	uint8_t id[3] = { 0 };
	struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
	struct spi_buf rx_buf = { .buf = id, .len = sizeof(id) };
	struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };
	int ret;

	ret = spi_transceive(spi, &cfg, &tx, &rx);
	printk("[release] transfer with SPI_LOCK_ON            = %s: %02x %02x %02x\n", err(ret), id[0],
	       id[1], id[2]);
	ret = spi_release(spi, &cfg);
	printk("[release] spi_release                          = %s\n", err(ret));
}

int main(void)
{
	if (!device_is_ready(spi)) {
		printk("spi not ready\n");
		return 0;
	}

	demo_command_read();
	demo_full_duplex();
	demo_spi_write();
	demo_spi_read();
	demo_read_flash();
	demo_modes();
	demo_unsupported();
	demo_set_sample();
	demo_release();

	printk("spi_api done\n");
	return 0;
}
