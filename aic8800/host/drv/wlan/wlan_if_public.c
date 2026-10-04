#include "lmac_types.h"
#include "fhost_wpa.h"
#include "fhost_config.h"
#include "fhost_cntrl.h"
#include "rtos_al.h"
#include "net_al.h"
#include "fhost.h"
#include "mac.h"
#include "rwnx_msg_tx.h"
#include "fhost_tx.h"
#include "wlan_if.h"
#include "compiler.h"
#include "porting.h"
//#include "log.h"

static int mac_acl_mode;
static int last_mac_acl_mode;
static struct co_list mac_acl_list;

#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

static int _is_multicast_ether_addr(const u8 *a)
{
	return a[0] & 0x01;
}

static int _is_zero_ether_addr(const u8 *a)
{
	return !(a[0] | a[1] | a[2] | a[3] | a[4] | a[5]);
}

static struct wifi_mac_node *fetch_mac(struct co_list *list, char *mac)
{
	struct wifi_mac_node *mac_node, *temp;

	/* Loop through list to find the corresponding event */
	mac_node = (struct wifi_mac_node *)co_list_pick(list);

	while (mac_node) {
		if (!memcmp(mac_node->mac, mac, WIFI_MAC_ADDR_LEN)) {
            return mac_node;
        }
		mac_node = (struct wifi_mac_node *)co_list_next(&mac_node->node);
	}
	return NULL;
}

int wlan_ap_get_mac_acl_mode(void)
{
	return mac_acl_mode;
}

static __inline const char *acl_mode2str(char mode)
{
	char *str = NULL;

	switch (mode) {
	case WIFI_MAC_ACL_DISABLED:
		str = "DISABLED";
		break;
	case WIFI_MAC_ACL_BLACKLIST:
		str = "BLACKLIST";
		break;
	case WIFI_MAC_ACL_WHITELIST:
		str = "WHITELIST";
		break;
	default:
		str = "Unsupported MAC ACL mode";
		break;
	}
	return str;
}

//called in wlan_start_ap
int wlan_ap_mac_acl_init(void)
{
    mac_acl_mode = WIFI_MAC_ACL_DISABLED;
    last_mac_acl_mode = WIFI_MAC_ACL_DISABLED;
    co_list_init(&mac_acl_list);

    return 0;
}

//called in wlan_stop_ap
int wlan_ap_mac_acl_deinit(void)
{
    struct wifi_mac_node *marked_sta = NULL;

    while (1) {
        marked_sta = co_list_pop_front(&mac_acl_list);
        if (marked_sta == NULL) {
            break;
        }
        rtos_free(marked_sta);
    }
}

