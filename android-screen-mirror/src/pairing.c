/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The phone is the server of the pairing. Over TLS 1.3 (any certificate is accepted by both
 * sides) the two run SPAKE2 on edwards25519 with the QR password and the TLS exporter value as
 * the shared secret, then exchange their ADB public keys encrypted with AES-128-GCM under a key
 * derived from the SPAKE2 result.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <mbedtls/gcm.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/sha512.h>
#include <mirror_zephyr/adb.h>
#include <mirror_zephyr/pairing.h>
#include "ed25519_ops.h"
#include "tls_io.h"

#define PKT_SPAKE2	0
#define PKT_PEER_INFO	1
#define PEER_INFO_SIZE	8192
#define GCM_TAG		16
#define EXPORT_LEN	64

static const uint8_t M_POINT[32] = {
	0x5a, 0xda, 0x7e, 0x4b, 0xf6, 0xdd, 0xd9, 0xad, 0xb6, 0x62, 0x6d, 0x32, 0x13, 0x1c, 0x6b, 0x5c,
	0x51, 0xa1, 0xe3, 0x47, 0xa3, 0x47, 0x8f, 0x53, 0xcf, 0xcf, 0x44, 0x1b, 0x88, 0xee, 0xd1, 0x2e};
static const uint8_t N_POINT[32] = {
	0x10, 0xe3, 0xdf, 0x0a, 0xe3, 0x7d, 0x8e, 0x7a, 0x99, 0xb5, 0xfe, 0x74, 0xb4, 0x46, 0x72, 0x10,
	0x3d, 0xbd, 0xdc, 0xbd, 0x06, 0xaf, 0x68, 0x0d, 0x71, 0x32, 0x9a, 0x11, 0x69, 0x3b, 0xc7, 0x78};

static const char client_name[16] = "adb pair client";	/* with the NUL */
static const char server_name[16] = "adb pair server";

static int send_pkt(struct tls_io *t, uint8_t type, const void *data, size_t len)
{
	uint8_t *b = malloc(6 + len);
	int ret;

	if (b == NULL) {
		return -ENOMEM;
	}
	b[0] = 1;
	b[1] = type;
	sys_put_be32(len, b + 2);
	memcpy(b + 6, data, len);
	ret = tls_io_send(t, b, 6 + len);
	free(b);

	return ret;
}

/* Reads a packet of the given type into buf (which holds cap bytes); returns its size or <0 */
static int recv_pkt(struct tls_io *t, uint8_t type, uint8_t *buf, size_t cap)
{
	uint8_t h[6];
	uint32_t len;
	int ret = tls_io_recv(t, h, sizeof(h), 15000);

	if (ret != 0) {
		return ret;
	}
	len = sys_get_be32(h + 2);
	if (h[0] != 1 || h[1] != type || len == 0U || len > cap) {
		return -EPROTO;
	}
	ret = tls_io_recv(t, buf, len, 15000);

	return ret != 0 ? ret : (int)len;
}

static void put_le64(uint8_t *p, uint64_t v)
{
	for (int i = 0; i < 8; i++) {
		p[i] = v >> (8 * i);
	}
}

static void sha_len_prefixed(mbedtls_sha512_context *c, const void *data, size_t len)
{
	uint8_t l[8];

	put_le64(l, len);
	mbedtls_sha512_update(c, l, sizeof(l));
	mbedtls_sha512_update(c, data, len);
}

/* SPAKE2 as the client (Alice) */
static int spake2_client(struct tls_io *t, const uint8_t *pw, size_t pw_len, uint8_t key[64])
{
	uint8_t pw_hash[64], w[32], x[32], rnd[64], my_msg[32], their_msg[32], k[32];
	ed_point m, n, tmp, q, p;
	mbedtls_sha512_context sha;
	int ret;

	mbedtls_sha512(pw, pw_len, pw_hash, 0);
	ed_scalar_reduce64(w, pw_hash);

	sys_rand_get(rnd, sizeof(rnd));
	ed_scalar_reduce64(x, rnd);
	/* x * 8: the private scalar is a multiple of the cofactor */
	for (int i = 31; i > 0; i--) {
		x[i] = (x[i] << 3) | (x[i - 1] >> 5);
	}
	x[0] <<= 3;

	if (ed_decode(m, M_POINT) != 0 || ed_decode(n, N_POINT) != 0) {
		return -EINVAL;
	}
	ed_base_mult(p, x);
	ed_scalarmult(tmp, m, w);
	ed_add(p, tmp);
	ed_encode(my_msg, p);

	ret = send_pkt(t, PKT_SPAKE2, my_msg, sizeof(my_msg));
	if (ret != 0) {
		return ret;
	}
	ret = recv_pkt(t, PKT_SPAKE2, their_msg, sizeof(their_msg));
	if (ret != (int)sizeof(their_msg)) {
		return ret < 0 ? ret : -EPROTO;
	}
	if (ed_decode(q, their_msg) != 0) {
		return -EPROTO;
	}
	/* remove the mask of the server: Q = S - w * N */
	ed_scalarmult(tmp, n, w);
	ed_neg(tmp);
	ed_add(q, tmp);
	ed_scalarmult(p, q, x);
	ed_encode(k, p);

	mbedtls_sha512_init(&sha);
	mbedtls_sha512_starts(&sha, 0);
	sha_len_prefixed(&sha, client_name, sizeof(client_name));
	sha_len_prefixed(&sha, server_name, sizeof(server_name));
	sha_len_prefixed(&sha, my_msg, sizeof(my_msg));
	sha_len_prefixed(&sha, their_msg, sizeof(their_msg));
	sha_len_prefixed(&sha, k, sizeof(k));
	sha_len_prefixed(&sha, pw_hash, sizeof(pw_hash));
	mbedtls_sha512_finish(&sha, key);
	mbedtls_sha512_free(&sha);

	return 0;
}

