/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/printk.h>
#include <mbedtls/debug.h>
#include "adb_priv.h"
#include "tls_io.h"

#if defined(CONFIG_MBEDTLS_DEBUG)
extern void zephyr_mbedtls_debug(void *ctx, int level, const char *file, int line, const char *str);
#endif

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
	ssize_t n = zsock_send(*(int *)ctx, buf, len, ZSOCK_MSG_DONTWAIT);

	if (n < 0) {
		return (errno == EAGAIN || errno == EWOULDBLOCK) ? MBEDTLS_ERR_SSL_WANT_WRITE
								  : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
	}

	return n;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
	ssize_t n = zsock_recv(*(int *)ctx, buf, len, ZSOCK_MSG_DONTWAIT);

	if (n == 0) {
		return MBEDTLS_ERR_SSL_CONN_EOF;
	}
	if (n < 0) {
		return (errno == EAGAIN || errno == EWOULDBLOCK) ? MBEDTLS_ERR_SSL_WANT_READ
								  : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
	}

	return n;
}

/* Waits for the socket; 0 when it is ready (or data is buffered inside the TLS context) */
static int wait_fd(struct tls_io *t, short events, int timeout_ms)
{
	struct zsock_pollfd pfd = {.fd = t->fd, .events = events};
	int r;

	if ((events & ZSOCK_POLLIN) && mbedtls_ssl_get_bytes_avail(&t->ssl) > 0U) {
		return 0;
	}
	r = zsock_poll(&pfd, 1, timeout_ms);
	if (r == 0) {
		return -ETIMEDOUT;
	}

	return r < 0 ? -errno : 0;
}

int tls_io_start(struct tls_io *t, int fd, int timeout_ms)
{
	static const int suites[] = {MBEDTLS_TLS1_3_AES_128_GCM_SHA256, 0};
	static const uint16_t sigalgs[] = {
		MBEDTLS_TLS1_3_SIG_RSA_PSS_RSAE_SHA256,
		MBEDTLS_TLS1_3_SIG_RSA_PSS_RSAE_SHA384,
		MBEDTLS_TLS1_3_SIG_RSA_PSS_RSAE_SHA512,
		MBEDTLS_TLS1_3_SIG_NONE,
	};
	int64_t end = k_uptime_get() + timeout_ms;
	int ret;

	memset(t, 0, sizeof(*t));
	t->fd = fd;
	k_mutex_init(&t->lock);
	mbedtls_ssl_init(&t->ssl);
	mbedtls_ssl_config_init(&t->conf);
	mbedtls_x509_crt_init(&t->cert);

	ret = adb_auth_client_cert(&t->cert);
	if (ret != 0) {
		goto fail;
	}
	ret = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT,
					  MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
	if (ret != 0) {
		goto fail_tls;
	}
#if defined(CONFIG_MBEDTLS_DEBUG)
	mbedtls_debug_set_threshold(CONFIG_MBEDTLS_DEBUG_LEVEL);
	mbedtls_ssl_conf_dbg(&t->conf, zephyr_mbedtls_debug, NULL);
#endif
	mbedtls_ssl_conf_min_tls_version(&t->conf, MBEDTLS_SSL_VERSION_TLS1_3);
	mbedtls_ssl_conf_max_tls_version(&t->conf, MBEDTLS_SSL_VERSION_TLS1_3);
	mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_NONE);
	mbedtls_ssl_conf_rng(&t->conf, adb_rng, NULL);
	mbedtls_ssl_conf_ciphersuites(&t->conf, suites);
	mbedtls_ssl_conf_sig_algs(&t->conf, sigalgs);
	mbedtls_ssl_conf_tls13_key_exchange_modes(&t->conf,
						  MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL);
	ret = mbedtls_ssl_conf_own_cert(&t->conf, &t->cert, adb_auth_pk());
	if (ret == 0) {
		ret = mbedtls_ssl_setup(&t->ssl, &t->conf);
	}
	if (ret != 0) {
		goto fail_tls;
	}
	mbedtls_ssl_set_bio(&t->ssl, &t->fd, bio_send, bio_recv, NULL);

	while ((ret = mbedtls_ssl_handshake(&t->ssl)) != 0) {
		int left = (int)(end - k_uptime_get());

		if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
			goto fail_tls;
		}
		if (left <= 0 ||
		    wait_fd(t, ret == MBEDTLS_ERR_SSL_WANT_READ ? ZSOCK_POLLIN : ZSOCK_POLLOUT,
			    left) != 0) {
			ret = -ETIMEDOUT;
			goto fail;
		}
	}
	t->up = true;

	return 0;
fail_tls:
	printk("tls: handshake failed: -0x%x\n", -ret);
	ret = -EPROTO;
fail:
	tls_io_free(t);

	return ret;
}

int tls_io_recv(struct tls_io *t, void *buf, size_t len, int timeout_ms)
{
	uint8_t *p = buf;

	while (len > 0U) {
		int n;

		k_mutex_lock(&t->lock, K_FOREVER);
		n = mbedtls_ssl_read(&t->ssl, p, len);
		k_mutex_unlock(&t->lock);
		if (n > 0) {
			p += n;
			len -= n;
			continue;
		}
		if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ||
		    n == MBEDTLS_ERR_SSL_CONN_EOF) {
			return -ECONNRESET;
		}
		if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE ||
		    n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
			int r = wait_fd(t, n == MBEDTLS_ERR_SSL_WANT_WRITE ? ZSOCK_POLLOUT
									   : ZSOCK_POLLIN,
					timeout_ms);

			if (r != 0) {
				return r;
			}
			continue;
		}
		printk("tls: read failed: -0x%x\n", -n);

		return -EIO;
	}

	return 0;
}

int tls_io_send(struct tls_io *t, const void *buf, size_t len)
{
	const uint8_t *p = buf;

	while (len > 0U) {
		int n;

		k_mutex_lock(&t->lock, K_FOREVER);
		n = mbedtls_ssl_write(&t->ssl, p, len);
		k_mutex_unlock(&t->lock);
		if (n > 0) {
			p += n;
			len -= n;
			continue;
		}
		if (n == MBEDTLS_ERR_SSL_WANT_WRITE || n == MBEDTLS_ERR_SSL_WANT_READ) {
			int r = wait_fd(t, n == MBEDTLS_ERR_SSL_WANT_WRITE ? ZSOCK_POLLOUT
									   : ZSOCK_POLLIN,
					8000);

			if (r != 0) {
				printk("tls: send waited for %s: %d\n",
				       n == MBEDTLS_ERR_SSL_WANT_WRITE ? "write" : "read", r);
				return r;
			}
			continue;
		}
		printk("tls: write failed: -0x%x\n", -n);

		return -EIO;
	}

	return 0;
}

int tls_io_export(struct tls_io *t, uint8_t *out, size_t len)
{
	/* the label includes its terminating NUL */
	int ret = mbedtls_ssl_export_keying_material(&t->ssl, out, len, "adb-label", 10, NULL, 0, 0);

	return ret == 0 ? 0 : -EIO;
}

void tls_io_free(struct tls_io *t)
{
	if (t->up) {
		mbedtls_ssl_close_notify(&t->ssl);
	}
	mbedtls_ssl_free(&t->ssl);
	mbedtls_ssl_config_free(&t->conf);
	mbedtls_x509_crt_free(&t->cert);
	t->up = false;
}
