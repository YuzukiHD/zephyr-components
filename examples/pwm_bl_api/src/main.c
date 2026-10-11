/*
 * Calls the functions of <zephyr/drivers/pwm.h> on the PWM_BL backlight block. The block is
 * not a PWM that makes square waves of any frequency: the duty ratio (pulse / period) is turned into a
 * load current level, 0..200, of the boost converter. The registers of the block are printed after
 * each call to show the level.
 */
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/sys_io.h>

static const struct device *const pwm_bl = DEVICE_DT_GET(DT_NODELABEL(pwm_bl));
static const struct pwm_dt_spec bl = PWM_DT_SPEC_GET(DT_PATH(zephyr_user));

#define BASE		DT_REG_ADDR(DT_NODELABEL(pwm_bl))
#define PORT_STRIDE	0x200
#define REG_CTRL	0x0000		/* bit 0 enable, bit 1 clock gating, bit 2 digital + analog */
#define REG_PRD		0x0008		/* period of the digital dimming */
#define REG_ANA1	0x0054		/* bits 23:16 load current level, bits 2:0 maximum brightness */

static const char *err(int ret)
{
	switch (ret) {
	case 0:		return "0";
	case -EINVAL:	return "-EINVAL";
	default:	return "?";
	}
}

/* what the driver wrote: enable bits of the two ports, the load current level of port 0 */
static void show_regs(void)
{
	uint32_t ctrl0 = sys_read32(BASE + REG_CTRL);
	uint32_t ctrl1 = sys_read32(BASE + PORT_STRIDE + REG_CTRL);
	uint32_t ana1 = sys_read32(BASE + REG_ANA1);

	printk("         port 0 enabled %u, port 1 enabled %u, load current level %u (maximum brightness code %u)\n",
	       ctrl0 & 1, ctrl1 & 1, (ana1 >> 16) & 0xff, ana1 & 7);
}

/* pwm_get_cycles_per_sec: 1 GHz, so a cycle is a nanosecond and the ratios are exact */
static void demo_cycles_per_sec(void)
{
	uint64_t hz;
	int ret;

	ret = pwm_get_cycles_per_sec(pwm_bl, 0, &hz);
	printk("[clock] pwm_get_cycles_per_sec(channel 0)   = %s, %u Hz\n", err(ret), (uint32_t)hz);

	ret = pwm_get_cycles_per_sec(pwm_bl, 1, &hz);
	printk("[clock] pwm_get_cycles_per_sec(channel 1)   = %s\n", err(ret));
}

/* pwm_set_cycles: the level is pulse * 256 / period; 200 is the maximum, a level of 0 switches the
 * block off. The flags are ignored, there is no polarity.
 */
static void demo_set_cycles(void)
{
	static const uint32_t pulse[] = { 20000, 10000, 5000, 1000, 100 };
	int ret;

	for (int i = 0; i < ARRAY_SIZE(pulse); i++) {
		ret = pwm_set_cycles(pwm_bl, 0, 20000, pulse[i], 0);
		printk("[cycles] pulse %5u of 20000 (%3u%%)         = %s\n", pulse[i], pulse[i] * 100 / 20000,
		       err(ret));
		show_regs();
	}
}

/* pwm_set: the same in nanoseconds; the period does not change the output, only the ratio does */
static void demo_set(void)
{
	int ret;

	ret = pwm_set(pwm_bl, 0, PWM_USEC(20), PWM_USEC(10), PWM_POLARITY_NORMAL);
	printk("[set] 20 us period, 10 us pulse                 = %s\n", err(ret));
	show_regs();

	ret = pwm_set(pwm_bl, 0, PWM_MSEC(1), PWM_USEC(500), PWM_POLARITY_INVERTED);
	printk("[set] 1 ms period, 500 us pulse, inverted       = %s\n", err(ret));
	show_regs();
}

/* pwm_set_dt: channel, period and flags from the pwms property of the zephyr,user node */
static void demo_set_dt(void)
{
	int ret = pwm_set_dt(&bl, bl.period, bl.period / 4);

	printk("[dt] channel %u, period %u ns, flags 0x%x\n", bl.channel, bl.period, bl.flags);
	printk("[dt] pwm_set_dt(period, 1/4 of it)              = %s\n", err(ret));
	show_regs();
}

/* pwm_set_pulse_dt: only the pulse; 0 switches the backlight off */
static void demo_set_pulse_dt(void)
{
	int ret = pwm_set_pulse_dt(&bl, bl.period * 3 / 4);

	printk("[dt] pwm_set_pulse_dt(3/4 of the period)        = %s\n", err(ret));
	show_regs();

	ret = pwm_set_pulse_dt(&bl, 0);
	printk("[dt] pwm_set_pulse_dt(0)                        = %s\n", err(ret));
	show_regs();
}

/* the arguments the driver refuses */
static void demo_errors(void)
{
	int ret;

	ret = pwm_set_cycles(pwm_bl, 0, 0, 0, 0);
	printk("[error] period 0                                = %s\n", err(ret));

	ret = pwm_set_cycles(pwm_bl, 0, 20000, 20001, 0);
	printk("[error] pulse longer than the period            = %s\n", err(ret));

	ret = pwm_set_cycles(pwm_bl, 5, 20000, 10000, 0);
	printk("[error] channel 5 (only channel 0 exists)       = %s\n", err(ret));
	show_regs();
}

int main(void)
{
	if (!device_is_ready(pwm_bl)) {
		printk("pwm_bl not ready\n");
		return 0;
	}

	demo_cycles_per_sec();
	demo_set_cycles();
	demo_set();
	demo_set_dt();
	demo_set_pulse_dt();
	demo_errors();

	pwm_set_pulse_dt(&bl, 0);
	printk("pwm_bl_api done\n");
	return 0;
}
