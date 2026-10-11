/*
 * Calls the functions of <zephyr/drivers/i2c.h> on I2C1 of the YuzukiNeko,
 * where the touch controller GT967 of the carrier board sits at 0x5d.
 */
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

static const struct device *const bus = DEVICE_DT_GET(DT_NODELABEL(i2c1));
/* the touch controller node of yuzukineko.dts: address and reset/interrupt pins */
#define TOUCH DT_NODELABEL(touch)
static const struct gpio_dt_spec rst = GPIO_DT_SPEC_GET(TOUCH, reset_gpios);
static const struct gpio_dt_spec irq = GPIO_DT_SPEC_GET(TOUCH, irq_gpios);
#define ADDR DT_REG_ADDR(TOUCH)

/* GT967 registers (16 bit addresses, big endian) */
#define REG_PRODUCT_ID	0x8140	/* "967" in ASCII, 4 bytes */
#define REG_CONFIG_VER	0x8047	/* 1 byte */

static const char *err(int ret)
{
	switch (ret) {
	case 0:		return "0";
	case -EIO:	return "-EIO";
	case -ENOTSUP:	return "-ENOTSUP";
	case -EINVAL:	return "-EINVAL";
	case -EAGAIN:	return "-EAGAIN";
	case -ENOSYS:	return "-ENOSYS";
	case -ETIMEDOUT: return "-ETIMEDOUT";
	default:	return "?";
	}
}

/* The controller takes its address from the level of INT at the moment RST is released:
 * INT low gives 0x5d, INT high gives 0x14. A real touch driver does this once at start.
 */
static void touch_reset(void)
{
	gpio_pin_configure_dt(&rst, GPIO_OUTPUT_ACTIVE);	/* RST is active low in the DT: held in reset */
	k_msleep(12);
	gpio_pin_configure_dt(&irq, GPIO_OUTPUT_INACTIVE);	/* INT low selects 0x5d */
	k_msleep(2);
	gpio_pin_set_dt(&rst, 0);				/* released */
	k_msleep(6);
	gpio_pin_configure_dt(&irq, GPIO_INPUT);
	k_msleep(60);
	printk("[reset] released RST with INT low, the controller should answer at 0x%02x\n", ADDR);
}

/* i2c_configure: the speed can be changed at run time */
static void demo_configure(void)
{
	int ret;

	ret = i2c_configure(bus, I2C_SPEED_SET(I2C_SPEED_STANDARD) | I2C_MODE_CONTROLLER);
	printk("[configure] standard, 100 kHz           = %s\n", err(ret));

	ret = i2c_configure(bus, I2C_SPEED_SET(I2C_SPEED_FAST) | I2C_MODE_CONTROLLER);
	printk("[configure] fast, 400 kHz               = %s\n", err(ret));

	ret = i2c_configure(bus, I2C_SPEED_SET(I2C_SPEED_FAST_PLUS) | I2C_MODE_CONTROLLER);
	printk("[configure] fast plus, 1 MHz            = %s\n", err(ret));

	ret = i2c_configure(bus, I2C_SPEED_SET(I2C_SPEED_STANDARD) | I2C_MODE_CONTROLLER |
				 I2C_ADDR_10_BITS);
	printk("[configure] 10 bit addresses            = %s\n", err(ret));

	ret = i2c_configure(bus, I2C_SPEED_SET(I2C_SPEED_STANDARD));
	printk("[configure] without I2C_MODE_CONTROLLER = %s\n", err(ret));

	ret = i2c_configure(bus, I2C_SPEED_SET(I2C_SPEED_FAST) | I2C_MODE_CONTROLLER);
	printk("[configure] back to fast                = %s\n", err(ret));
}

/* i2c_get_config is not implemented by this driver */
static void demo_get_config(void)
{
	uint32_t cfg;
	int ret = i2c_get_config(bus, &cfg);

	printk("[configure] i2c_get_config              = %s\n", err(ret));
}

/* A read of one byte finds out whether a device acknowledges its address. */
static void demo_scan(void)
{
	uint8_t b;
	int found = 0;

	for (uint16_t a = 0x08; a <= 0x77; a++) {
		if (i2c_read(bus, &b, 1, a) == 0) {
			printk("[scan] device at 0x%02x\n", a);
			found++;
		}
	}
	printk("[scan] %d device(s) found\n", found);
}

