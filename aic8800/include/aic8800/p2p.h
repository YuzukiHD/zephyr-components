/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AIC8800_P2P_H_
#define AIC8800_P2P_H_

#include <stddef.h>
#include <stdint.h>

/**
 * Start an exclusive Wi-Fi Direct group owner advertising a primary WFD sink.
 * The chip must be stopped first; STA and Bluetooth users must release it.
 * The printable ASCII device name is 1..32 bytes. PBC is enabled for 120 seconds;
 * call aic8800_p2p_accept() to open another pairing window. A random WPA2 key
 * is generated per group. The GO is 192.168.49.1/24 and runs a DHCP server.
 * Requires CONFIG_AIC8800_P2P_WFD. Call from an application thread.
 */
int aic8800_p2p_start(const char *name, uint16_t rtsp_port);
int aic8800_p2p_accept(void);
/** First allocated DHCP lease (the WFD source), or -EAGAIN before it joins. */
int aic8800_p2p_peer(char *ipv4, size_t size);
/** Stops the DHCP server, removes the group, and powers the chip off. */
int aic8800_p2p_stop(void);

#endif
