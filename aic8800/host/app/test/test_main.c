#include "test_main.h"
#include "lmac_mac.h"
#include "fhost_api.h"
#include "fhost_cntrl.h"
//#include "log.h"

void test_wifi_scan(void)
{
    int nb_res, fvif_idx = 0;
    struct mac_scan_result result;
    struct fhost_cntrl_link *link = fhost_cntrl_cfgrwnx_link_open();
    if (link == NULL) {
        aic_dbg("Failed to open link with control task\n");
        return;
    }
    // Reset STA interface (this will end previous wpa_supplicant task)
    if (fhost_set_vif_type(link, fvif_idx, VIF_UNKNOWN, false) ||
        fhost_set_vif_type(link, fvif_idx, VIF_STA, false)) {
        fhost_cntrl_cfgrwnx_link_close(link);
        aic_dbg("Failed to set vif type\n");
        return;
    }

    nb_res = fhost_scan(link, fvif_idx, NULL);
    aic_dbg("Got %d scan results\n", nb_res);

    nb_res = 0;
    while(fhost_get_scan_results(link, nb_res++, 1, &result)) {
        result.ssid.array[result.ssid.length] = '\0'; // set ssid string ending
        aic_dbg("(%3d dBm) CH=%3d BSSID=%02x:%02x:%02x:%02x:%02x:%02x SSID=%s\n",
            (int8_t)result.rssi, phy_freq_to_channel(result.chan->band, result.chan->freq),
            ((uint8_t *)result.bssid.array)[0], ((uint8_t *)result.bssid.array)[1],
            ((uint8_t *)result.bssid.array)[2], ((uint8_t *)result.bssid.array)[3],
            ((uint8_t *)result.bssid.array)[4], ((uint8_t *)result.bssid.array)[5],
            (char *)result.ssid.array);
    }
    fhost_cntrl_cfgrwnx_link_close(link);
}

void test_rtos_al(void)
{
}

void test_main_entry(void)
{
    // rtos al test
    test_rtos_al();

    // wifi scan test
    test_wifi_scan();
}
