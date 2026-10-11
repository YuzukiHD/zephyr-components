/*
 * Calls the functions of <zephyr/drivers/pwm.h> on the PWM of the YuzukiNeko. Channel 2 is on PD22
 * and drives the backlight of the display (node backlight_dsi), so the screen brightness follows
 * the calls. The registers of the channel are printed after each call to show what the driver
 * programmed.
 */
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/sys_io.h>

static const struct device *const pwm = DEVICE_DT_GET(DT_NODELABEL(pwm));
/* channel, period and flags of the backlight, from the pwms property of its node */
static const struct pwm_dt_spec bl = PWM_DT_SPEC_GET(DT_NODELABEL(backlight_dsi));

#define CH	2U

#define PWM_BASE	DT_REG_ADDR(DT_NODELABEL(pwm))
#define REG_PER		0x80			/* channel enable bits */
#define REG_PCGR	0x40			/* clock gate bits */
#define REG_PCCR	(0x20 + (CH / 2) * 4)	/* clock source and divider of channel 2 and 3 */
#define REG_PCR		(0x100 + CH * 0x20)	/* prescaler and polarity */
#define REG_PPR		(0x104 + CH * 0x20)	/* period and active count */

static const char *err(int ret)
{
	switch (ret) {
	case 0:		return "0";
	case -EINVAL:	return "-EINVAL";
	case -ENOTSUP:	return "-ENOTSUP";
	case -ERANGE:	return "-ERANGE";
	default:	return "?";
	}
}

/* what the driver wrote: enable bit, divider M, prescaler, polarity, counts of the period */
static void show_regs(void)
{
	uint32_t per = sys_read32(PWM_BASE + REG_PER);
	uint32_t pccr = sys_read32(PWM_BASE + REG_PCCR);
	uint32_t pcr = sys_read32(PWM_BASE + REG_PCR);
	uint32_t ppr = sys_read32(PWM_BASE + REG_PPR);

	printk("         enabled %u, clock source %u, divider 1/%u, prescaler %u, %s, period %u counts, active %u counts\n",
	       !!(per & BIT(CH)), (pccr >> 7) & 3, 1U << (pccr & 15), (pcr & 0xff) + 1,
	       (pcr & BIT(8)) ? "inverted" : "normal", (ppr >> 16) + 1, ppr & 0xffff);
}

/* pwm_get_cycles_per_sec: the unit of the cycles of pwm_set_cycles */
static void demo_cycles_per_sec(void)
{
	uint64_t hz;
	int ret;

	ret = pwm_get_cycles_per_sec(pwm, CH, &hz);
	printk("[clock] pwm_get_cycles_per_sec(channel %u) = %s, %u Hz\n", CH, err(ret), (uint32_t)hz);

	ret = pwm_get_cycles_per_sec(pwm, 4, &hz);
	printk("[clock] pwm_get_cycles_per_sec(channel 4) = %s\n", err(ret));
}

/* pwm_set_cycles: period and pulse in cycles of the 24 MHz reference clock. 24000 cycles are
 * 1 ms, a frequency of 1 kHz. PWM_POLARITY_INVERTED makes the pulse the low part of the period,
 * which is how the backlight of the board is wired (low = on).
 */
static void demo_set_cycles(void)
{
	static const uint32_t pulse[] = { 2400, 12000, 21600 };
	int ret;

	for (int i = 0; i < ARRAY_SIZE(pulse); i++) {
		ret = pwm_set_cycles(pwm, CH, 24000, pulse[i], PWM_POLARITY_INVERTED);
		printk("[cycles] 1 kHz, pulse %5u of 24000 (%2u%%), inverted = %s\n", pulse[i],
		       pulse[i] * 100 / 24000, err(ret));
		show_regs();
		k_msleep(1000);
	}

	ret = pwm_set_cycles(pwm, CH, 24000, 12000, PWM_POLARITY_NORMAL);
	printk("[cycles] 1 kHz, 50%%, normal polarity            = %s\n", err(ret));
	show_regs();
	k_msleep(500);
}

