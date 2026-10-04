#ifndef _IW_H
#define _IW_H

#include "iw_handler.h"
#include "wifi.h"
#include "wlan_if.h"

#define	EFAULT		14	/* Bad address */

int aic_p2p_set(struct iw_request_info *info, union iwreq_data *wrqu, char *extra);

int aic_get_private(struct iw_request_info *info, union iwreq_data *wrqu, char *extra);

int dummy(struct iw_request_info *a, union iwreq_data *wrqu, char *b);

static int aic8800_eth_proc_wireless_ioctl(struct iwreq *ifr, unsigned int cmd,
				struct iw_request_info *info);

int aic_wx_get_private(struct iw_request_info *info, union iwreq_data *wrqu, char *extra);


static iw_handler aic_private_handler[] = {
	dummy,//aic_wx_write32,					/* 0x00 */
	dummy,//aic_wx_read32,					/* 0x01 */
#ifndef CONFIG_IOCTL_DBG
	dummy,//NULL,							/* 0x02 */
#else
	dummy,//aic_ioctl_dbg,
#endif
#ifdef MP_IOCTL_HDL
	dummy,//aic_mp_ioctl_hdl,				/* 0x03 */
#else
	dummy,//aic_wx_priv_null,
#endif
	/* for MM DTV platform */
	dummy,//aic_get_ap_info,				/* 0x04 */

	dummy,//aic_set_pid,					/* 0x05 */
	dummy,//aic_wps_start,					/* 0x06 */

	/* for PLATFORM_MT53XX */
	dummy,//aic_wx_get_sensitivity,			/* 0x07 */
	dummy,//aic_wx_set_mtk_wps_probe_ie,	/* 0x08 */
	dummy,//aic_wx_set_mtk_wps_ie,			/* 0x09 */

	/* Set Channel depend on the country code */
	dummy,//aic_wx_set_channel_plan,		/* 0x0A */

	dummy,//aic_dbg_port,					/* 0x0B */
	dummy,//aic_wx_write_rf,				/* 0x0C */
	dummy,//aic_wx_read_rf,					/* 0x0D */

	dummy,//aic_priv_set,					/*0x0E*/
	dummy,//aic_priv_get,					/*0x0F*/

	aic_p2p_set,					/* 0x10 */
	dummy,//aic_p2p_get,					/* 0x11 */
	dummy,//NULL,							/* 0x12 */
	dummy,//aic_p2p_get2,					/* 0x13 */

	dummy,//aic_tdls,						/* 0x14 */
	dummy,//aic_tdls_get,					/* 0x15 */

	dummy,//aic_pm_set,						/* 0x16 */
#ifdef CONFIG_80211K
	dummy,//aic_wx_priv_rrm,				/* 0x17 */
#else
	dummy,//aic_wx_priv_null,				/* 0x17 */
#endif
	dummy,//aic_rereg_nd_name,				/* 0x18 */
	dummy,//aic_wx_priv_null,				/* 0x19 */
#ifdef CONFIG_MP_INCLUDED
	dummy,//aic_wx_priv_null,				/* 0x1A */
	dummy,//aic_wx_priv_null,				/* 0x1B */
#else
	dummy,//aic_wx_priv_null,				/* 0x1A */
	dummy,//aic_mp_efuse_get,				/* 0x1B */
#endif
	dummy,//NULL,							/* 0x1C is reserved for hostapd */
	dummy,//aic_test,						/* 0x1D */
#ifdef CONFIG_INTEL_WIDI
	dummy,//aic_widi_set,					/* 0x1E */
	dummy,//aic_widi_set_probe_request,		/* 0x1F */
#endif /* CONFIG_INTEL_WIDI */
};

