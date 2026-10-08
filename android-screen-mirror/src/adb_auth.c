/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * RSA key of the ADB client. adbd checks a signature of its 20 byte challenge made with
 * PKCS#1 v1.5 and the SHA-1 digest prefix (the challenge is used as the digest), and shows the
 * public key to the user once; the public key travels in a fixed layout of 32 bit words (the
 * modulus and R^2 mod n for a Montgomery multiplication) encoded as base64.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>
#include <mbedtls/base64.h>
#include <mbedtls/bignum.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/x509_crt.h>
#include <mirror_zephyr/adb.h>
#include "adb_priv.h"

#define KEY_BITS	2048
#define KEY_BYTES	(KEY_BITS / 8)
#define KEY_WORDS	(KEY_BYTES / 4)
#define PEM_MAX		4096

static mbedtls_pk_context pk;
static bool have_key;

/* The randomness is only needed for the blinding of signatures and for a new key */
int adb_rng(void *ctx, unsigned char *out, size_t len)
{
	ARG_UNUSED(ctx);
	sys_rand_get(out, len);

	return 0;
}

#define rng adb_rng

static int read_file(const char *path, uint8_t *buf, size_t size, size_t *len)
{
	struct fs_file_t f;
	ssize_t n;
	int ret;

	fs_file_t_init(&f);
	ret = fs_open(&f, path, FS_O_READ);
	if (ret != 0) {
		return ret;
	}
	n = fs_read(&f, buf, size - 1U);
	fs_close(&f);
	if (n < 0) {
		return (int)n;
	}
	buf[n] = '\0';
	*len = n;

	return 0;
}

static int write_file(const char *path, const uint8_t *buf, size_t len)
{
	struct fs_file_t f;
	ssize_t n;
	int ret;

	fs_file_t_init(&f);
	ret = fs_open(&f, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0) {
		return ret;
	}
	n = fs_write(&f, buf, len);
	fs_close(&f);

	return n == (ssize_t)len ? 0 : (n < 0 ? (int)n : -EIO);
}

int adb_auth_init(const char *path)
{
	uint8_t *pem;
	size_t len = 0;
	int ret;

	if (have_key) {
		return 0;
	}
	pem = malloc(PEM_MAX);
	if (pem == NULL) {
		return -ENOMEM;
	}
	mbedtls_pk_init(&pk);

	ret = read_file(path, pem, PEM_MAX, &len);
	if (ret == 0) {
		/* a key of the PC (~/.android/adbkey) is fine, the phone knows it already */
		ret = mbedtls_pk_parse_key(&pk, pem, len + 1U, NULL, 0, rng, NULL);
		if (ret != 0) {
			printk("adb: cannot parse %s: -0x%x\n", path, -ret);
			ret = -EINVAL;
		}
	} else {
		printk("adb: no key %s, generating a %d bit RSA key (takes a while)\n", path,
		       KEY_BITS);
		ret = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
		if (ret == 0) {
			ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(pk), rng, NULL, KEY_BITS, 65537);
		}
		if (ret == 0) {
			ret = mbedtls_pk_write_key_pem(&pk, pem, PEM_MAX);
		}
		if (ret == 0) {
			ret = write_file(path, pem, strlen((char *)pem));
			if (ret != 0) {
				printk("adb: cannot write %s: %d (the key is not kept)\n", path,
				       ret);
				ret = 0;
			}
		} else {
			printk("adb: key generation failed: -0x%x\n", -ret);
			ret = -EIO;
		}
	}
	free(pem);
	if (ret != 0) {
		mbedtls_pk_free(&pk);
		return ret;
	}
	have_key = true;

	return 0;
}

void adb_auth_free(void)
{
	if (have_key) {
		mbedtls_pk_free(&pk);
		have_key = false;
	}
}

int adb_auth_sign(const uint8_t *token, size_t token_len, uint8_t *sig, size_t *sig_len)
{
	int ret;

	if (!have_key) {
		return -ENOENT;
	}
	ret = mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA1, token, token_len, sig, KEY_BYTES, sig_len, rng,
			      NULL);

	return ret == 0 ? 0 : -EIO;
}

static void put_le32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

/* big endian bytes of a number to little endian 32 bit words */
static void be_to_words(const uint8_t *be, uint8_t *out)
{
	for (int i = 0; i < KEY_WORDS; i++) {
		const uint8_t *w = be + KEY_BYTES - 4 - 4 * i;

		put_le32(out + 4 * i, ((uint32_t)w[0] << 24) | (w[1] << 16) | (w[2] << 8) | w[3]);
	}
}

