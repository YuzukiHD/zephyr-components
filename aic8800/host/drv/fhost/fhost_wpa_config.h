#ifndef _FHOST_WPA_CONFIG_H_
#define _FHOST_WPA_CONFIG_H_

extern int fhost_wpa_debug_level;

struct fhost_interworking_cfg {
	/* IEEE 802.11u - Interworking */
	int interworking;
	int access_network_type;
	int internet;
	int asra;
	int esr;
	int uesa;
	int venue_info_set;
	unsigned char venue_group;
	unsigned char venue_type;
};
extern struct fhost_interworking_cfg aic_interworking_cfg ;
void fhost_set_interworking_cfg(struct fhost_interworking_cfg *cfg);

#endif