static iw_handler aic_handlers[] = {
	dummy,//NULL,					/* SIOCSIWCOMMIT */
	dummy,//aic_wx_get_name,		/* SIOCGIWNAME */
	dummy,					/* SIOCSIWNWID */
	dummy,					/* SIOCGIWNWID */
	dummy,//aic_wx_set_freq,		/* SIOCSIWFREQ */
	dummy,//aic_wx_get_freq,		/* SIOCGIWFREQ */
	dummy,//aic_wx_set_mode,		/* SIOCSIWMODE */
	dummy,//aic_wx_get_mode,		/* SIOCGIWMODE */
	dummy,//dummy,					/* SIOCSIWSENS */
	dummy,//aic_wx_get_sens,		/* SIOCGIWSENS */
	dummy,					/* SIOCSIWRANGE */
	dummy,//aic_wx_get_range,		/* SIOCGIWRANGE */
	dummy,//aic_wx_set_priv,		/* SIOCSIWPRIV */
	aic_wx_get_private,		/* SIOCGIWPRIV */
	dummy,//NULL,					/* SIOCSIWSTATS */
	dummy,//NULL,					/* SIOCGIWSTATS */
	dummy,					/* SIOCSIWSPY */
	dummy,					/* SIOCGIWSPY */
	dummy,//NULL,					/* SIOCGIWTHRSPY */
	dummy,//NULL,					/* SIOCWIWTHRSPY */
	dummy,//aic_wx_set_wap,			/* SIOCSIWAP */
	dummy,//aic_wx_get_wap,			/* SIOCGIWAP */
	dummy,//aic_wx_set_mlme,		/* request MLME operation; uses struct iw_mlme */
	dummy,					/* SIOCGIWAPLIST -- depricated */
	dummy,//aic_wx_set_scan,		/* SIOCSIWSCAN */
	dummy,//aic_wx_get_scan,		/* SIOCGIWSCAN */
	dummy,//aic_wx_set_essid,		/* SIOCSIWESSID */
	dummy,//aic_wx_get_essid,		/* SIOCGIWESSID */
	dummy,					/* SIOCSIWNICKN */
	dummy,//aic_wx_get_nick,		/* SIOCGIWNICKN */
	dummy,//NULL,					/* -- hole -- */
	dummy,//NULL,					/* -- hole -- */
	dummy,//aic_wx_set_rate,		/* SIOCSIWRATE */
	dummy,//aic_wx_get_rate,		/* SIOCGIWRATE */
	dummy,//aic_wx_set_rts,			/* SIOCSIWRTS */
	dummy,//aic_wx_get_rts,			/* SIOCGIWRTS */
	dummy,//aic_wx_set_frag,		/* SIOCSIWFRAG */
	dummy,//aic_wx_get_frag,		/* SIOCGIWFRAG */
	dummy,					/* SIOCSIWTXPOW */
	dummy,					/* SIOCGIWTXPOW */
	dummy,					/* SIOCSIWRETRY */
	dummy,//aic_wx_get_retry,		/* SIOCGIWRETRY */
	dummy,//aic_wx_set_enc,			/* SIOCSIWENCODE */
	dummy,//aic_wx_get_enc,			/* SIOCGIWENCODE */
	dummy,					/* SIOCSIWPOWER */
	dummy,//aic_wx_get_power,		/* SIOCGIWPOWER */
	dummy,//NULL,					/*---hole---*/
	dummy,//NULL,					/*---hole---*/
	dummy,//aic_wx_set_gen_ie,		/* SIOCSIWGENIE */
	dummy,//NULL,					/* SIOCGWGENIE */
	dummy,//aic_wx_set_auth,		/* SIOCSIWAUTH */
	dummy,					/* SIOCGIWAUTH */
	dummy,//aic_wx_set_enc_ext,		/* SIOCSIWENCODEEXT */
	dummy,					/* SIOCGIWENCODEEXT */
	dummy,//aic_wx_set_pmkid,		/* SIOCSIWPMKSA */
	dummy,//NULL,					/*---hole---*/
};

