# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0

import base64
import hashlib
import hmac
import struct
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa, utils
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

key = serialization.load_pem_private_key(Path("adbkey").read_bytes(), password=None)
numbers = key.public_key().public_numbers()
blob = base64.b64decode(Path("adb.pub").read_text().split()[0], validate=True)
assert len(blob) == 524
words, n0inv = struct.unpack_from("<II", blob)
modulus = int.from_bytes(blob[8:264], "little")
rr = int.from_bytes(blob[264:520], "little")
exponent = struct.unpack_from("<I", blob, 520)[0]
assert words == 64 and modulus.bit_length() == 2048
assert (modulus, exponent) == (numbers.n, numbers.e)
assert (modulus * n0inv) % (1 << 32) == (1 << 32) - 1
assert rr == pow(2, 4096, modulus)
public = rsa.RSAPublicNumbers(exponent, modulus).public_key()
public.verify(Path("signature.bin").read_bytes(), Path("token.bin").read_bytes(),
              padding.PKCS1v15(), utils.Prehashed(hashes.SHA1()))
cert = x509.load_der_x509_certificate(Path("certificate.der").read_bytes())
assert cert.public_key().public_numbers() == numbers
public.verify(cert.signature, cert.tbs_certificate_bytes,
              padding.PKCS1v15(), cert.signature_hash_algorithm)
assert cert.subject == cert.issuer

# Independent RFC 5869 extract/expand and OpenSSL-backed AES-GCM.
prk = hmac.digest(bytes(32), bytes(range(64)), hashlib.sha256)
info = b"adb pairing_auth aes-128-gcm key"
okm = hmac.digest(prk, info + b"\x01", hashlib.sha256)[:16]
expected = AESGCM(okm).encrypt(bytes(12), bytes(255 - i for i in range(64)), None)
actual = Path("pairing-cipher.bin").read_bytes() + Path("pairing-tag.bin").read_bytes()
assert actual == expected
print("Independent RSA, Montgomery constants, certificate and HKDF/GCM checks passed")
