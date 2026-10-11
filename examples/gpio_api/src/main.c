/*
 * Calls the functions of <zephyr/drivers/gpio.h> that the sunxi GPIO driver
 * implements, one function per call, and prints what each call returns.
 */
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* PD5, the LED of the board (alias led0 in yuzukineko.dts) */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
/* PB2 and PB3, from the zephyr,user node of app.overlay */
static const struct gpio_dt_spec out = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), out_gpios);
static const struct gpio_dt_spec in = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), in_gpios);

static const struct device *const gpiob = DEVICE_DT_GET(DT_NODELABEL(gpiob));

static const char *err(int ret)
{
	switch (ret) {
	case 0:		return "0";
	case -ENOTSUP:	return "-ENOTSUP";
	case -EINVAL:	return "-EINVAL";
	case -EIO:	return "-EIO";
	default:	return "?";
	}
}

/* gpio_pin_configure_dt: direction, initial level and pull resistor */
static void demo_pin_configure_dt(void)
{
	int ret;

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	printk("[configure] PD5 output, low                 = %s\n", err(ret));

	ret = gpio_pin_configure_dt(&in, GPIO_INPUT | GPIO_PULL_UP);
	printk("[configure] PB3 input with pull up         = %s\n", err(ret));

	ret = gpio_pin_configure_dt(&in, GPIO_INPUT | GPIO_PULL_DOWN);
	printk("[configure] PB3 input with pull down       = %s\n", err(ret));

	ret = gpio_pin_configure_dt(&out, GPIO_OUTPUT | GPIO_OPEN_DRAIN);
	printk("[configure] PB2 open drain output          = %s\n", err(ret));

	ret = gpio_pin_configure_dt(&out, GPIO_OUTPUT | GPIO_OPEN_SOURCE);
	printk("[configure] PB2 open source output         = %s\n", err(ret));
}

/* gpio_pin_set_dt: the logical level of an output */
static void demo_pin_set_dt(void)
{
	int ret;

	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	ret = gpio_pin_set_dt(&led, 1);
	printk("[set] gpio_pin_set_dt(PD5, 1)              = %s, register bit %d\n", err(ret),
	       gpio_pin_get_raw(led.port, led.pin));

	ret = gpio_pin_set_dt(&led, 0);
	printk("[set] gpio_pin_set_dt(PD5, 0)              = %s, register bit %d\n", err(ret),
	       gpio_pin_get_raw(led.port, led.pin));
}

/* gpio_pin_toggle_dt: invert an output; six calls make the LED blink three times */
static void demo_pin_toggle_dt(void)
{
	int ret = gpio_pin_toggle_dt(&led);

	printk("[toggle] gpio_pin_toggle_dt(PD5)           = %s, register bit %d\n", err(ret),
	       gpio_pin_get_raw(led.port, led.pin));

	for (int i = 0; i < 5; i++) {
		k_msleep(100);
		gpio_pin_toggle_dt(&led);
	}
	printk("[toggle] blinked the LED 3 times\n");
}

/* gpio_pin_get_dt: the logical level of a pin. For an output pin it is the level that was written. */
static void demo_pin_get_dt(void)
{
	int level;

	gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	level = gpio_pin_get_dt(&led);
	printk("[get] PD5 written active, gpio_pin_get_dt   = %d\n", level);

	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	level = gpio_pin_get_dt(&led);
	printk("[get] PD5 written inactive, gpio_pin_get_dt = %d\n", level);
}

/* gpio_pin_get_raw: the level of the pin, not looking at GPIO_ACTIVE_LOW. PB3 is not connected,
 * so the pull resistor decides.
 */
static void demo_pin_get_raw(void)
{
	gpio_pin_configure_dt(&in, GPIO_INPUT | GPIO_PULL_UP);
	k_msleep(1);
	printk("[get_raw] PB3 with pull up                  = %d\n", gpio_pin_get_raw(in.port, in.pin));

	gpio_pin_configure_dt(&in, GPIO_INPUT | GPIO_PULL_DOWN);
	k_msleep(1);
	printk("[get_raw] PB3 with pull down                = %d\n", gpio_pin_get_raw(in.port, in.pin));
}

/* gpio_port_get_raw: all 32 pins of the bank at once */
static void demo_port_get_raw(void)
{
	gpio_port_value_t v;
	int ret;

	gpio_pin_configure_dt(&in, GPIO_INPUT | GPIO_PULL_UP);
	k_msleep(1);
	ret = gpio_port_get_raw(gpiob, &v);
	printk("[port_get] gpio_port_get_raw(PB)            = %s, PB2 %d, PB3 %d\n", err(ret),
	       !!(v & BIT(out.pin)), !!(v & BIT(in.pin)));
}

/* gpio_port_set_bits_raw: drive the pins of the mask high */
static void demo_port_set_bits_raw(void)
{
	gpio_port_value_t v;
	int ret;

	gpio_pin_configure_dt(&out, GPIO_OUTPUT_INACTIVE);
	ret = gpio_port_set_bits_raw(gpiob, BIT(out.pin));
	gpio_port_get_raw(gpiob, &v);
	printk("[set_bits] gpio_port_set_bits_raw(PB2)       = %s, PB2 %d\n", err(ret), !!(v & BIT(out.pin)));
}

