#ifndef __PLAT_CONFIG_H__
#define __PLAT_CONFIG_H__

#include <zephyr/device.h>

#ifndef CONFIG_AIC_SDIO_SDC_ID
#define CONFIG_AIC_SDIO_SDC_ID  2
#endif

void platform_config_init(void);
int aic_wifi_reset_power(void);
void platform_config_destory(void);
int platform_get_sdc_index(void);
int platform_set_regon_en(int en);

/* SD host controller the chip is on (parent of the chip node in the devicetree) */
const struct device *platform_get_sdhc(void);
/* level of the wakeup line, -ENOTSUP when the node has none */
int platform_get_wakeup_level(void);

#endif
