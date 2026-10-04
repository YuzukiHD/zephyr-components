/*
 * Wi-Fi APIs
 */

#include <stdbool.h>

#if 0
struct wifi_config
{

};
int _fdlib_version = 0;

int wifi_get_mac(char *mac)
{
	return 0;

}
int wifi_sigtest_hdlr(char *at_cmd, char *at_rsp)
{
	return 0;

}
int iwnpi_cmd_handler(int argc, char **argv, char *iwnpi_ret)
{
	return 0;

}

unsigned int wifi_get_netid_by_name(const char *name)
{
	return 0;

}
int wifi_eut_hdlr(char *diag_cmd, char *at_rsp)
{
	return 0;

}
int wifi_sta_get_conf(struct wifi_config *conf)
{

	return 0;
}

int wifi_sta_set_conf(struct wifi_config *conf)
{

	return 0;
}

int wifi_sta_clear_conf(void *conf)
{
	int ret;


	return ret;
}

void *wifi_sta_scan_results(void)
{
	return 0;
}


int wifi_sta_scan(void *params)
{
	return 0;
}

int wifi_sta_connect(void)
{

	return 0;
}

int wifi_sta_disconnect(void)
{

	return 0;
}

int wifi_sta_open(void)
{
	int ret;


	return 0;
}

int wifi_sta_close(void)
{


	return 0;
}

int wifi_ap_get_mac(char mac[6])
{
	return 0;
}

int wifi_ap_start_wps(char mode, char *pin, int timeout)
{
	int ret = 0;

	return ret;
}

int wifi_ap_cancel_wps(void)
{
	return 0;
}

int wifi_ap_set_conf(void *conf)
{

	return 0;
}

int wifi_ap_clear_conf(void)
{
	return 0;
}

int wifi_ap_start_ap(void)
{
	int size;

	return 0;
}

int wifi_ap_stop_ap(void)
{

	return 0;
}

int wifi_ap_open(void)
{
	int ret, net_id;


	return net_id;
}

int wifi_ap_close(void)
{
	int ret = 0;
	return ret;
}

void *wifi_ap_get_assoc_sta(void)
{

	return 0;
}

int wifi_ap_del_station(char *mac)
{
	int ret = 0;

	return ret;
}

unsigned char wifi_ap_get_state(void)
{
	int net_id;

	return net_id;
}
int wifi_ap_get_mac_acl_mode(void)
{
	return 0;
}

int wifi_ap_set_mac_acl_mode(char mode)
{
	int size;

	return size;
}

void *wifi_ap_get_mac_acl(void)
{
	return 0;
}

int wifi_ap_set_mac_acl(char subcmd, char *mac_acl)
{
	int ret;

	return ret;
}

int wifi_ap_set_max_sta(unsigned char max_sta)
{
	int ret;
	return ret;
}
#endif