/* i2c_write_read: write the 16 bit register address, restart, read the value */
static void demo_write_read(void)
{
	uint8_t reg[2] = { REG_PRODUCT_ID >> 8, REG_PRODUCT_ID & 0xff };
	uint8_t id[4] = { 0 };
	int ret;

	ret = i2c_write_read(bus, ADDR, reg, sizeof(reg), id, sizeof(id));
	printk("[write_read] product id register 0x%04x = %s: %02x %02x %02x %02x \"%c%c%c\"\n",
	       REG_PRODUCT_ID, err(ret), id[0], id[1], id[2], id[3], id[0], id[1], id[2]);
}

/* i2c_write: one complete transfer with a stop at the end. Here it sets the register address. */
static void demo_write(void)
{
	uint8_t reg[2] = { REG_CONFIG_VER >> 8, REG_CONFIG_VER & 0xff };
	int ret = i2c_write(bus, reg, sizeof(reg), ADDR);

	printk("[write+read] i2c_write(register address) = %s\n", err(ret));
}

/* i2c_read: reads the register that the write above selected */
static void demo_read(void)
{
	uint8_t val = 0;
	int ret = i2c_read(bus, &val, 1, ADDR);

	printk("[write+read] i2c_read(1 byte)            = %s: 0x%02x\n", err(ret), val);
}

/* i2c_burst_read: writes a one byte start address, then reads. The GT967 registers have 16 bit
 * addresses, so this only shows that the call goes through; use i2c_write_read for this chip.
 */
static void demo_burst(void)
{
	uint8_t buf[4];
	int ret;

	ret = i2c_burst_read(bus, ADDR, 0x81, buf, sizeof(buf));
	printk("[burst] i2c_burst_read(0x81, 4)       = %s\n", err(ret));
}

/* i2c_transfer: the messages by hand, flags decide start, restart and stop */
static void demo_transfer(void)
{
	uint8_t reg[2] = { REG_PRODUCT_ID >> 8, REG_PRODUCT_ID & 0xff };
	uint8_t id[4] = { 0 };
	struct i2c_msg msgs[2];
	int ret;

	msgs[0].buf = reg;
	msgs[0].len = sizeof(reg);
	msgs[0].flags = I2C_MSG_WRITE;				/* no stop: the next message restarts */
	msgs[1].buf = id;
	msgs[1].len = sizeof(id);
	msgs[1].flags = I2C_MSG_READ | I2C_MSG_RESTART | I2C_MSG_STOP;

	ret = i2c_transfer(bus, msgs, 2, ADDR);
	printk("[transfer] write + restart + read       = %s: \"%c%c%c\"\n", err(ret), id[0], id[1], id[2]);

	/* a 10 bit address is refused */
	msgs[1].flags |= I2C_MSG_ADDR_10_BITS;
	ret = i2c_transfer(bus, msgs, 2, ADDR);
	printk("[transfer] 10 bit address               = %s\n", err(ret));

	/* an address above 7 bits */
	msgs[1].flags &= ~I2C_MSG_ADDR_10_BITS;
	ret = i2c_transfer(bus, msgs, 2, 0x100);
	printk("[transfer] address 0x100                = %s\n", err(ret));
}

/* a device that is not there does not acknowledge */
static void demo_nack(void)
{
	uint8_t b;
	int ret;

	ret = i2c_read(bus, &b, 1, 0x50);
	printk("[nack] read from 0x50                  = %s\n", err(ret));
	ret = i2c_write(bus, &b, 1, 0x50);
	printk("[nack] write to 0x50                   = %s\n", err(ret));
}

int main(void)
{
	if (!device_is_ready(bus) || !gpio_is_ready_dt(&rst) || !gpio_is_ready_dt(&irq)) {
		printk("i2c or gpio not ready\n");
		return 0;
	}

	touch_reset();
	demo_configure();
	demo_get_config();
	demo_scan();
	demo_write_read();
	demo_write();
	demo_read();
	demo_burst();
	demo_transfer();
	demo_nack();

	printk("i2c_api done\n");
	return 0;
}
