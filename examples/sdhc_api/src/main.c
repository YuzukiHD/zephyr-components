/*
 * Calls every function of <zephyr/drivers/sdhc.h> on SMHC0 (the TF card slot of the YuzukiNeko). There is no disk
 * or file system layer here: the card is brought up by hand with the commands of the SD
 * specification, and a block is read with CMD17 and CMD18.
 */
#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/kernel.h>
#include <zephyr/sd/sd_spec.h>
#include <zephyr/sys/printk.h>

static const struct device *const sdhc = DEVICE_DT_GET(DT_NODELABEL(smhc0));

static uint32_t rca;		/* relative card address, from CMD3 */
static bool block_addressed;	/* SDHC and SDXC cards: the address of a read is a block number */

static const char *err(int ret)
{
	switch (ret) {
	case 0:		return "0";
	case -EIO:	return "-EIO";
	case -EINVAL:	return "-EINVAL";
	case -ENOTSUP:	return "-ENOTSUP";
	case -ETIMEDOUT: return "-ETIMEDOUT";
	case -ENOSYS:	return "-ENOSYS";
	default:	return "?";
	}
}

/* one command without data */
static int send(uint32_t opcode, uint32_t arg, uint32_t response_type, struct sdhc_command *cmd)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->opcode = opcode;
	cmd->arg = arg;
	cmd->response_type = response_type;
	cmd->retries = 2;
	cmd->timeout_ms = 200;
	return sdhc_request(sdhc, cmd, NULL);
}

/* sdhc_get_host_props: what the controller can do */
static void demo_get_host_props(void)
{
	struct sdhc_host_props props;
	int ret = sdhc_get_host_props(sdhc, &props);

	printk("[props] sdhc_get_host_props = %s: clock %u..%u Hz, power-up delay %u ms\n", err(ret),
	       props.f_min, props.f_max, props.power_delay);
	printk("[props] 4 bit bus %d, 8 bit bus %d, high speed %d, 3.3 V %d, 3.0 V %d, 1.8 V %d, SDR50 %d, DDR50 %d, SPI mode %d\n",
	       props.host_caps.bus_4_bit_support, props.host_caps.bus_8_bit_support,
	       props.host_caps.high_spd_support, props.host_caps.vol_330_support,
	       props.host_caps.vol_300_support, props.host_caps.vol_180_support,
	       props.host_caps.sdr50_support, props.host_caps.ddr50_support, props.is_spi);
	printk("[props] SDIO card interrupt %d, max current at 3.3 V %u mA\n",
	       props.host_caps.sdio_async_interrupt_support, props.max_current_330);
}

/* sdhc_card_present: the card detect line (cd-gpios) */
static void demo_card_present(void)
{
	int ret = sdhc_card_present(sdhc);

	printk("[present] sdhc_card_present = %d\n", ret);
}

/* sdhc_hw_reset: reset the controller, the card keeps its state */
static void demo_hw_reset(void)
{
	int ret = sdhc_hw_reset(sdhc);

	printk("[reset] sdhc_hw_reset = %s\n", err(ret));
}

/* sdhc_set_io: power, clock, bus width and timing. A card starts in 1 bit mode at 400 kHz or less. */
static void demo_set_io(void)
{
	struct sdhc_io io = {
		.clock = 400000,
		.bus_mode = SDHC_BUSMODE_PUSHPULL,
		.power_mode = SDHC_POWER_ON,
		.bus_width = SDHC_BUS_WIDTH1BIT,
		.timing = SDHC_TIMING_LEGACY,
		.signal_voltage = SD_VOL_3_3_V,
	};
	int ret;

	ret = sdhc_set_io(sdhc, &io);
	printk("[io] power on, 400 kHz, 1 bit, legacy timing    = %s\n", err(ret));
	k_msleep(10);

	io.clock = 25000000;
	ret = sdhc_set_io(sdhc, &io);
	printk("[io] 25 MHz                                   = %s\n", err(ret));

	io.clock = 50000000;
	io.timing = SDHC_TIMING_HS;
	ret = sdhc_set_io(sdhc, &io);
	printk("[io] 50 MHz, high speed timing                = %s\n", err(ret));

	io.clock = 100000000;
	ret = sdhc_set_io(sdhc, &io);
	printk("[io] 100 MHz (above the maximum)               = %s\n", err(ret));

	io.clock = 100000;
	ret = sdhc_set_io(sdhc, &io);
	printk("[io] 100 kHz (below the minimum)               = %s\n", err(ret));

	io.clock = 400000;
	io.timing = SDHC_TIMING_LEGACY;
	io.signal_voltage = SD_VOL_1_8_V;
	ret = sdhc_set_io(sdhc, &io);
	printk("[io] 1.8 V signalling                          = %s\n", err(ret));

	io.signal_voltage = SD_VOL_3_3_V;
	io.bus_width = 3;
	ret = sdhc_set_io(sdhc, &io);
	printk("[io] a bus width of 3                          = %s\n", err(ret));

	io.bus_width = SDHC_BUS_WIDTH1BIT;
	ret = sdhc_set_io(sdhc, &io);
	printk("[io] back to 400 kHz, 1 bit, 3.3 V             = %s\n", err(ret));
}

