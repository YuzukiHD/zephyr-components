/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
 *
 * All Rights Reserved
 */

#include "iw.h"
//#include <log.h>
#include <errno.h>

char* prev_dev_name = "DEV_P2P";

static int aic_iw_set_p2p_enable(struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{

	int ret = 0;
	printf("%s in, extra %s\n", __func__, extra);
	if (*extra == '0') {
		if (wlan_stop_p2p()) {
			AIC_LOG_PRINTF("Failed to stop p2p via iw cmd\n");
			ret = -EFAULT;
			goto exit;
		}
	} else if (*extra == '3') {
		if(wlan_stop_ap()) {
			AIC_LOG_PRINTF("No need to close ap before open it\n");
		}
		ret = user_p2p_start(1);
		if (ret) {
			AIC_LOG_PRINTF("Failed to start p2p, ret=%d\n", ret);
			return -1;
		} else
			printf("p2p is already in running\n");

	} else
		printf("Invalid extra info\n");
exit:
	return ret;
}

static int aic_p2p_setDN(struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	char dev_name[32];
	memset(dev_name, '\0', sizeof(dev_name));
	strncpy(dev_name, extra, wrqu->data.length - 1);
	dev_name[wrqu->data.length]='\0';
	printf("%s dev_name: %s %d\n", __func__, dev_name, wrqu->data.length);
	if (strcmp(prev_dev_name, dev_name)) {
		if (user_p2p_setDN(dev_name)) {
			ret = -EFAULT;
			goto exit;
		}
		prev_dev_name = dev_name;
	} else {
		printf("%s No need to reset device name as it's identical to previous one\n", __func__);
	}

exit:
	return ret;

}

static int aic_iw_set_wfd_type(struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	int ret = 1;
	int wfd_device_type = 1;
	printf("%s in, extra %s\n", __func__, extra);
	if (extra[0] == '0')	/*	Set to Miracast source device. */
		wfd_device_type = 0;
	else					/*	Set to Miracast sink device. */
		wfd_device_type = 1;
	ret = user_set_wfd_type(wfd_device_type);

exit:
	return ret;
}

static int aic_iw_set_wfd_enable(struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	int ret = 1;
	printf("%s in, extra %s\n", __func__, extra);
	if (*extra == '0')
		ret = user_set_wfd_enable(0);
	else if (*extra == '1')
		ret = user_set_wfd_enable(1);
exit:
	return ret;
}

static int aic_iw_set_sta_enable(struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	int ret = 1;
	printf("%s in, extra %s\n", __func__, extra);
	int fhost_vif_idx = 0;
	if (*extra == '0')
		ret = wlan_disconnect_sta(fhost_vif_idx);
	else if (*extra == '1') {
		ret = wlan_start_sta("Empty_SSID", "Empty_Password", -1);
		if (ret) {
			AIC_LOG_PRINTF("wlan_start_sta fail, ret=%d\n", ret);
			return -1;
		}
#if 0
		ret = wlan_sta_connect("Xiaomi_66D0_5G", "qqqqqqqq", 12000);
		if (ret) {
			AIC_LOG_PRINTF("wlan_sta_connect fail, ret=%d\n", ret);
			return -1;
		}
#endif
	}
exit:
	return ret;
}

int aic_wx_get_private(struct iw_request_info *info,
		       union iwreq_data *wrqu, char *extra)
{
	printf("%s in %d %d\n", __func__, num_of_private_args, wrqu->data.length);
	if (wrqu->data.length < num_of_private_args) {
		wrqu->data.length = num_of_private_args;
		printf("Func:%s, No enough size!\n",__func__);
		return -E2BIG;
	}
	wrqu->data.length = num_of_private_args;
	memcpy(extra, aic_private_args, sizeof(struct iw_priv_args) * wrqu->data.length);
	return 0;
}

int aic_p2p_set(struct iw_request_info *info,
		       union iwreq_data *wrqu, char *extra)
{
	if (!_aic_memcmp(extra, "enable=", 7))
		return aic_iw_set_p2p_enable(info, wrqu, &extra[7]);
	else if(!_aic_memcmp(extra, "setDN=", 6))
		return aic_p2p_setDN(info, wrqu, &extra[6]);
	else if(!_aic_memcmp(extra, "wfd_type=", 9))
		return aic_iw_set_wfd_type(info, wrqu, &extra[9]);
	else if(!_aic_memcmp(extra, "wfd_enable=", 11))
		return aic_iw_set_wfd_enable(info, wrqu, &extra[11]);
	else if(!_aic_memcmp(extra, "sta_enable=", 11))
		return aic_iw_set_sta_enable(info, wrqu, &extra[11]);
	else
		printf("%s failed\n", __func__);
	return -EFAULT;
}

int dummy(struct iw_request_info *a, union iwreq_data *wrqu, char *b)
{
	/* PLACE_HOLDER FUNC*/
	return -1;

}
