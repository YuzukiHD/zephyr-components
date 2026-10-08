/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/* A TLS 1.3 client on a TCP socket with the client certificate of the ADB key */

#ifndef TLS_IO_H_
#define TLS_IO_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

struct tls_io {
	int fd;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	mbedtls_x509_crt cert;
	struct k_mutex lock;
	bool up;
};

/** Handshake as a client on the connected socket @p fd */
int tls_io_start(struct tls_io *t, int fd, int timeout_ms);
/** Read exactly @p len bytes; -ETIMEDOUT when @p timeout_ms (-1: none) passes without progress */
int tls_io_recv(struct tls_io *t, void *buf, size_t len, int timeout_ms);
int tls_io_send(struct tls_io *t, const void *buf, size_t len);
/** TLS exporter with the label "adb-label" (including its NUL), no context */
int tls_io_export(struct tls_io *t, uint8_t *out, size_t len);
void tls_io_free(struct tls_io *t);

#endif