/* sdhc_request without data: the identification of the card. Every step prints the response. */
static int bring_up_card(void)
{
	struct sdhc_command cmd;
	int ret;

	ret = send(SD_GO_IDLE_STATE, 0, SD_RSP_TYPE_NONE, &cmd);
	printk("[card] CMD0  go idle                       = %s\n", err(ret));

	/* CMD8: 2.7..3.6 V and a check pattern that the card repeats */
	ret = send(SD_SEND_IF_COND, SD_IF_COND_VHS_3V3 | SD_IF_COND_CHECK, SD_RSP_TYPE_R7, &cmd);
	printk("[card] CMD8  interface condition           = %s: R7 %08x\n", err(ret), cmd.response[0]);
	if (ret != 0) {
		return ret;
	}

	/* ACMD41: CMD55 first, then the operating condition, until the card says it is powered up */
	for (int i = 0; i < 100; i++) {
		ret = send(SD_APP_CMD, 0, SD_RSP_TYPE_R1, &cmd);
		if (ret == 0) {
			ret = send(SD_APP_SEND_OP_COND, SD_OCR_HOST_CAP_FLAG | 0x00ff8000, SD_RSP_TYPE_R3, &cmd);
		}
		if (ret != 0 || (cmd.response[0] & SD_OCR_PWR_BUSY_FLAG)) {
			break;
		}
		k_msleep(10);
	}
	block_addressed = (cmd.response[0] & SD_OCR_CARD_CAP_FLAG) != 0;
	printk("[card] ACMD41 operating condition          = %s: OCR %08x, %s\n", err(ret), cmd.response[0],
	       block_addressed ? "block addressed (SDHC/SDXC)" : "byte addressed");
	if (ret != 0) {
		return ret;
	}

	ret = send(SD_ALL_SEND_CID, 0, SD_RSP_TYPE_R2, &cmd);
	printk("[card] CMD2  CID                           = %s: %08x %08x %08x %08x\n", err(ret),
	       cmd.response[0], cmd.response[1], cmd.response[2], cmd.response[3]);

	ret = send(SD_SEND_RELATIVE_ADDR, 0, SD_RSP_TYPE_R6, &cmd);
	rca = cmd.response[0] >> 16;
	printk("[card] CMD3  relative address              = %s: RCA %04x\n", err(ret), rca);

	ret = send(SD_SEND_CSD, rca << 16, SD_RSP_TYPE_R2, &cmd);
	printk("[card] CMD9  CSD                           = %s: %08x %08x %08x %08x\n", err(ret),
	       cmd.response[0], cmd.response[1], cmd.response[2], cmd.response[3]);

	ret = send(SD_SELECT_CARD, rca << 16, SD_RSP_TYPE_R1b, &cmd);
	printk("[card] CMD7  select the card               = %s\n", err(ret));
	return ret;
}

/* switch to 4 data lines: ACMD6 tells the card, sdhc_set_io tells the controller */
static void bus_width_4(void)
{
	struct sdhc_command cmd;
	struct sdhc_io io = {
		.clock = 25000000,
		.bus_mode = SDHC_BUSMODE_PUSHPULL,
		.power_mode = SDHC_POWER_ON,
		.bus_width = SDHC_BUS_WIDTH4BIT,
		.timing = SDHC_TIMING_LEGACY,
		.signal_voltage = SD_VOL_3_3_V,
	};
	int ret = send(SD_APP_CMD, rca << 16, SD_RSP_TYPE_R1, &cmd);

	if (ret == 0) {
		ret = send(SD_APP_SET_BUS_WIDTH, 2, SD_RSP_TYPE_R1, &cmd);	/* 2 = 4 bit bus */
	}
	if (ret == 0) {
		ret = sdhc_set_io(sdhc, &io);
	}
	printk("[card] ACMD6 4 bit bus, 25 MHz            = %s\n", err(ret));
}

/* sdhc_request with data: CMD17 reads one block. The buffer is 64 byte aligned for the DMA. */
static uint8_t blocks[8 * 512] __aligned(64);

static void demo_request_read_one(void)
{
	struct sdhc_command cmd = { .opcode = SD_READ_SINGLE_BLOCK, .response_type = SD_RSP_TYPE_R1,
				    .retries = 2, .timeout_ms = 200 };
	struct sdhc_data data = { .block_addr = 0, .block_size = 512, .blocks = 1, .data = blocks,
				  .timeout_ms = 500 };
	int ret;

	cmd.arg = block_addressed ? 0 : 0 * 512;
	ret = sdhc_request(sdhc, &cmd, &data);
	printk("[read1] CMD17 block 0                      = %s: %u bytes, R1 %08x, signature %02x %02x\n",
	       err(ret), data.bytes_xfered, cmd.response[0], blocks[510], blocks[511]);
}

