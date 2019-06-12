/*
 *  OpenVPN -- An application to securely tunnel IP networks
 *             over a single TCP/UDP port, with support for SSL/TLS-based
 *             session authentication and key exchange,
 *             packet encryption, packet authentication, and
 *             packet compression.
 *
 *  Copyright (C) 2002-2019 OpenVPN Inc <sales@openvpn.net>
 *  Copyright (C) 2010-2019 Fox Crypto B.V. <openvpn@fox-it.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file Data Channel Cryptography wolfSSL-specific backend interface
 */

#ifndef CRYPTO_WOLFSSL_H_
#define CRYPTO_WOLFSSL_H_

#include <wolfssl/openssl/evp.h>
#include <wolfssl/openssl/hmac.h>

/** Generic cipher key type %context. */
typedef WOLFSSL_CIPHER cipher_kt_t;

/** Generic message digest key type %context. */
typedef WOLFSSL_Hasher md_kt_t;

/** Generic cipher %context. */
typedef WOLFSSL_EVP_CIPHER_CTX cipher_ctx_t;

/** Generic message digest %context. */
typedef WOLFSSL_EVP_MD_CTX md_ctx_t;

/** Generic HMAC %context. */
typedef WOLFSSL_HMAC_CTX hmac_ctx_t;

#define DES_KEY_LENGTH 8
#define MD4_DIGEST_LENGTH       16

#endif /* CRYPTO_WOLFSSL_H_ */
