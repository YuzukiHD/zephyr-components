#include <stdint.h>
#include "rtos_al.h"
#include "fhost_wpa_config.h"

enum {
	MSG_EXCESSIVE, MSG_MSGDUMP, MSG_DEBUG, MSG_INFO, MSG_WARNING, MSG_ERROR
};

int fhost_wpa_debug_level = MSG_INFO;

struct fhost_interworking_cfg aic_interworking_cfg = {
    .interworking = 1,
    .access_network_type = 0x03,
    .internet=  1,
    .asra = 0,
    .esr = 0,
    .uesa = 0,
    .venue_info_set = 1,
    .venue_group = 0x0A,
    .venue_type = 0x03,
};

void fhost_set_interworking_cfg(struct fhost_interworking_cfg *cfg)
{
	if (cfg) {
		rtos_memcpy(&aic_interworking_cfg, cfg, sizeof(*cfg));
	} 
}

