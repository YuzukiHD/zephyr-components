#ifndef __WIFI_DRIVER_CFG_H__
#define __WIFI_DRIVER_CFG_H__

/*!
 * @file     wifi_driver_cfg.h
 * @brief    APIs&data structure for WiFi driver feature config.
 */

/*!
 * \addtogroup network
 * @{
 *     @defgroup WiFiDriver
 *     @brief Declare WiFi driver cfg & data structure type.
 *     @{
 */


typedef struct {
	int rx_reserved_buf_size;     /*!< Set the data buffer length in the RX direction */
	int mem_statistics_ctrl;      /*!< Switch memory usage monitoring mechanism in the driver */

} wifi_drv_conf;

/**
 * @brief Configure WiFi driver.
 * @param conf[in] Configuration information.
 * @return success or not
 * @retval  0 : success
 * @retval -1 : fail
 * @note
 * \li This interface must be called before the WiFi driver is loaded.
 */
int wifi_drv_feature_config(wifi_drv_conf* conf);

/*!
 *    @} end of defgroup WiFiDriver
 * @} end of addtogroup network
 */


#endif