static int gcm_key(const uint8_t key_material[64], mbedtls_gcm_context *g)
{
	static const uint8_t info[] = "adb pairing_auth aes-128-gcm key";	/* 32 bytes, no NUL */
	uint8_t okm[16];
	int ret;

	ret = mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), NULL, 0, key_material, 64,
			   info, sizeof(info) - 1U, okm, sizeof(okm));
	if (ret == 0) {
		ret = mbedtls_gcm_setkey(g, MBEDTLS_CIPHER_ID_AES, okm, 128);
	}

	return ret == 0 ? 0 : -EIO;
}

int adb_pair(const char *host, uint16_t port, const char *password, const char *key_path,
	     char *guid, size_t guid_size)
{
	struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(port)};
	uint8_t exported[EXPORT_LEN], key[64], iv[12] = {0}, tag[GCM_TAG];
	uint8_t *pw = NULL, *plain = NULL, *wire = NULL;
	struct tls_io *t = NULL;
	mbedtls_gcm_context gcm;
	size_t pwlen = strlen(password), publen = 0;
	char *pub = NULL;
	int fd = -1, ret;

	mbedtls_gcm_init(&gcm);
	ret = adb_auth_init(key_path);
	if (ret != 0) {
		return ret;
	}
	if (zsock_inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		return -EINVAL;
	}
	t = calloc(1, sizeof(*t));
	pw = malloc(pwlen + EXPORT_LEN);
	plain = calloc(1, PEER_INFO_SIZE);
	wire = malloc(PEER_INFO_SIZE + GCM_TAG);
	pub = malloc(1024);
	if (t == NULL || pw == NULL || plain == NULL || wire == NULL || pub == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0 || zsock_connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		ret = -errno;
		goto out;
	}
	ret = tls_io_start(t, fd, 15000);
	if (ret != 0) {
		goto out;
	}
	ret = tls_io_export(t, exported, sizeof(exported));
	if (ret != 0) {
		printk("pairing: no TLS exporter value\n");
		goto out;
	}
	memcpy(pw, password, pwlen);
	memcpy(pw + pwlen, exported, EXPORT_LEN);

	ret = spake2_client(t, pw, pwlen + EXPORT_LEN, key);
	if (ret != 0) {
		printk("pairing: SPAKE2 failed: %d\n", ret);
		goto out;
	}
	ret = gcm_key(key, &gcm);
	if (ret != 0) {
		goto out;
	}

	/* our peer info: the type byte and the public key text, zero padded */
	ret = adb_auth_public_key(pub, 1024, &publen);
	if (ret != 0 || publen > PEER_INFO_SIZE) {
		ret = -EINVAL;
		goto out;
	}
	plain[0] = 0;
	memcpy(plain + 1, pub, publen - 1U);
	ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, PEER_INFO_SIZE, iv, sizeof(iv),
					NULL, 0, plain, wire, GCM_TAG, tag);
	if (ret != 0) {
		ret = -EIO;
		goto out;
	}
	memcpy(wire + PEER_INFO_SIZE, tag, GCM_TAG);
	ret = send_pkt(t, PKT_PEER_INFO, wire, PEER_INFO_SIZE + GCM_TAG);
	if (ret != 0) {
		goto out;
	}

	ret = recv_pkt(t, PKT_PEER_INFO, wire, PEER_INFO_SIZE + GCM_TAG);
	if (ret != PEER_INFO_SIZE + GCM_TAG) {
		/* the phone drops the connection when the password was wrong */
		printk("pairing: no answer from the phone (%d), wrong code?\n", ret);
		ret = ret < 0 ? ret : -EPROTO;
		goto out;
	}
	ret = mbedtls_gcm_auth_decrypt(&gcm, PEER_INFO_SIZE, iv, sizeof(iv), NULL, 0,
				       wire + PEER_INFO_SIZE, GCM_TAG, wire, plain);
	if (ret != 0) {
		printk("pairing: the answer of the phone does not authenticate\n");
		ret = -EACCES;
		goto out;
	}
	plain[PEER_INFO_SIZE - 1] = '\0';
	strncpy(guid, (const char *)plain + 1, guid_size - 1U);
	guid[guid_size - 1U] = '\0';
	ret = 0;
out:
	if (t != NULL && t->up) {
		tls_io_free(t);
	}
	if (fd >= 0) {
		zsock_close(fd);
	}
	mbedtls_gcm_free(&gcm);
	free(t);
	free(pw);
	free(plain);
	free(wire);
	free(pub);

	return ret;
}
