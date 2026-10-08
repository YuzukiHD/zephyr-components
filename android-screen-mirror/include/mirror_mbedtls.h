/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/* Features of mbed TLS that the key handling of the ADB client needs on top of the Zephyr set */
#ifndef MIRROR_MBEDTLS_H_
#define MIRROR_MBEDTLS_H_

#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_PEM_WRITE_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PK_C
#define MBEDTLS_X509_CREATE_C
#define MBEDTLS_X509_CRT_WRITE_C
#define MBEDTLS_SSL_KEYING_MATERIAL_EXPORT
#define MBEDTLS_X509_RSASSA_PSS_SUPPORT

#endif