int wlan_ap_set_mac_acl_mode(char mode)
{
	int size;
	int ret = -1;
	void *dev;
	unsigned char mac[WIFI_MAC_ADDR_LEN];
	struct wifi_mac_node *marked_sta = NULL;
	struct sta_info_tag *tmp, *assoc_sta;

	if (mode == mac_acl_mode) {
		aic_dbg("MAC ACL mode unchanged!(%d -> %d)\n", mode, mac_acl_mode);
		return ret;
	}

	switch (mode) {
	case WIFI_MAC_ACL_BLACKLIST:
	case WIFI_MAC_ACL_WHITELIST:
		if (mac_acl_mode != WIFI_MAC_ACL_DISABLED) {
			aic_dbg("Acl Mode(%s) already enabled!\n", acl_mode2str(mac_acl_mode));
			return -1;
		}
	case WIFI_MAC_ACL_DISABLED:
		last_mac_acl_mode = mac_acl_mode;
		mac_acl_mode = mode;
		break;
	default:
		return -EINVAL;
	}
    aic_dbg("MAC ACL mode %d\n", mac_acl_mode);

	memset(mac, 0xff, WIFI_MAC_ADDR_LEN);

	if (mac_acl_mode == WIFI_MAC_ACL_WHITELIST) {
		marked_sta = (struct wifi_mac_node *)co_list_pick(&mac_acl_list);
		while (marked_sta) {
			aic_dbg(" add (" MACSTR ") to whiteklist\n",
			 	    MAC2STR((unsigned char *)marked_sta->mac));
			ret = fhost_ap_add_whitelist(0, marked_sta->mac);
			marked_sta = (struct wifi_mac_node *)co_list_next(&marked_sta->node);
		}
        fhost_ap_macaddr_acl(0, 1);
	} else	if (mac_acl_mode == WIFI_MAC_ACL_BLACKLIST){
        #if 0
		marked_sta = (struct wifi_mac_node *)co_list_pick(&mac_acl_list);
		while (marked_sta) {
			aic_dbg(" add (" MACSTR ") to blacklist\n",
			 	    MAC2STR((unsigned char *)marked_sta->mac));
			ret = fhost_ap_add_blacklist(0, marked_sta->mac);
			marked_sta = (struct wifi_mac_node *)co_list_next(&marked_sta->node);
		}
        #endif
	} else if (mac_acl_mode == WIFI_MAC_ACL_DISABLED) {
		if (last_mac_acl_mode == WIFI_MAC_ACL_WHITELIST) {
			marked_sta = (struct wifi_mac_node *)co_list_pop_front(&mac_acl_list);
			while (marked_sta) {
				aic_dbg(" del (" MACSTR ") from Whitelist\n",
				 	MAC2STR((unsigned char *)marked_sta->mac));

				fhost_ap_delete_whitelist(0, marked_sta->mac);
                #if 0
                struct vif_info_tag *vif = fhost_to_mac_vif(0);
                assoc_sta = (struct sta_info_tag *)co_list_pick(&vif->sta_list);
                while (assoc_sta) {
                    if (!memcmp(marked_sta->mac, assoc_sta->mac_addr.array, WIFI_MAC_ADDR_LEN)) {
                        aic_dbg("the assoc sta ("MACSTR") in  whitelist , del it\r\n",
                            MAC2STR((unsigned char *)assoc_sta->mac_addr.array));
                        wlan_ap_disassociate_sta(assoc_sta->mac_addr.array);
                
                    }
                    assoc_sta = (struct sta_info_tag *)co_list_next(&assoc_sta->list_hdr);
                }
                #endif
                rtos_free(marked_sta);
				marked_sta = (struct wifi_mac_node *)co_list_next(&marked_sta->node);
			}
		
		} else if (last_mac_acl_mode == WIFI_MAC_ACL_BLACKLIST){
			marked_sta = (struct wifi_mac_node *)co_list_pop_front(&mac_acl_list);
			while (marked_sta) {
				aic_dbg(" del (" MACSTR ") from Blaklist\n",
				 	MAC2STR((unsigned char *)marked_sta->mac));

				fhost_ap_delete_blacklist(0, marked_sta->mac);
                rtos_free(marked_sta);
				marked_sta = (struct wifi_mac_node *)co_list_next(&marked_sta->node);
			}
		}
        co_list_init(&mac_acl_list);
        fhost_ap_macaddr_acl(0, 0);
	}

    #if 0
	if (mac_acl_mode == WIFI_MAC_ACL_WHITELIST) {
		dl_list_for_each_safe(assoc_sta, tmp, &assoc_list,
			      struct wifi_mac_node, node) {
			marked_sta = fetch_mac(&mac_acl_list, assoc_sta->mac);
			if (!marked_sta) {
				aic_dbg(MSG_INFO, "the assoc sta ("MACSTR") not in whitelist , del it",
					MAC2STR((unsigned char *)assoc_sta->mac));
				wifi_ap_del_station(assoc_sta->mac);
			}
		}
	} else if (mac_acl_mode == WIFI_MAC_ACL_BLACKLIST) {
		struct vif_info_tag *vif = fhost_to_mac_vif(0);

		assoc_sta = (struct sta_info_tag *)co_list_pick(&vif->sta_list);
		while (assoc_sta) {
			marked_sta = fetch_mac(&mac_acl_list, assoc_sta->mac_addr.array);
			if (marked_sta) {
				aic_dbg("the assoc sta ("MACSTR") in  blacklist , del it\r\n",
					MAC2STR((unsigned char *)assoc_sta->mac_addr.array));
				wlan_ap_disassociate_sta(assoc_sta->mac_addr.array);
			}
			assoc_sta = (struct sta_info_tag *)co_list_next(&assoc_sta->list_hdr);
		}
	}
    #endif
	return ret;
}

int wlan_ap_add_blacklist(struct mac_addr *macaddr)
{
	int ret = 0;
	struct wifi_mac_node *marked_sta = NULL;
	unsigned char mac[WIFI_MAC_ADDR_LEN];

	if (macaddr && !_is_multicast_ether_addr((const u8 *)macaddr) && 
		!_is_zero_ether_addr((const u8 *)macaddr)) {
		memcpy(mac, macaddr, WIFI_MAC_ADDR_LEN);
	} else if (!macaddr) {
		memset(mac, 0xff, WIFI_MAC_ADDR_LEN);
	} else {
		aic_dbg("invalid MAC address!\n");
		return -1;
	}

	marked_sta = rtos_malloc(sizeof(struct wifi_mac_node));
	if (!marked_sta) {
		ret = -ENOMEM;
		return -1;
	}
	memset(marked_sta, 0, sizeof(struct wifi_mac_node));
	memcpy(marked_sta->mac, mac, WIFI_MAC_ADDR_LEN);
	co_list_push_back(&mac_acl_list, &marked_sta->node);
    ret = fhost_ap_add_blacklist(0, marked_sta->mac);
	aic_dbg("Added in blacklist\r\n");

	return ret;
}
int wlan_ap_delete_blacklist(struct mac_addr *macaddr)
{
	int ret = 0;
	struct wifi_mac_node *marked_sta = NULL;
	unsigned char mac[WIFI_MAC_ADDR_LEN];
	struct sta_info_tag *assoc_sta;

	if (macaddr && !_is_multicast_ether_addr((const u8 *)macaddr) && 
		!_is_zero_ether_addr((const u8 *)macaddr)) {
		memcpy(mac, macaddr, WIFI_MAC_ADDR_LEN);
	} else if (!macaddr) {
		memset(mac, 0xff, WIFI_MAC_ADDR_LEN);
	} else {
		aic_dbg("invalid MAC address!\n");
		return -1;
	}
	
	marked_sta = fetch_mac(&mac_acl_list, mac);

	if (marked_sta) {
		fhost_ap_delete_blacklist(0, marked_sta->mac);
		co_list_extract(&mac_acl_list, &marked_sta->node);
		rtos_free(marked_sta);
        marked_sta = NULL;
		aic_dbg("Delete Black list\r\n");
	}
	return ret;
}