int adb_auth_public_key(char *out, size_t size, size_t *len)
{
	static const char suffix[] = " mirror@f101";
	mbedtls_mpi n, e, rr;
	uint8_t bin[KEY_BYTES];
	uint8_t *blob;
	uint32_t n0, inv, pub_e;
	size_t olen = 0;
	int ret;

	if (!have_key) {
		return -ENOENT;
	}
	blob = malloc(4 + 4 + 2 * KEY_BYTES + 4);
	if (blob == NULL) {
		return -ENOMEM;
	}
	mbedtls_mpi_init(&n);
	mbedtls_mpi_init(&e);
	mbedtls_mpi_init(&rr);

	ret = mbedtls_rsa_export(mbedtls_pk_rsa(pk), &n, NULL, NULL, NULL, &e);
	if (ret != 0 || mbedtls_mpi_size(&n) != KEY_BYTES) {
		ret = -EINVAL;
		goto out;
	}
	put_le32(blob, KEY_WORDS);

	ret = mbedtls_mpi_write_binary(&n, bin, KEY_BYTES);
	if (ret != 0) {
		goto out;
	}
	/* -1/n mod 2^32 by Newton iteration on the lowest word */
	n0 = ((uint32_t)bin[KEY_BYTES - 4] << 24) | (bin[KEY_BYTES - 3] << 16) |
	     (bin[KEY_BYTES - 2] << 8) | bin[KEY_BYTES - 1];
	inv = n0;
	for (int i = 0; i < 5; i++) {
		inv *= 2U - n0 * inv;
	}
	put_le32(blob + 4, -inv);
	be_to_words(bin, blob + 8);

	/* R^2 mod n with R = 2^2048 */
	ret = mbedtls_mpi_lset(&rr, 1);
	if (ret == 0) {
		ret = mbedtls_mpi_shift_l(&rr, 2 * KEY_BITS);
	}
	if (ret == 0) {
		ret = mbedtls_mpi_mod_mpi(&rr, &rr, &n);
	}
	if (ret == 0) {
		ret = mbedtls_mpi_write_binary(&rr, bin, KEY_BYTES);
	}
	if (ret != 0) {
		goto out;
	}
	be_to_words(bin, blob + 8 + KEY_BYTES);

	{
		uint8_t eb[4] = {0};
		size_t es = mbedtls_mpi_size(&e);

		if (es > 4U || mbedtls_mpi_write_binary(&e, eb + 4 - es, es) != 0) {
			ret = -EINVAL;
			goto out;
		}
		pub_e = ((uint32_t)eb[0] << 24) | (eb[1] << 16) | (eb[2] << 8) | eb[3];
	}
	put_le32(blob + 8 + 2 * KEY_BYTES, pub_e);

	ret = mbedtls_base64_encode((unsigned char *)out, size - sizeof(suffix), &olen, blob,
				    8 + 2 * KEY_BYTES + 4);
	if (ret != 0) {
		ret = -ENOSPC;
		goto out;
	}
	memcpy(out + olen, suffix, sizeof(suffix));
	*len = olen + sizeof(suffix);
	ret = 0;
out:
	mbedtls_mpi_free(&n);
	mbedtls_mpi_free(&e);
	mbedtls_mpi_free(&rr);
	free(blob);

	return ret;
}

mbedtls_pk_context *adb_auth_pk(void)
{
	return have_key ? &pk : NULL;
}

int adb_auth_client_cert(mbedtls_x509_crt *chain)
{
	mbedtls_x509write_cert w;
	static const unsigned char serial[] = {1};
	unsigned char *der;
	int ret;

	if (!have_key) {
		return -ENOENT;
	}
	der = malloc(2048);
	if (der == NULL) {
		return -ENOMEM;
	}
	mbedtls_x509write_crt_init(&w);
	mbedtls_x509write_crt_set_md_alg(&w, MBEDTLS_MD_SHA256);
	mbedtls_x509write_crt_set_subject_key(&w, &pk);
	mbedtls_x509write_crt_set_issuer_key(&w, &pk);
	ret = mbedtls_x509write_crt_set_subject_name(&w, "C=US,O=Android,CN=Adb");
	if (ret == 0) {
		ret = mbedtls_x509write_crt_set_issuer_name(&w, "C=US,O=Android,CN=Adb");
	}
	if (ret == 0) {
		ret = mbedtls_x509write_crt_set_serial_raw(&w, (unsigned char *)serial,
							   sizeof(serial));
	}
	if (ret == 0) {
		/* adbd does not look at the dates */
		ret = mbedtls_x509write_crt_set_validity(&w, "20240101000000", "20340101000000");
	}
	if (ret == 0) {
		ret = mbedtls_x509write_crt_set_basic_constraints(&w, 1, -1);
	}
	if (ret == 0) {
		ret = mbedtls_x509write_crt_der(&w, der, 2048, rng, NULL);
	}
	if (ret > 0) {
		/* the certificate is at the end of the buffer */
		ret = mbedtls_x509_crt_parse_der(chain, der + 2048 - ret, ret);
	} else if (ret == 0) {
		ret = -EIO;
	}
	mbedtls_x509write_crt_free(&w);
	free(der);
	if (ret != 0) {
		printk("adb: client certificate failed: -0x%x\n", -ret);
	}

	return ret == 0 ? 0 : -EIO;
}
