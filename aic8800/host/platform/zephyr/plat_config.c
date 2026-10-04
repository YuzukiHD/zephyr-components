/*
 * Platform side of the AIC8800: the power pin and the host controller the
 * chip is wired to, both taken from the devicetree node "aicsemi,aic8800".
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "plat_config.h"
#include "rtos_al.h"
#include "aic_log.h"

#define AIC_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(aicsemi_aic8800)

BUILD_ASSERT(DT_NODE_EXISTS(AIC_NODE),
	     "CONFIG_AIC8800 needs an enabled node compatible with \"aicsemi,aic8800\" "
	     "under the SD host controller of the chip");

static const struct gpio_dt_spec reg_on = GPIO_DT_SPEC_GET(AIC_NODE, reg_on_gpios);
#if DT_NODE_HAS_PROP(AIC_NODE, wakeup_gpios)
static const struct gpio_dt_spec wakeup = GPIO_DT_SPEC_GET(AIC_NODE, wakeup_gpios);
#endif

static int is_init;

const struct device *platform_get_sdhc(void)
{
	return DEVICE_DT_GET(DT_PARENT(AIC_NODE));
}

int platform_get_wakeup_level(void)
{
#if DT_NODE_HAS_PROP(AIC_NODE, wakeup_gpios)
	return gpio_pin_get_dt(&wakeup);
#else
	return -ENOTSUP;
#endif
}

/* power off, then on; the chip needs time to boot before it answers on the bus */
int aic_wifi_reset_power(void)
{
	platform_config_init();
	gpio_pin_set_dt(&reg_on, 0);
	rtos_msleep(100);
	gpio_pin_set_dt(&reg_on, 1);
	rtos_msleep(300);
	AIC_LOG_PRINTF("aic_wifi_reset_power done\n");

	return 0;
}

void platform_config_init(void)
{
	if (is_init == 1) {
		return;
	}
	if (!gpio_is_ready_dt(&reg_on)) {
		AIC_LOG_PRINTF("reg_on gpio not ready\n");
		return;
	}
	/* the chip is off until the power sequence runs */
	gpio_pin_configure_dt(&reg_on, GPIO_OUTPUT_INACTIVE);
#if DT_NODE_HAS_PROP(AIC_NODE, wakeup_gpios)
	if (gpio_is_ready_dt(&wakeup)) {
		gpio_pin_configure_dt(&wakeup, GPIO_INPUT);
	}
#endif
	is_init = 1;
}

void platform_config_destory(void)
{
	if (is_init) {
		gpio_pin_set_dt(&reg_on, 0);
	}
	is_init = 0;
}

int platform_get_sdc_index(void)
{
	return CONFIG_AIC_SDIO_SDC_ID;
}

int platform_set_regon_en(int en)
{
	platform_config_init();

	return gpio_pin_set_dt(&reg_on, en ? 1 : 0);
}