int wlan_ap_show_blacklist(uint8_t *list, uint16_t *list_len)
{
	int ret = 0;

	fhost_ap_show_blacklist(0, list, list_len);

	return ret;
}

int wlan_ap_add_whitelist(struct mac_addr *macaddr)
{
	int ret = 0;
	struct wifi_mac_node *marked_sta = NULL;
	unsigned char mac[WIFI_MAC_ADDR_LEN];

	if (macaddr && !_is_multicast_ether_addr((const u8 *)macaddr) && 
		!_is_zero_ether_addr((const u8 *)macaddr)) {
		memcpy(mac, macaddr, WIFI_MAC_ADDR_LEN);
	} else if (!macaddr) {
		memset(mac, 0xff, WIFI_MAC_ADDR_LEN);
	} else {
		aic_dbg("invalid MAC address!\n");
		return -1;
	}

	marked_sta = rtos_malloc(sizeof(struct wifi_mac_node));
	if (!marked_sta) {
		ret = -ENOMEM;
		return -1;
	}
	memset(marked_sta, 0, sizeof(struct wifi_mac_node));
	memcpy(marked_sta->mac, mac, WIFI_MAC_ADDR_LEN);
	co_list_push_back(&mac_acl_list, &marked_sta->node);
	aic_dbg("Added White list\r\n");

	return ret;
}
int wlan_ap_delete_whitelist(struct mac_addr *macaddr)
{
	int ret = 0;
	struct wifi_mac_node *marked_sta = NULL;
	unsigned char mac[WIFI_MAC_ADDR_LEN];
	struct sta_info_tag *assoc_sta;

	if (macaddr && !_is_multicast_ether_addr((const u8 *)macaddr) && 
		!_is_zero_ether_addr((const u8 *)macaddr)) {
		memcpy(mac, macaddr, WIFI_MAC_ADDR_LEN);
	} else if (!macaddr) {
		memset(mac, 0xff, WIFI_MAC_ADDR_LEN);
	} else {
		aic_dbg("invalid MAC address!\n");
		return -1;
	}
	
	marked_sta = fetch_mac(&mac_acl_list, mac);
	if (marked_sta) {
        fhost_ap_delete_whitelist(0, marked_sta->mac);
        #if 0
        struct vif_info_tag *vif = fhost_to_mac_vif(0);

		assoc_sta = (struct sta_info_tag *)co_list_pick(&vif->sta_list);
		while (assoc_sta) {
			if (!memcmp(marked_sta->mac, assoc_sta->mac_addr.array, WIFI_MAC_ADDR_LEN)) {
                aic_dbg("the assoc sta ("MACSTR") in  whitelist , del it\r\n",
                    MAC2STR((unsigned char *)assoc_sta->mac_addr.array));
                wlan_ap_disassociate_sta(assoc_sta->mac_addr.array);

            }
			assoc_sta = (struct sta_info_tag *)co_list_next(&assoc_sta->list_hdr);
		}
        #endif
        co_list_extract(&mac_acl_list, marked_sta);
		rtos_free(marked_sta);
        marked_sta = NULL;

		aic_dbg("Delete white list\r\n");
	}
	return ret;
}

int wlan_ap_show_whitelist(uint8_t *list, uint16_t *list_len)
{
	int ret = 0;

	fhost_ap_show_whitelist(0, list, *list_len);

	return ret;
}
uint8_t wlan_ap_get_mac_acl_list_cnt(void)
{
    return co_list_cnt(&mac_acl_list);
}
void * wlan_ap_get_mac_acl_list(void)
{
    return &mac_acl_list;
}
uint8_t wlan_ap_get_associated_sta_cnt(void)
{
    return vif_mgmt_sta_cnt();
}
void *wlan_ap_get_associated_sta_list(void)
{
    return get_vif_mgmt_sta_list();
}
int8_t wlan_ap_get_associated_sta_rssi(uint8_t *addr)
{
    return data_pkt_rssi_get(addr);
}
#undef MAC2STR(a)
