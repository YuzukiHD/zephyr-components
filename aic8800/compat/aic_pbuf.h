/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * Packet buffers of the driver: one contiguous block with headroom in front of the
 * payload, so that headers can be pushed and pulled without a copy. The buffers
 * are reference counted; the payload is 64 byte aligned for the DMA of the bus.
 */

#ifndef AIC_PBUF_H_
#define AIC_PBUF_H_

#include <stddef.h>
#include <stdint.h>

typedef enum {
	PBUF_TRANSPORT = 0,
	PBUF_IP,
	PBUF_LINK,
	PBUF_RAW_TX,
	PBUF_RAW,
} pbuf_layer;

typedef enum {
	PBUF_RAM = 0,
	PBUF_ROM,
	PBUF_REF,
	PBUF_POOL,
} pbuf_type;

struct pbuf {
	struct pbuf *next;
	void *payload;
	uint16_t tot_len;
	uint16_t len;
	uint8_t type_internal;
	uint8_t flags;
	uint16_t ref;
};

/** Bytes that can be added in front of the payload of a new buffer */
#define PBUF_HEADROOM 128

struct pbuf *pbuf_alloc(pbuf_layer layer, uint16_t length, pbuf_type type);
/** Drop a reference, the buffer is released with the last one; returns 1 when it was released */
uint8_t pbuf_free(struct pbuf *p);
void pbuf_ref(struct pbuf *p);
/** Move the payload by -delta bytes (positive: add header room, negative: strip), 0 on success */
uint8_t pbuf_header(struct pbuf *p, int16_t delta);

#endif /* AIC_PBUF_H_ */
