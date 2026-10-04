/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
 *
 * All Rights Reserved
 */

#include "iw.h"
//#include <log.h>

static iw_handler aic8800_eth_get_handler(unsigned int cmd)
{
	/* Don't "optimise" the following variable, it will crash */
	unsigned int	index;		/* *MUST* be unsigned */
	printf("%s cmd %x, %x, %x\n", __func__, cmd, SIOCGIWPRIV, SIOCIWFIRSTPRIV);

	/* Try as a standard command */
	index = IW_IOCTL_IDX(cmd);
	diag_printf("index:%d,std num:%d,priv num:%d\n",index, num_of_standard_handlers, num_of_private_handlers);
	if (index < num_of_standard_handlers)
		return aic_handlers[index];

	/* Try as a private command */
	index = cmd - SIOCIWFIRSTPRIV;
	if (index < num_of_private_handlers)
		return aic_private_handler[index];
	
	/* Not found */
	return NULL;
}

static int aic8800_eth_proc_wireless_ioctl(struct iwreq *ifr, unsigned int cmd,
				struct iw_request_info *info) {
	struct iwreq *iwr = (struct iwreq *)ifr;
	iw_handler	handler;
	handler = aic8800_eth_get_handler(cmd);
	if (handler) {
		if(iwr->u.data.pointer) {
			/* Standard and private handler are not the same */
			return handler(info, &iwr->u, iwr->u.data.pointer);
		} else
			printf("data pointer is null\n");
	}

	AIC_LOG_PRINTF("no ioctl supported\n");
	return 0;
}

int iw_control(int request, struct iwreq *	pwrq) {
	return aic8800_eth_proc_wireless_ioctl(pwrq, request, NULL);
}