/* gpio_port_clear_bits_raw: drive the pins of the mask low */
static void demo_port_clear_bits_raw(void)
{
	gpio_port_value_t v;
	int ret = gpio_port_clear_bits_raw(gpiob, BIT(out.pin));

	gpio_port_get_raw(gpiob, &v);
	printk("[clear_bits] gpio_port_clear_bits_raw(PB2)   = %s, PB2 %d\n", err(ret), !!(v & BIT(out.pin)));
}

/* gpio_port_toggle_bits: invert the pins of the mask */
static void demo_port_toggle_bits(void)
{
	gpio_port_value_t v;
	int ret = gpio_port_toggle_bits(gpiob, BIT(out.pin));

	gpio_port_get_raw(gpiob, &v);
	printk("[toggle_bits] gpio_port_toggle_bits(PB2)     = %s, PB2 %d\n", err(ret), !!(v & BIT(out.pin)));
}

/* gpio_port_set_masked_raw: set the pins of the mask to the matching bits of value, leave the others */
static void demo_port_set_masked_raw(void)
{
	gpio_port_value_t v;
	int ret = gpio_port_set_masked_raw(gpiob, BIT(out.pin), 0);

	gpio_port_get_raw(gpiob, &v);
	printk("[set_masked] gpio_port_set_masked_raw(PB2, 0) = %s, PB2 %d\n", err(ret),
	       !!(v & BIT(out.pin)));
}

/* interrupts. The edge is made by switching the pull resistor of the unconnected pin PB3. */
static struct gpio_callback cb;
static volatile int fired;
static volatile uint32_t last_pins;

static void on_edge(const struct device *port, struct gpio_callback *callback, uint32_t pins)
{
	fired++;
	last_pins = pins;
}

/* gpio_init_callback: bind a handler and the mask of the pins it wants */
static void demo_init_callback(void)
{
	gpio_init_callback(&cb, on_edge, BIT(in.pin));
	printk("[init_callback] handler for pin mask 0x%x\n", (unsigned int)cb.pin_mask);
}

/* gpio_add_callback: register it with the bank */
static void demo_add_callback(void)
{
	int ret = gpio_add_callback(in.port, &cb);

	printk("[add_callback] gpio_add_callback              = %s\n", err(ret));
}

/* gpio_pin_interrupt_configure_dt: arm the pin. The pull resistor is part of the pin configuration,
 * so after every gpio_pin_configure the interrupt is armed again.
 */
static void demo_pin_interrupt_configure_dt(void)
{
	int ret;

	gpio_pin_configure_dt(&in, GPIO_INPUT | GPIO_PULL_DOWN);
	k_msleep(1);

	ret = gpio_pin_interrupt_configure_dt(&in, GPIO_INT_EDGE_RISING);
	printk("[interrupt] arm EDGE_RISING                 = %s\n", err(ret));

	fired = 0;
	gpio_pin_configure_dt(&in, GPIO_INPUT | GPIO_PULL_UP);
	gpio_pin_interrupt_configure_dt(&in, GPIO_INT_EDGE_RISING);
	k_msleep(20);
	printk("[interrupt] pull up after arming rising     : %d callback(s), pins 0x%x\n", fired, last_pins);

	fired = 0;
	gpio_pin_configure_dt(&in, GPIO_INPUT | GPIO_PULL_DOWN);
	gpio_pin_interrupt_configure_dt(&in, GPIO_INT_EDGE_RISING);
	k_msleep(20);
	printk("[interrupt] pull down after arming rising   : %d callback(s)\n", fired);

	ret = gpio_pin_interrupt_configure_dt(&in, GPIO_INT_DISABLE);
	printk("[interrupt] GPIO_INT_DISABLE                = %s\n", err(ret));
}

/* gpio_get_pending_int: the interrupt status register of the bank, one bit per pin */
static void demo_get_pending_int(void)
{
	printk("[pending] gpio_get_pending_int(PB)           = 0x%x\n", gpio_get_pending_int(gpiob));
}

/* gpio_remove_callback: take the handler out again */
static void demo_remove_callback(void)
{
	int ret = gpio_remove_callback(in.port, &cb);

	printk("[remove_callback] gpio_remove_callback        = %s\n", err(ret));
}

int main(void)
{
	if (!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&out) || !gpio_is_ready_dt(&in)) {
		printk("gpio not ready\n");
		return 0;
	}

	demo_pin_configure_dt();
	demo_pin_set_dt();
	demo_pin_toggle_dt();
	demo_pin_get_dt();
	demo_pin_get_raw();
	demo_port_get_raw();
	demo_port_set_bits_raw();
	demo_port_clear_bits_raw();
	demo_port_toggle_bits();
	demo_port_set_masked_raw();
	demo_init_callback();
	demo_add_callback();
	demo_pin_interrupt_configure_dt();
	demo_get_pending_int();
	demo_remove_callback();

	printk("gpio_api done\n");
	return 0;
}
