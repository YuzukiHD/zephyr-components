/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ADB_PRIV_H_
#define ADB_PRIV_H_

#include <stddef.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>

/** Random bytes for mbed TLS (a pseudo random source until the SoC has a generator) */
int adb_rng(void *ctx, unsigned char *out, size_t len);

/** The key of the client */
mbedtls_pk_context *adb_auth_pk(void);

/**
 * A self-signed certificate of that key, parsed into @p chain (the certificate that adbd matches
 * against the key stored at pairing time).
 */
int adb_auth_client_cert(mbedtls_x509_crt *chain);

#endif