/* CMD18 reads several blocks; the controller ends the transfer itself, no CMD12 is needed. */
static void demo_request_read_many(void)
{
	struct sdhc_command cmd = { .opcode = SD_READ_MULTIPLE_BLOCK, .response_type = SD_RSP_TYPE_R1,
				    .retries = 2, .timeout_ms = 200 };
	struct sdhc_data data = { .block_addr = 0, .block_size = 512, .blocks = 8, .data = blocks,
				  .timeout_ms = 1000 };
	static uint8_t first[512];
	int ret;

	memcpy(first, blocks, sizeof(first));
	memset(blocks, 0, sizeof(blocks));
	cmd.arg = block_addressed ? 0 : 0;
	ret = sdhc_request(sdhc, &cmd, &data);
	printk("[readN] CMD18 8 blocks from 0               = %s: %u bytes, first block %s\n", err(ret),
	       data.bytes_xfered, memcmp(first, blocks, 512) == 0 ? "same as CMD17" : "DIFFERENT");
}

/* the card state after all this: CMD13 returns the status register */
static void demo_request_status(void)
{
	struct sdhc_command cmd;
	int ret = send(SD_SEND_STATUS, rca << 16, SD_RSP_TYPE_R1, &cmd);

	printk("[status] CMD13 card status                 = %s: R1 %08x, state %u (4 = transfer)\n",
	       err(ret), cmd.response[0], (cmd.response[0] >> 9) & 0xf);
}

/* requests the controller or the card refuses */
static void demo_request_errors(void)
{
	struct sdhc_command cmd = { .opcode = SD_READ_SINGLE_BLOCK, .response_type = SD_RSP_TYPE_R1,
				    .retries = 0, .timeout_ms = 200 };
	struct sdhc_data data = { .block_addr = 0, .block_size = 512, .blocks = 1, .data = NULL,
				  .timeout_ms = 500 };
	int ret;

	ret = sdhc_request(sdhc, &cmd, &data);
	printk("[request] data without a buffer            = %s\n", err(ret));

	data.data = blocks;
	data.blocks = 0;
	ret = sdhc_request(sdhc, &cmd, &data);
	printk("[request] zero blocks                      = %s\n", err(ret));

	/* the card has RCA 0x0001; a command addressed to another RCA is ignored by it, so no response comes */
	ret = send(SD_SEND_STATUS, 0x1234 << 16, SD_RSP_TYPE_R1, &cmd);
	printk("[request] CMD13 to an address nobody has   = %s\n", err(ret));

	/* a block far beyond the end of the card: the card does not send the data */
	cmd.opcode = SD_READ_SINGLE_BLOCK;
	cmd.arg = 0xfffffff0;
	cmd.response_type = SD_RSP_TYPE_R1;
	data.blocks = 1;
	ret = sdhc_request(sdhc, &cmd, &data);
	printk("[request] CMD17 beyond the end of the card  = %s\n", err(ret));
}

/* sdhc_card_busy: 1 while the card holds DAT0 low (during a write or a programming step) */
static void demo_card_busy(void)
{
	int ret = sdhc_card_busy(sdhc);

	printk("[busy] sdhc_card_busy = %d\n", ret);
}

/* sdhc_execute_tuning: only the UHS modes at 1.8 V need it, which this controller does not do */
static void demo_execute_tuning(void)
{
	int ret = sdhc_execute_tuning(sdhc);

	printk("[tuning] sdhc_execute_tuning = %s\n", err(ret));
}

/* sdhc_enable_interrupt: a callback for card insertion and removal (the card detect line) */
static volatile int events;

static void on_card_event(const struct device *dev, int reason, const void *user_data)
{
	events++;
}

static void demo_enable_interrupt(void)
{
	int ret = sdhc_enable_interrupt(sdhc, on_card_event, SDHC_INT_INSERTED | SDHC_INT_REMOVED, NULL);

	printk("[irq] sdhc_enable_interrupt(inserted | removed) = %s\n", err(ret));
	k_msleep(100);
	printk("[irq] events in 100 ms with the card left alone = %d\n", events);
}

/* sdhc_disable_interrupt: switch the events off again */
static void demo_disable_interrupt(void)
{
	int ret = sdhc_disable_interrupt(sdhc, SDHC_INT_INSERTED | SDHC_INT_REMOVED);

	printk("[irq] sdhc_disable_interrupt                   = %s\n", err(ret));
}

int main(void)
{
	if (!device_is_ready(sdhc)) {
		printk("sdhc not ready\n");
		return 0;
	}

	demo_get_host_props();
	demo_card_present();
	demo_hw_reset();
	demo_set_io();
	if (bring_up_card() == 0) {
		bus_width_4();
		demo_request_read_one();
		demo_request_read_many();
		demo_request_status();
		demo_request_errors();
		demo_card_busy();
	}
	demo_execute_tuning();
	demo_enable_interrupt();
	demo_disable_interrupt();

	printk("sdhc_api done\n");
	return 0;
}
