#ifndef AIC_COMPAT_EFUSE_H_
#define AIC_COMPAT_EFUSE_H_

#include <zephyr/drivers/hwinfo.h>

/* the 128 bit chip identifier the MAC address is derived from; stays zero when unreadable */
static inline int hal_efuse_get_chipid(unsigned char *buf)
{
	return hwinfo_get_device_id(buf, 16);
}

#endif
