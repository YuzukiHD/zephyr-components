/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stdio.h>

/* Exercise the production HKDF helper without changing its visibility. */
#include "../../src/pairing.c"
#include "adb_priv.h"

static void save(const char *path, const void *data, size_t len)
{
	FILE *file = fopen(path, "wb");

	assert(file != NULL);
	assert(fwrite(data, 1, len, file) == len);
	assert(fclose(file) == 0);
}

static void test_auth(void)
{
	mbedtls_x509_crt cert;
	char public_key[1024], reloaded[1024];
	uint8_t token[20], signature[256];
	size_t public_len, reloaded_len, signature_len;

	assert(adb_auth_init("adbkey") == 0);
	assert(adb_auth_public_key(public_key, sizeof(public_key), &public_len) == 0);
	assert(public_len > 1 && public_key[public_len - 1] == '\0');
	save("adb.pub", public_key, public_len - 1);
	for (size_t i = 0; i < sizeof(token); i++) {
		token[i] = i;
	}
	assert(adb_auth_sign(token, sizeof(token), signature, &signature_len) == 0);
	assert(signature_len == sizeof(signature));
	save("token.bin", token, sizeof(token));
	save("signature.bin", signature, signature_len);
	mbedtls_x509_crt_init(&cert);
	assert(adb_auth_client_cert(&cert) == 0);
	save("certificate.der", cert.raw.p, cert.raw.len);
	mbedtls_x509_crt_free(&cert);
	adb_auth_free();
	assert(adb_auth_sign(token, sizeof(token), signature, &signature_len) == -ENOENT);
	assert(adb_auth_init("adbkey") == 0);
	assert(adb_auth_public_key(reloaded, sizeof(reloaded), &reloaded_len) == 0);
	assert(public_len == reloaded_len);
	assert(memcmp(public_key, reloaded, public_len) == 0);
	adb_auth_free();
	/* Existing malformed keys must fail rather than silently replacing the identity. */
	save("invalid-key", "invalid", 7);
	assert(adb_auth_init("invalid-key") == -EINVAL);
	adb_auth_free();
}

static void test_pairing_key(void)
{
	mbedtls_gcm_context gcm;
	uint8_t material[64], plain[64], encrypted[64], decrypted[64];
	uint8_t iv[12] = {0}, tag[16];

	for (size_t i = 0; i < sizeof(material); i++) {
		material[i] = i;
		plain[i] = 255 - i;
	}
	mbedtls_gcm_init(&gcm);
	assert(gcm_key(material, &gcm) == 0);
	assert(mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, sizeof(plain),
					iv, sizeof(iv), NULL, 0, plain, encrypted,
					sizeof(tag), tag) == 0);
	save("pairing-cipher.bin", encrypted, sizeof(encrypted));
	save("pairing-tag.bin", tag, sizeof(tag));
	assert(mbedtls_gcm_auth_decrypt(&gcm, sizeof(encrypted), iv, sizeof(iv), NULL, 0,
				      tag, sizeof(tag), encrypted, decrypted) == 0);
	assert(memcmp(plain, decrypted, sizeof(plain)) == 0);
	tag[0] ^= 1;
	assert(mbedtls_gcm_auth_decrypt(&gcm, sizeof(encrypted), iv, sizeof(iv), NULL, 0,
				      tag, sizeof(tag), encrypted, decrypted) != 0);
	mbedtls_gcm_free(&gcm);
}

int main(void)
{
	test_auth();
	test_pairing_key();
	puts("ADB key lifecycle, certificate and pairing crypto passed");
	return 0;
}
