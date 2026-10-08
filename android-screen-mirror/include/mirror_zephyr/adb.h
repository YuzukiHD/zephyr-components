/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * ADB client over TCP: the connection with the RSA authentication, the stream multiplexer
 * (shell, sync, local sockets) and the file push of the sync service.
 *
 * One thread receives the packets of the connection; the data of a stream is queued for
 * adb_read() or, when the stream has a sink, handed to it in the context of that thread.
 */

#ifndef MIRROR_ZEPHYR_ADB_H_
#define MIRROR_ZEPHYR_ADB_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

struct adb_conn;
struct adb_stream;

/** Receives the data of a stream instead of the queue; runs in the receive thread */
typedef void (*adb_sink_t)(const uint8_t *data, size_t len, void *arg);

/**
 * @brief Connect to adbd and authenticate
 *
 * Signs the challenge with the key; when the phone does not know the key yet it is offered
 * and the call waits (up to @p accept_timeout_ms) for the user to accept it on the phone.
 *
 * @param key_path PEM file of the RSA key; it is created when it does not exist
 * @return 0 or a negative errno
 */
int adb_connect(struct adb_conn **conn, const char *host, uint16_t port, const char *key_path,
		int accept_timeout_ms);

/**
 * @brief Connect to the TLS service of wireless debugging (after adb_pair())
 *
 * @return 0, -EACCES when the phone does not know the key, or another negative errno
 */
int adb_connect_tls(struct adb_conn **conn, const char *host, uint16_t port, const char *key_path);

/** Close the connection and every stream */
void adb_disconnect(struct adb_conn *conn);

/** @brief Open a service ("shell:...", "sync:", "localabstract:...") */
int adb_open(struct adb_conn *conn, const char *service, adb_sink_t sink, void *sink_arg,
	     struct adb_stream **stream);

/** @brief Close a stream */
void adb_close(struct adb_stream *stream);

/** @brief Send data and wait until the peer took it */
int adb_write(struct adb_stream *stream, const void *data, size_t len);

/**
 * @brief Read exactly @p len bytes
 * @retval 0 done
 * @retval -ETIMEDOUT not enough data in time
 * @retval -ECONNRESET the stream is closed
 */
int adb_read(struct adb_stream *stream, void *buf, size_t len, k_timeout_t timeout);

/** @brief Bytes queued and not read yet */
size_t adb_available(struct adb_stream *stream);

/** @brief The stream or its connection is gone */
bool adb_is_closed(struct adb_stream *stream);

/**
 * @brief Copy a file of the local file system to the phone with the sync service
 *
 * Nothing is sent when the remote file already has the size of the local one.
 *
 * @param pushed set to true when the file was transferred
 */
int adb_push_file(struct adb_conn *conn, const char *local, const char *remote, unsigned int mode,
		  bool *pushed);

/* ---- adb_auth.c ----------------------------------------------------------------------- */

/** Load the key file, or create it (RSA 2048) and write it */
int adb_auth_init(const char *path);
/** Sign the 20 byte token of an AUTH message; @p sig takes 256 bytes */
int adb_auth_sign(const uint8_t *token, size_t token_len, uint8_t *sig, size_t *sig_len);
/** The public key in the text form the phone's adbd accepts (NUL terminated) */
int adb_auth_public_key(char *out, size_t size, size_t *len);
void adb_auth_free(void);

#ifdef __cplusplus
}
#endif

#endif
