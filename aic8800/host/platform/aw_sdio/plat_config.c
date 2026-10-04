#include "plat_config.h"
#include "hal_gpio.h"
#include "rtos_al.h"
#include "aic_log.h"
#include "hal_cfg.h"
#include "script.h"

/* Default fallback: PE09 = port E(5), pin 9 -> (5-1)*32+9 = 137 */
#define AIC_WIFI_ON_PIN_DEFAULT  137

static int sdc_channel = CONFIG_AIC_SDIO_SDC_ID;
static int wifi_on_pin = AIC_WIFI_ON_PIN_DEFAULT;
static int is_init = 0;

int aic_wifi_reset_power(void)
{
    hal_gpio_set_pull(wifi_on_pin, GPIO_PULL_UP);
    hal_gpio_set_data(wifi_on_pin, 0);
    rtos_msleep(100);
    hal_gpio_set_data(wifi_on_pin, 1);
    rtos_msleep(300);
    AIC_LOG_PRINTF("aic_wifi_reset_power done (pin=%d)\n", wifi_on_pin);
    return 0;
}

void platform_config_init(void)
{
    user_gpio_set_t gpio_set = {0};
    int ret;

    if (is_init == 1)
        return;

    /* Read wifi_sdc_id from sys_config.fex [wifi_para] section */
    ret = hal_cfg_get_keyvalue("wifi_para", "wifi_sdc_id", (int32_t *)&sdc_channel, 1);
    if (ret == 0) {
        if (sdc_channel < 0 || sdc_channel > 2) {
            AIC_LOG_PRINTF("wifi_sdc_id[%d] set err, use default\n", sdc_channel);
            sdc_channel = CONFIG_AIC_SDIO_SDC_ID;
        }
    } else {
        AIC_LOG_PRINTF("read cfg fail wifi_sdc_id, use default %d\n", sdc_channel);
    }

    /* Read wifi_reg_on GPIO from sys_config.fex [wifi_para] section */
    ret = hal_cfg_get_keyvalue("wifi_para", "wifi_reg_on", (int32_t *)&gpio_set,
                               sizeof(user_gpio_set_t) / 4);
    if (ret == 0) {
        wifi_on_pin = (gpio_set.port - 1) * 32 + gpio_set.port_num;
        AIC_LOG_PRINTF("wifi_reg_on pin=%d (port=%d, num=%d)\n",
                       wifi_on_pin, gpio_set.port, gpio_set.port_num);
    } else {
        AIC_LOG_PRINTF("read cfg fail wifi_reg_on, use default pin %d\n", wifi_on_pin);
    }

    /* Initialize the GPIO pin (output, pull-up, low) */
    hal_gpio_set_pull(wifi_on_pin, GPIO_PULL_UP);
    hal_gpio_set_driving_level(wifi_on_pin, GPIO_DRIVING_LEVEL3);
    hal_gpio_set_data(wifi_on_pin, 0);

    is_init = 1;
}

void platform_config_destory(void)
{
    is_init = 0;
}

int platform_get_sdc_index(void)
{
    return sdc_channel;
}

int platform_set_regon_en(int en)
{
    hal_gpio_set_data(wifi_on_pin, en ? 1 : 0);
    return 0;
}