static const struct iw_priv_args aic_private_args[] = {
	{
		SIOCIWFIRSTPRIV + 0x0,
		IW_PRIV_TYPE_CHAR | 0x7FF, 0, "write"
	},
	{
		SIOCIWFIRSTPRIV + 0x1,
		IW_PRIV_TYPE_CHAR | 0x7FF,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | IFNAMSIZ, "read"
	},
#ifndef CONFIG_IOCTL_DBG
	{
		SIOCIWFIRSTPRIV + 0x2, 0, 0, "driver_ext"
	},
#else
	{
		SIOCIWFIRSTPRIV + 0x2,
		IW_PRIV_TYPE_CHAR | 1024,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK,
		"iwdbg"
	},
#endif
	{
		SIOCIWFIRSTPRIV + 0x3, 0, 0, "mp_ioctl"
	},
	{
		SIOCIWFIRSTPRIV + 0x4,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "apinfo"
	},
	{
		SIOCIWFIRSTPRIV + 0x5,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, 0, "setpid"
	},
	{
		SIOCIWFIRSTPRIV + 0x6,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "wps_start"
	},
	/* for PLATFORM_MT53XX	 */
	{
		SIOCIWFIRSTPRIV + 0x7,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "get_sensitivity"
	},
	{
		SIOCIWFIRSTPRIV + 0x8,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "wps_prob_req_ie"
	},
	{
		SIOCIWFIRSTPRIV + 0x9,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "wps_assoc_req_ie"
	},

	/* for RTK_DMP_PLATFORM	 */
	{
		SIOCIWFIRSTPRIV + 0xA,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "channel_plan"
	},

	{
		SIOCIWFIRSTPRIV + 0xB,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, 0, "dbg"
	},
	{
		SIOCIWFIRSTPRIV + 0xC,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 3, 0, "rfw"
	},
	{
		SIOCIWFIRSTPRIV + 0xD,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | IFNAMSIZ, "rfr"
	},
#if 0
	{
		SIOCIWFIRSTPRIV + 0xE, 0, 0, "wowlan_ctrl"
	},
#endif
	{
		SIOCIWFIRSTPRIV + 0x10,
		IW_PRIV_TYPE_CHAR | 1024, 0, "p2p_set"
	},
	{
		SIOCIWFIRSTPRIV + 0x11,
		IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK , "p2p_get"
	},
	{
		SIOCIWFIRSTPRIV + 0x12, 0, 0, "NULL"
	},
	{
		SIOCIWFIRSTPRIV + 0x13,
		IW_PRIV_TYPE_CHAR | 64, IW_PRIV_TYPE_CHAR | 64 , "p2p_get2"
	},
	{
		SIOCIWFIRSTPRIV + 0x14,
		IW_PRIV_TYPE_CHAR  | 64, 0, "tdls"
	},
	{
		SIOCIWFIRSTPRIV + 0x15,
		IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | 1024 , "tdls_get"
	},
	{
		SIOCIWFIRSTPRIV + 0x16,
		IW_PRIV_TYPE_CHAR | 64, 0, "pm_set"
	},
#ifdef CONFIG_80211K
	{
		SIOCIWFIRSTPRIV + 0x17,
		IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | 1024 , "rrm"
	},
#endif
	{SIOCIWFIRSTPRIV + 0x18, IW_PRIV_TYPE_CHAR | IFNAMSIZ , 0 , "rereg_nd_name"},
#ifdef CONFIG_MP_INCLUDED
	{SIOCIWFIRSTPRIV + 0x1A, IW_PRIV_TYPE_CHAR | 1024, 0,  "NULL"},
	{SIOCIWFIRSTPRIV + 0x1B, IW_PRIV_TYPE_CHAR | 128, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "NULL"},
#else
	{SIOCIWFIRSTPRIV + 0x1A, IW_PRIV_TYPE_CHAR | 1024, 0,  "NULL"},
	{SIOCIWFIRSTPRIV + 0x1B, IW_PRIV_TYPE_CHAR | 128, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "efuse_get"},
#endif
	{
		SIOCIWFIRSTPRIV + 0x1D,
		IW_PRIV_TYPE_CHAR | 40, IW_PRIV_TYPE_CHAR | 0x7FF, "test"
	},

#ifdef CONFIG_INTEL_WIDI
	{
		SIOCIWFIRSTPRIV + 0x1E,
		IW_PRIV_TYPE_CHAR | 1024, 0, "widi_set"
	},
	{
		SIOCIWFIRSTPRIV + 0x1F,
		IW_PRIV_TYPE_CHAR | 128, 0, "widi_prob_req"
	},
#endif /* CONFIG_INTEL_WIDI */

	{ SIOCIWFIRSTPRIV + 0x0E, IW_PRIV_TYPE_CHAR | 1024, 0 , ""},  /* set  */
	{ SIOCIWFIRSTPRIV + 0x0F, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK , ""},/* get
 * --- sub-ioctls definitions --- */
};

static __attribute__((aligned(4))) __u16 num_of_standard_handlers = sizeof(aic_handlers) / sizeof(iw_handler);
static __attribute__((aligned(4))) __u16 num_of_private_handlers = sizeof(aic_private_handler) / sizeof(iw_handler);
static __attribute__((aligned(4))) __u16 num_of_private_args = sizeof(aic_private_args) / sizeof(struct iw_priv_args);

static int _aic_memcmp(const void *s1, const void *s2, size_t n)
{
	const uint8_t *ptr1=s1, *ptr2=s2;

	while(n) {
		int diff = (*ptr1++ - *ptr2++);
		if (diff)
			return diff;
		n--;
	}

	return 0;
}

#endif //_IW_H