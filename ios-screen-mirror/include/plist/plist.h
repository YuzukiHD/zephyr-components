/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/* The part of the property list API that the AirPlay library uses: binary plists only */

#ifndef PLIST_PLIST_H_
#define PLIST_PLIST_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct plist_node *plist_t;

typedef enum {
	PLIST_BOOLEAN,
	PLIST_UINT,
	PLIST_REAL,
	PLIST_STRING,
	PLIST_ARRAY,
	PLIST_DICT,
	PLIST_DATE,
	PLIST_DATA,
	PLIST_KEY,
	PLIST_UID,
	PLIST_NONE,
} plist_type;

plist_type plist_get_node_type(plist_t node);
#define PLIST_IS_ARRAY(n) ((n) != NULL && plist_get_node_type(n) == PLIST_ARRAY)
#define PLIST_IS_DATA(n) ((n) != NULL && plist_get_node_type(n) == PLIST_DATA)

plist_t plist_new_dict(void);
plist_t plist_new_array(void);
plist_t plist_new_string(const char *val);
plist_t plist_new_bool(uint8_t val);
plist_t plist_new_uint(uint64_t val);
plist_t plist_new_real(double val);
plist_t plist_new_data(const char *val, uint64_t length);

/** The item belongs to the container afterwards */
void plist_dict_set_item(plist_t node, const char *key, plist_t item);
plist_t plist_dict_get_item(plist_t node, const char *key);
void plist_array_append_item(plist_t node, plist_t item);
plist_t plist_array_get_item(plist_t node, uint32_t n);
uint32_t plist_array_get_size(plist_t node);

void plist_get_uint_val(plist_t node, uint64_t *val);
/** Allocates *val, the caller frees it */
void plist_get_data_val(plist_t node, char **val, uint64_t *length);

void plist_from_bin(const char *plist_bin, uint32_t length, plist_t *plist);
/** Allocates *plist_bin, the caller frees it */
void plist_to_bin(plist_t plist, char **plist_bin, uint32_t *length);

void plist_free(plist_t plist);

#ifdef __cplusplus
}
#endif

#endif