/* pwm_set: the same in nanoseconds; the API converts to cycles of the 24 MHz reference, and a period
 * shorter than one cycle (41.7 ns) becomes 0 cycles and is refused.
 */
static void demo_set(void)
{
	int ret;

	ret = pwm_set(pwm, CH, PWM_USEC(100), PWM_USEC(25), PWM_POLARITY_NORMAL);
	printk("[set] 10 kHz, 25%%                                = %s\n", err(ret));
	show_regs();

	ret = pwm_set(pwm, CH, PWM_MSEC(20), PWM_USEC(1500), PWM_POLARITY_NORMAL);
	printk("[set] 50 Hz, 1.5 ms (a servo in the middle)      = %s\n", err(ret));
	show_regs();

	ret = pwm_set(pwm, CH, PWM_SEC(2), PWM_SEC(1), PWM_POLARITY_NORMAL);
	printk("[set] 0.5 Hz, 50%%                                = %s\n", err(ret));
	show_regs();

	ret = pwm_set(pwm, CH, 200, 100, PWM_POLARITY_NORMAL);
	printk("[set] 200 ns period (5 MHz), 50%%                 = %s\n", err(ret));
	show_regs();

	ret = pwm_set(pwm, CH, 10, 5, PWM_POLARITY_NORMAL);
	printk("[set] 10 ns period (less than one 24 MHz cycle) = %s\n", err(ret));
	show_regs();
}

/* pwm_set_pulse_dt: the channel, period and polarity come from the pwms property of the devicetree
 * node, only the pulse is given
 */
static void demo_set_pulse_dt(void)
{
	int ret;

	printk("[dt] backlight_dsi: channel %u, period %u ns, flags 0x%x\n", bl.channel, bl.period,
	       bl.flags);

	ret = pwm_set_pulse_dt(&bl, bl.period / 4);
	printk("[dt] pwm_set_pulse_dt(period / 4)                = %s\n", err(ret));
	show_regs();
	k_msleep(1000);
}

/* pwm_set_dt: period and pulse, with the channel and the polarity from the devicetree node */
static void demo_set_dt(void)
{
	int ret = pwm_set_dt(&bl, bl.period, bl.period * 3 / 4);

	printk("[dt] pwm_set_dt(period, 3/4 of it)               = %s\n", err(ret));
	show_regs();
	k_msleep(1000);
}

/* a pulse of 0 stops the output and gates the channel's clock */
static void demo_stop(void)
{
	int ret;

	ret = pwm_set_pulse_dt(&bl, 0);
	printk("[stop] pulse 0                                  = %s\n", err(ret));
	show_regs();
	k_msleep(1000);

	/* back to the default level of the backlight node: 96 of 255, inverted */
	ret = pwm_set_pulse_dt(&bl, bl.period * 96 / 255);
	printk("[stop] pulse 96/255 of the period (the default) = %s\n", err(ret));
	show_regs();
}

/* what the driver refuses */
static void demo_errors(void)
{
	int ret;

	ret = pwm_set_cycles(pwm, 4, 24000, 12000, 0);
	printk("[error] channel 4                       = %s\n", err(ret));

	ret = pwm_set_cycles(pwm, CH, 0, 0, 0);
	printk("[error] period 0                        = %s\n", err(ret));

	ret = pwm_set_cycles(pwm, CH, 24000, 24001, 0);
	printk("[error] pulse longer than the period    = %s\n", err(ret));

	ret = pwm_set_cycles(pwm, CH, 24000, 12000, BIT(1));
	printk("[error] a flag other than the polarity  = %s\n", err(ret));

	ret = pwm_set_cycles(pwm, CH, 1, 1, 0);
	printk("[error] period of 1 cycle (41.7 ns)     = %s\n", err(ret));
}

int main(void)
{
	if (!device_is_ready(pwm)) {
		printk("pwm not ready\n");
		return 0;
	}

	demo_cycles_per_sec();
	demo_set_cycles();
	demo_set();
	demo_set_pulse_dt();
	demo_set_dt();
	demo_stop();
	demo_errors();

	/* leave the backlight on */
	pwm_set_pulse_dt(&bl, bl.period * 96 / 255);
	printk("pwm_api done\n");
	return 0;
}
