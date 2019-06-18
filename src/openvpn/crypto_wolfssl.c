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

#ifdef HAVE_CONFIG_H
#include "config.h"
#elif defined(_MSC_VER)
#include "config-msvc.h"
#endif

#include "syshead.h"

#if defined(ENABLE_CRYPTO_WOLFSSL)

#include "basic.h"
#include "buffer.h"
#include "integer.h"
#include "crypto.h"
#include "crypto_backend.h"

void crypto_init_lib(void) {
	int ret;

    if ((ret = wolfCrypt_Init()) != 0) {
        msg(D_CRYPT_ERRORS, "wolfCrypt_Init failed");
        printf("wolfCrypt_Init failed %d\n", ret);
    }
}

void crypto_uninit_lib(void) {
	int ret;
    if ((ret = wolfCrypt_Cleanup()) != 0) {
        msg(D_CRYPT_ERRORS, "wolfCrypt_Cleanup failed");
        printf("wolfCrypt_Cleanup failed %d\n", ret);
    }
}

void crypto_clear_error(void) {}

void crypto_init_lib_engine(const char *engine_name) {
    msg(M_INFO, "Note: wolfSSL does not have an engine");
}

void show_available_ciphers(void) {
    static char ciphers[4096];
    char* x;

    int ret = wolfSSL_get_ciphers(ciphers, (int)sizeof(ciphers));

    for (x = ciphers; *x != '\0'; x++) {
    	if (*x == ':')
    		*x = '\n';
    }

    if (ret == WOLFSSL_SUCCESS)
        printf("%s\n", ciphers);
}

void show_available_digests(void) {
	#ifdef WOLFSSL_MD2
        printf("MD2 128 bit digest size\n")
    #endif
    #ifndef NO_MD4
		printf("MD4 128 bit digest size\n");
    #endif
    #ifndef NO_MD5
		printf("MD5 128 bit digest size\n");
    #endif
    #ifndef NO_SHA
		printf("SHA1 160 bit digest size\n");
    #endif
    #ifndef NO_SHA256
		printf("SHA256 256 bit digest size\n");
    #endif
    #ifdef WOLFSSL_SHA384
		printf("SHA384 384 bit digest size\n");
    #endif
    #ifdef WOLFSSL_SHA512
		printf("SHA512 512 bit digest size\n");
    #endif
}

void show_available_engines(void) {
    msg(M_INFO, "Note: wolfSSL does not have an engine");
}


#define PEM_BEGIN              "-----BEGIN "
#define PEM_BEGIN_LEN          11
#define PEM_LINE_END           "-----\n"
#define PEM_LINE_END_LEN       6
#define PEM_END                "-----END "
#define PEM_END_LEN            9

uint32_t der_to_pem_len(uint32_t der_len) {
	uint32_t pem_len;
    pem_len = (der_len + 2) / 3 * 4;
    pem_len += (pem_len + 63) / 64;
    return pem_len;
}

bool crypto_pem_encode(const char *name, struct buffer *dst,
                       const struct buffer *src, struct gc_arena *gc) {
	uint8_t* pem_buf;
	uint32_t pem_len = der_to_pem_len(BLEN(src));
	uint8_t* out_buf;
	uint8_t* out_buf_ptr;
    bool ret = false;
	int err;
	int name_len = strlen(name);
	int out_len = PEM_BEGIN_LEN + PEM_LINE_END_LEN + name_len + pem_len +
  	  	    	  PEM_END_LEN + PEM_LINE_END_LEN + name_len;

	if (!(pem_buf = (uint8_t*) malloc(pem_len))) {
		return false;
	}

	if (!(out_buf = (uint8_t*) malloc(out_len))) {
        goto out_buf_err;
	}

	if ((err = Base64_Encode(BPTR(src), BLEN(src), pem_buf, &pem_len)) != 0) {
	    msg(M_INFO, "Base64_Encode failed with Errno: %d", err);
        goto Base64_Encode_err;
	}

	out_buf_ptr = out_buf;
	memcpy(out_buf_ptr, PEM_BEGIN, PEM_BEGIN_LEN);
	out_buf_ptr += PEM_BEGIN_LEN;
	memcpy(out_buf_ptr, name, name_len);
	out_buf_ptr += name_len;
	memcpy(out_buf_ptr, PEM_LINE_END, PEM_LINE_END_LEN);
	out_buf_ptr += PEM_LINE_END_LEN;
	memcpy(out_buf_ptr, pem_buf, pem_len);
	out_buf_ptr += pem_len;
	memcpy(out_buf_ptr, PEM_END, PEM_END_LEN);
	out_buf_ptr += PEM_END_LEN;
	memcpy(out_buf_ptr, name, name_len);
	out_buf_ptr += name_len;
	memcpy(out_buf_ptr, PEM_LINE_END, PEM_LINE_END_LEN);

    *dst = alloc_buf_gc(out_len + 1, gc);
    ASSERT(buf_write(dst, out_buf, out_len));
    buf_null_terminate(dst);

    ret = true;

Base64_Encode_err:
	free(out_buf);
out_buf_err:
	free(pem_buf);

	return ret;
}

uint32_t pem_to_der_len(uint32_t pem_len) {
	static int PEM_LINE_SZ = 64;
	int plainSz = pem_len - ((pem_len + (PEM_LINE_SZ - 1)) / PEM_LINE_SZ );
    return (plainSz * 3 + 3) / 4;
}

bool crypto_pem_decode(const char *name, struct buffer *dst,
                       const struct buffer *src) {
	int name_len = strlen(name);
	int err;
	uint8_t* src_buf;
    bool ret = false;
    unsigned int der_len = BLEN(src) - PEM_BEGIN_LEN - PEM_LINE_END_LEN -
    					   PEM_END_LEN - PEM_LINE_END_LEN -
    					   name_len - name_len - 1;
    unsigned int pem_len = pem_to_der_len(der_len);

	ASSERT(BLEN(src) > PEM_BEGIN_LEN + PEM_LINE_END_LEN + PEM_END_LEN + PEM_LINE_END_LEN);

	if (!(src_buf = (uint8_t*) malloc(BLEN(src)))) {
        msg(M_FATAL, "Cannot open memory BIO for PEM decode");
		return false;
	}
	memcpy(src_buf, BPTR(src), BLEN(src));

	src_buf[PEM_BEGIN_LEN + name_len] = '\0';

	if (strcmp((char*)(src_buf + PEM_BEGIN_LEN), name)) {
        msg(D_CRYPT_ERRORS,
		    "%s: unexpected PEM name (got '%s', expected '%s')",
            __func__, src_buf + PEM_BEGIN_LEN, name);
        goto cleanup;
	}

	if ((err = Base64_Decode(BPTR(src) + PEM_BEGIN_LEN + PEM_LINE_END_LEN + name_len,
							 der_len, src_buf, &pem_len)) != 0) {
	    msg(M_INFO, "Base64_Decode failed with Errno: %d", err);
        goto cleanup;
	}

    uint8_t *dst_data = buf_write_alloc(dst, pem_len);
    if (!dst_data)
    {
        msg(D_CRYPT_ERRORS, "%s: dst too small (%i, needs %i)", __func__,
            BCAP(dst), pem_len);
        goto cleanup;
    }

    memcpy(dst_data, src_buf, pem_len);

	ret = true;

cleanup:
	free(src_buf);
	return ret;
}

int rand_bytes(uint8_t *output, int len) {
	WC_RNG rng;
	int ret;

	if ((ret = wc_InitRng(&rng)) != 0){
	    msg(D_CRYPT_ERRORS, "wc_InitRng failed Errno: %d", ret);
	    return 0;
	}

	if ((ret = wc_RNG_GenerateBlock(&rng, output, len)) != 0){
	    msg(D_CRYPT_ERRORS, "wc_RNG_GenerateBlock failed Errno: %d", ret);
	    return 0;
	}

	if ((ret = wc_FreeRng(&rng)) != 0){
	    msg(D_CRYPT_ERRORS, "wc_FreeRng failed Errno: %d", ret);
	    return 0;
	}

	return 1;
}

int key_des_num_cblocks(const cipher_kt_t *kt) {
    int ret = 0;

    if (kt) {
        switch (*kt) {
        case OV_WC_DES_CBC_TYPE:
        case OV_WC_DES_ECB_TYPE:
        	ret = DES_KEY_SIZE/DES_BLOCK_SIZE;
        	break;
        case OV_WC_DES_EDE3_CBC_TYPE:
        case OV_WC_DES_EDE3_ECB_TYPE:
        	ret = DES3_KEY_SIZE/DES_BLOCK_SIZE;
        	break;
        default:
        	ret = 0;
        }
    }

    msg(D_CRYPTO_DEBUG, "CRYPTO INFO: n_DES_cblocks=%d", ret);
    return ret;
}

static const unsigned char odd_parity[256] = {
    1, 1, 2, 2, 4, 4, 7, 7, 8, 8, 11, 11, 13, 13, 14, 14,
    16, 16, 19, 19, 21, 21, 22, 22, 25, 25, 26, 26, 28, 28, 31, 31,
    32, 32, 35, 35, 37, 37, 38, 38, 41, 41, 42, 42, 44, 44, 47, 47,
    49, 49, 50, 50, 52, 52, 55, 55, 56, 56, 59, 59, 61, 61, 62, 62,
    64, 64, 67, 67, 69, 69, 70, 70, 73, 73, 74, 74, 76, 76, 79, 79,
    81, 81, 82, 82, 84, 84, 87, 87, 88, 88, 91, 91, 93, 93, 94, 94,
    97, 97, 98, 98, 100, 100, 103, 103, 104, 104, 107, 107, 109, 109, 110,
    110,
    112, 112, 115, 115, 117, 117, 118, 118, 121, 121, 122, 122, 124, 124, 127,
    127,
    128, 128, 131, 131, 133, 133, 134, 134, 137, 137, 138, 138, 140, 140, 143,
    143,
    145, 145, 146, 146, 148, 148, 151, 151, 152, 152, 155, 155, 157, 157, 158,
    158,
    161, 161, 162, 162, 164, 164, 167, 167, 168, 168, 171, 171, 173, 173, 174,
    174,
    176, 176, 179, 179, 181, 181, 182, 182, 185, 185, 186, 186, 188, 188, 191,
    191,
    193, 193, 194, 194, 196, 196, 199, 199, 200, 200, 203, 203, 205, 205, 206,
    206,
    208, 208, 211, 211, 213, 213, 214, 214, 217, 217, 218, 218, 220, 220, 223,
    223,
    224, 224, 227, 227, 229, 229, 230, 230, 233, 233, 234, 234, 236, 236, 239,
    239,
    241, 241, 242, 242, 244, 244, 247, 247, 248, 248, 251, 251, 253, 253, 254,
    254
};

static int DES_check_key_parity(const uint8_t *key)
{
    unsigned int i;

    for (i = 0; i < DES_BLOCK_SIZE; i++) {
        if (key[i] != odd_parity[key[i]])
            return 0;
    }
    return 1;
}


/* return true in fail case (1) */
static int DES_check(word32 mask, word32 mask2, uint8_t* key)
{
    word32 value[2];

    value[0] = mask;
    value[1] = mask2;
    return (memcmp(value, key, sizeof(value)) == 0)? 1: 0;
}

/* check is not weak. Weak key list from Nist "Recommendation for the Triple
 * Data Encryption Algorithm (TDEA) Block Cipher"
 *
 * returns 1 if is weak 0 if not
 */
static int wolfSSL_DES_is_weak_key(uint8_t* key) {
    word32 mask, mask2;

    mask = 0x01010101; mask2 = 0x01010101;
    if (DES_check(mask, mask2, key)) {
        return 1;
    }

    mask = 0xFEFEFEFE; mask2 = 0xFEFEFEFE;
    if (DES_check(mask, mask2, key)) {
        return 1;
    }

    mask = 0xE0E0E0E0; mask2 = 0xF1F1F1F1;
    if (DES_check(mask, mask2, key)) {
        return 1;
    }

    mask = 0x1F1F1F1F; mask2 = 0x0E0E0E0E;
    if (DES_check(mask, mask2, key)) {
        return 1;
    }

    /* semi-weak *key check (list from same Nist paper) */
    mask  = 0x011F011F; mask2 = 0x010E010E;
    if (DES_check(mask, mask2, key) ||
       DES_check(ByteReverseWord32(mask), ByteReverseWord32(mask2), key)) {
        return 1;
    }

    mask  = 0x01E001E0; mask2 = 0x01F101F1;
    if (DES_check(mask, mask2, key) ||
       DES_check(ByteReverseWord32(mask), ByteReverseWord32(mask2), key)) {
        return 1;
    }

    mask  = 0x01FE01FE; mask2 = 0x01FE01FE;
    if (DES_check(mask, mask2, key) ||
       DES_check(ByteReverseWord32(mask), ByteReverseWord32(mask2), key)) {
        return 1;
    }

    mask  = 0x1FE01FE0; mask2 = 0x0EF10EF1;
    if (DES_check(mask, mask2, key) ||
       DES_check(ByteReverseWord32(mask), ByteReverseWord32(mask2), key)) {
        return 1;
    }

    mask  = 0x1FFE1FFE; mask2 = 0x0EFE0EFE;
    if (DES_check(mask, mask2, key) ||
       DES_check(ByteReverseWord32(mask), ByteReverseWord32(mask2), key)) {
        return 1;
    }

    return 0;
}


bool key_des_check(uint8_t *key, int key_len, int ndc) {
    int i;
    struct buffer b;

    buf_set_read(&b, key, key_len);

    for (i = 0; i < ndc; ++i)
    {
    	uint8_t *dc = (uint8_t *) buf_read_alloc(&b, DES_KEY_SIZE);
        if (!dc)
        {
        	msg(D_CRYPT_ERRORS, "CRYPTO INFO: check_key_DES: insufficient key material");
            return false;
        }
        if (wolfSSL_DES_is_weak_key(dc))
        {
        	msg(D_CRYPT_ERRORS, "CRYPTO INFO: check_key_DES: weak key detected");
            return false;
        }
        if (!DES_check_key_parity(dc))
        {
        	msg(D_CRYPT_ERRORS, "CRYPTO INFO: check_key_DES: bad parity detected");
            return false;
        }
    }
    return true;

}

/* Sets the parity of the DES key for use */
void wolfSSL_DES_set_odd_parity(uint8_t* myDes)
{
    uint32_t i;

    for (i = 0; i < DES_BLOCK_SIZE; i++) {
    	uint8_t c = myDes[i];
        if ((
            ((c >> 1) & 0x01) ^
            ((c >> 2) & 0x01) ^
            ((c >> 3) & 0x01) ^
            ((c >> 4) & 0x01) ^
            ((c >> 5) & 0x01) ^
            ((c >> 6) & 0x01) ^
            ((c >> 7) & 0x01)) != 1) {
            msg(D_CRYPTO_DEBUG, "Setting odd parity bit");
            myDes[i] = *((unsigned char*)myDes + i) | 0x01;
        }
    }
}

void key_des_fixup(uint8_t *key, int key_len, int ndc) {
    int i;
    struct buffer b;

    buf_set_read(&b, key, key_len);
    for (i = 0; i < ndc; ++i)
    {
    	uint8_t *dc = (uint8_t *) buf_read_alloc(&b, DES_BLOCK_SIZE);
        if (!dc)
        {
            msg(D_CRYPT_ERRORS, "CRYPTO INFO: fixup_key_DES: insufficient key material");
            return;
        }
        wolfSSL_DES_set_odd_parity(dc);
    }
}

void cipher_des_encrypt_ecb(const unsigned char key[DES_KEY_LENGTH],
                            unsigned char src[DES_KEY_LENGTH],
                            unsigned char dst[DES_KEY_LENGTH]) {
	Des myDes;

    if (src == NULL || dst == NULL || key == NULL) {
    	msg(D_CRYPT_ERRORS, "Bad argument passed to cipher_des_encrypt_ecb");
    }

	wc_Des_SetKey(&myDes, key, NULL, DES_ENCRYPTION);
	wc_Des_EcbEncrypt(&myDes, dst, src, DES_KEY_LENGTH);
}

const cipher_kt_t *cipher_kt_get(const char *ciphername) {
	const struct cipher* cipher;

    for (cipher = cipher_tbl; cipher->name != NULL; cipher++) {
        if(strncmp(ciphername, cipher->name, strlen(cipher->name)+1) == 0) {
            return &cipher_static[cipher->type];
        }
    }
	return NULL;
}

const char *cipher_kt_name(const cipher_kt_t *cipher_kt) {
	if (*cipher_kt >= OV_WC_NULL_CIPHER_TYPE) {
		return NULL;
	} else {
		return cipher_tbl[*cipher_kt].name;
	}
}

int cipher_kt_key_size(const cipher_kt_t *cipher_kt) {
    if (cipher_kt == NULL) {
        return 0;
    }
    switch (*cipher_kt) {
	case OV_WC_AES_128_CBC_TYPE:  return 16;
	case OV_WC_AES_192_CBC_TYPE:  return 24;
	case OV_WC_AES_256_CBC_TYPE:  return 32;
	case OV_WC_AES_128_CTR_TYPE:  return 16;
	case OV_WC_AES_192_CTR_TYPE:  return 24;
	case OV_WC_AES_256_CTR_TYPE:  return 32;
	case OV_WC_AES_128_ECB_TYPE:  return 16;
	case OV_WC_AES_192_ECB_TYPE:  return 24;
	case OV_WC_AES_256_ECB_TYPE:  return 32;
	case OV_WC_AES_128_OFB_TYPE:  return 16;
	case OV_WC_AES_192_OFB_TYPE:  return 24;
	case OV_WC_AES_256_OFB_TYPE:  return 32;
	case OV_WC_AES_128_CFB_TYPE:  return 16;
	case OV_WC_AES_192_CFB_TYPE:  return 24;
	case OV_WC_AES_256_CFB_TYPE:  return 32;
	case OV_WC_AES_128_GCM_TYPE:  return 16;
	case OV_WC_AES_192_GCM_TYPE:  return 24;
	case OV_WC_AES_256_GCM_TYPE:  return 32;
	case OV_WC_DES_CBC_TYPE:      return 8;
	case OV_WC_DES_ECB_TYPE:      return 8;
	case OV_WC_DES_EDE3_CBC_TYPE: return 24;
	case OV_WC_DES_EDE3_ECB_TYPE: return 24;
	case OV_WC_CHACHA20_POLY1305_TYPE: return CHACHA20_POLY1305_AEAD_KEYSIZE;
	case OV_WC_NULL_CIPHER_TYPE: return 0;
    }
    return 0;
}

int cipher_kt_iv_size(const cipher_kt_t *cipher_kt) {
	return cipher_kt_block_size(cipher_kt);
}

int cipher_kt_block_size(const cipher_kt_t *cipher_kt) {
    if (cipher_kt == NULL) {
        return 0;
    }
    switch (*cipher_kt) {
    case OV_WC_AES_128_CBC_TYPE:
    case OV_WC_AES_192_CBC_TYPE:
    case OV_WC_AES_256_CBC_TYPE:
    case OV_WC_AES_128_CTR_TYPE:
    case OV_WC_AES_192_CTR_TYPE:
    case OV_WC_AES_256_CTR_TYPE:
    case OV_WC_AES_128_ECB_TYPE:
    case OV_WC_AES_192_ECB_TYPE:
    case OV_WC_AES_256_ECB_TYPE:
    case OV_WC_AES_128_OFB_TYPE:
    case OV_WC_AES_192_OFB_TYPE:
    case OV_WC_AES_256_OFB_TYPE:
    case OV_WC_AES_128_CFB_TYPE:
    case OV_WC_AES_192_CFB_TYPE:
    case OV_WC_AES_256_CFB_TYPE:
    case OV_WC_AES_128_GCM_TYPE:
    case OV_WC_AES_192_GCM_TYPE:
    case OV_WC_AES_256_GCM_TYPE:
    	return AES_BLOCK_SIZE;
    case OV_WC_DES_CBC_TYPE:
    case OV_WC_DES_ECB_TYPE:
    case OV_WC_DES_EDE3_CBC_TYPE:
    case OV_WC_DES_EDE3_ECB_TYPE:
    	return DES_BLOCK_SIZE;
    case OV_WC_CHACHA20_POLY1305_TYPE:
    	return CHACHA_CHUNK_WORDS;
    case OV_WC_NULL_CIPHER_TYPE:
    	return 0;
    }
	return 0;
}

int cipher_kt_tag_size(const cipher_kt_t *cipher_kt) {
    if (cipher_kt_mode_aead(cipher_kt)) {
        return OPENVPN_AEAD_TAG_LENGTH;
    } else {
        return 0;
    }
}

bool cipher_kt_insecure(const cipher_kt_t *cipher) {
    return !(cipher_kt_block_size(cipher) >= 128 / 8);
}

int cipher_kt_mode(const cipher_kt_t *cipher_kt) {
    if (cipher_kt == NULL) {
        return 0;
    }
    switch (*cipher_kt) {
    case OV_WC_AES_128_CBC_TYPE:
    case OV_WC_AES_192_CBC_TYPE:
    case OV_WC_AES_256_CBC_TYPE:
    case OV_WC_DES_CBC_TYPE:
    case OV_WC_DES_EDE3_CBC_TYPE:
		return OPENVPN_MODE_CBC;
	case OV_WC_AES_128_OFB_TYPE:
	case OV_WC_AES_192_OFB_TYPE:
	case OV_WC_AES_256_OFB_TYPE:
		return OPENVPN_MODE_OFB;
	case OV_WC_AES_128_CFB_TYPE:
	case OV_WC_AES_192_CFB_TYPE:
	case OV_WC_AES_256_CFB_TYPE:
		return OPENVPN_MODE_CFB;
	case OV_WC_AES_128_GCM_TYPE:
	case OV_WC_AES_192_GCM_TYPE:
	case OV_WC_AES_256_GCM_TYPE:
	case OV_WC_CHACHA20_POLY1305_TYPE:
		return OPENVPN_MODE_GCM;
	case OV_WC_NULL_CIPHER_TYPE:
		break;
	}
    return 0;
}

bool cipher_kt_mode_cbc(const cipher_kt_t *cipher) {
    return cipher && cipher_kt_mode(cipher) == OPENVPN_MODE_CBC;
}

bool cipher_kt_mode_ofb_cfb(const cipher_kt_t *cipher) {
    return cipher && (cipher_kt_mode(cipher) == OPENVPN_MODE_OFB
    		          || cipher_kt_mode(cipher) == OPENVPN_MODE_CFB);
}

bool cipher_kt_mode_aead(const cipher_kt_t *cipher) {
#ifdef HAVE_AEAD_CIPHER_MODES
    msg(M_FATAL, "wolfSSL does not support AEAD ciphers in OpenSSL compatibility layer");
#endif

    return false;
}

static void wc_cipher_init(cipher_ctx_t* ctx) {
	ctx->cipher_type = OV_WC_NULL_CIPHER_TYPE;
	ctx->enc = OV_WC_ENCRYPT;
}

cipher_ctx_t *cipher_ctx_new(void) {
	cipher_ctx_t *ctx = (cipher_ctx_t*) malloc(sizeof *ctx);
    check_malloc_return(ctx);
    wc_cipher_init(ctx);
    return ctx;
}

void cipher_ctx_free(cipher_ctx_t *ctx) {
	if (ctx) {
		free(ctx);
	}
}

static void check_key_length(const cipher_kt_t kt, int key_len) {
    switch (kt) {
    /* CHECK KEY LENGTHS */
    case OV_WC_AES_128_CBC_TYPE:
    case OV_WC_AES_128_CTR_TYPE:
    case OV_WC_AES_128_ECB_TYPE:
    case OV_WC_AES_128_OFB_TYPE:
    case OV_WC_AES_128_CFB_TYPE:
    case OV_WC_AES_128_GCM_TYPE:
    	if (key_len != 16) {
            msg(M_FATAL,
				"Wrong key length for chosen cipher.\n"
				"Cipher chosen: %s\n"
				"Key length expected: %d\n"
				"Key length provided: %d\n",
			cipher_kt_name(&kt), 16, key_len);
    	}
    	return;
    case OV_WC_AES_192_CBC_TYPE:
    case OV_WC_AES_192_CTR_TYPE:
    case OV_WC_AES_192_ECB_TYPE:
    case OV_WC_AES_192_OFB_TYPE:
    case OV_WC_AES_192_CFB_TYPE:
    case OV_WC_AES_192_GCM_TYPE:
    	if (key_len != 24) {
            msg(M_FATAL,
				"Wrong key length for chosen cipher.\n"
				"Cipher chosen: %s\n"
				"Key length expected: %d\n"
				"Key length provided: %d\n",
			cipher_kt_name(&kt), 24, key_len);
    	}
    	return;
    case OV_WC_AES_256_CBC_TYPE:
    case OV_WC_AES_256_CTR_TYPE:
    case OV_WC_AES_256_ECB_TYPE:
    case OV_WC_AES_256_OFB_TYPE:
    case OV_WC_AES_256_CFB_TYPE:
    case OV_WC_AES_256_GCM_TYPE:
    	if (key_len != 32) {
            msg(M_FATAL,
				"Wrong key length for chosen cipher.\n"
				"Cipher chosen: %s\n"
				"Key length expected: %d\n"
				"Key length provided: %d\n",
			cipher_kt_name(&kt), 32, key_len);
    	}
    	return;
    case OV_WC_DES_CBC_TYPE:
    case OV_WC_DES_ECB_TYPE:
    	if (key_len != DES_KEY_SIZE) {
            msg(M_FATAL,
				"Wrong key length for chosen cipher.\n"
				"Cipher chosen: %s\n"
				"Key length expected: %d\n"
				"Key length provided: %d\n",
			cipher_kt_name(&kt), DES_KEY_SIZE, key_len);
    	}
    	return;
    case OV_WC_DES_EDE3_CBC_TYPE:
    case OV_WC_DES_EDE3_ECB_TYPE:
    	if (key_len != DES3_KEY_SIZE) {
            msg(M_FATAL,
				"Wrong key length for chosen cipher.\n"
				"Cipher chosen: %s\n"
				"Key length expected: %d\n"
				"Key length provided: %d\n",
			cipher_kt_name(&kt), DES3_KEY_SIZE, key_len);
    	}
    	return;
    case OV_WC_CHACHA20_POLY1305_TYPE:
    	if (key_len != CHACHA20_POLY1305_AEAD_KEYSIZE) {
            msg(M_FATAL,
				"Wrong key length for chosen cipher.\n"
				"Cipher chosen: %s\n"
				"Key length expected: %d\n"
				"Key length provided: %d\n",
			cipher_kt_name(&kt), CHACHA20_POLY1305_AEAD_KEYSIZE, key_len);
    	}
    	return;
    case OV_WC_NULL_CIPHER_TYPE:
    	break;
    }
	msg(M_FATAL, "Invalid cipher.");
}

void cipher_ctx_init(cipher_ctx_t *ctx, const uint8_t *key, int key_len,
                     const cipher_kt_t *kt, int enc) {
	int ret;
    ASSERT(NULL != kt && NULL != ctx && NULL != key);

    check_key_length(*kt, key_len);

    switch (*kt) {
    /* SETUP AES */
    case OV_WC_AES_128_CBC_TYPE:
    case OV_WC_AES_128_CTR_TYPE:
    case OV_WC_AES_128_ECB_TYPE:
    case OV_WC_AES_128_OFB_TYPE:
    case OV_WC_AES_128_CFB_TYPE:
    case OV_WC_AES_128_GCM_TYPE:
    case OV_WC_AES_192_CBC_TYPE:
    case OV_WC_AES_192_CTR_TYPE:
    case OV_WC_AES_192_ECB_TYPE:
    case OV_WC_AES_192_OFB_TYPE:
    case OV_WC_AES_192_CFB_TYPE:
    case OV_WC_AES_192_GCM_TYPE:
    case OV_WC_AES_256_CBC_TYPE:
    case OV_WC_AES_256_CTR_TYPE:
    case OV_WC_AES_256_ECB_TYPE:
    case OV_WC_AES_256_OFB_TYPE:
    case OV_WC_AES_256_CFB_TYPE:
    case OV_WC_AES_256_GCM_TYPE:
    	if ((ret = wc_AesSetKey(
    			&ctx->cipher.aes, key, key_len, NULL,
				enc == OPENVPN_OP_ENCRYPT ? AES_ENCRYPTION : AES_DECRYPTION
			)) != 0) {
            msg(M_FATAL, "wc_AesSetKey failed with Errno: %d", ret);
    	}
    	break;
    case OV_WC_DES_CBC_TYPE:
    case OV_WC_DES_ECB_TYPE:
    	if ((ret = wc_Des_SetKey(
    			&ctx->cipher.des, key, NULL,
				enc == OPENVPN_OP_ENCRYPT ? DES_ENCRYPTION : DES_DECRYPTION
			)) != 0) {
            msg(M_FATAL, "wc_Des_SetKey failed with Errno: %d", ret);
    	}
    	break;
    case OV_WC_DES_EDE3_CBC_TYPE:
    case OV_WC_DES_EDE3_ECB_TYPE:
    	if ((ret = wc_Des3_SetKey(
    			&ctx->cipher.des3, key, NULL,
				enc == OPENVPN_OP_ENCRYPT ? DES_ENCRYPTION : DES_DECRYPTION
			)) != 0) {
            msg(M_FATAL, "wc_Des3_SetKey failed with Errno: %d", ret);
    	}
    	break;
    case OV_WC_CHACHA20_POLY1305_TYPE:
        if ((ret = wc_Chacha_SetKey(&ctx->cipher.chacha20_poly1305.chacha,
        					   key, CHACHA20_POLY1305_AEAD_KEYSIZE)) != 0) {
            msg(M_FATAL, "wc_Des3_SetKey failed with Errno: %d", ret);
        }
        ctx->cipher.chacha20_poly1305.iv_set = false;
        memcpy(ctx->cipher.chacha20_poly1305.poly1305Key, key,
        	   CHACHA20_POLY1305_AEAD_KEYSIZE);
    	break;
    case OV_WC_NULL_CIPHER_TYPE:
    	break;
    }
	ctx->cipher_type = *kt;
	ctx->enc = enc == OPENVPN_OP_ENCRYPT ? OV_WC_ENCRYPT : OV_WC_DECRYPT;
}

void cipher_ctx_cleanup(cipher_ctx_t *ctx) {
	EVP_CIPHER_CTX_cleanup(ctx);
}

int cipher_ctx_iv_length(const cipher_ctx_t *ctx) {
    return EVP_CIPHER_CTX_iv_length(ctx);
}

int cipher_ctx_get_tag(cipher_ctx_t *ctx, uint8_t *tag, int tag_len) {
#ifdef HAVE_AEAD_CIPHER_MODES
	msg(M_FATAL, "wolfSSL does not support AEAD ciphers in OpenSSL compatibility layer");
#else
    ASSERT(0);
#endif
    return 0;
}

int cipher_ctx_block_size(const cipher_ctx_t *ctx) {
    return EVP_CIPHER_CTX_block_size(ctx);
}

int cipher_ctx_mode(const cipher_ctx_t *ctx) {
    return EVP_CIPHER_CTX_mode(ctx);
}

const cipher_kt_t *cipher_ctx_get_cipher_kt(const cipher_ctx_t *ctx) {
    const struct cipher *ent;


    static const struct cipher{
            unsigned char type;
            const char *name;
    } cipher_tbl[] = {
        {AES_128_CBC_TYPE, "AES-128-CBC"},
        {AES_192_CBC_TYPE, "AES-192-CBC"},
        {AES_256_CBC_TYPE, "AES-256-CBC"},
    	{AES_128_CTR_TYPE, "AES-128-CTR"},
    	{AES_192_CTR_TYPE, "AES-192-CTR"},
    	{AES_256_CTR_TYPE, "AES-256-CTR"},
    	{AES_128_ECB_TYPE, "AES-128-ECB"},
    	{AES_192_ECB_TYPE, "AES-192-ECB"},
    	{AES_256_ECB_TYPE, "AES-256-ECB"},
        {DES_CBC_TYPE, "DES-CBC"},
        {DES_ECB_TYPE, "DES-ECB"},
        {DES_EDE3_CBC_TYPE, "DES-EDE3-CBC"},
        {DES_EDE3_ECB_TYPE, "DES-EDE3-ECB"},
        {ARC4_TYPE, "ARC4"},
    #ifdef HAVE_IDEA
        {IDEA_CBC_TYPE, "IDEA-CBC"},
    #endif
        { 0, NULL}
    };

	if (ctx == NULL)
		return NULL;

    for (ent = cipher_tbl; ent->name != NULL; ent++) {
        if (ctx->cipherType == ent->type) {
            return (EVP_CIPHER *)ent->name;
        }
    }
	return NULL;
}

int cipher_ctx_reset(cipher_ctx_t *ctx, const uint8_t *iv_buf) {
    return EVP_CipherInit(ctx, NULL, NULL, iv_buf, -1);
}

int cipher_ctx_update_ad(cipher_ctx_t *ctx, const uint8_t *src, int src_len) {
#ifdef HAVE_AEAD_CIPHER_MODES
	msg(M_FATAL, "wolfSSL does not support AEAD ciphers in OpenSSL compatibility layer");
#else
    ASSERT(0);
#endif
    return 0;
}

int cipher_ctx_update(cipher_ctx_t *ctx, uint8_t *dst, int *dst_len,
                      uint8_t *src, int src_len) {
    if (!EVP_CipherUpdate(ctx, dst, dst_len, src, src_len)) {
        msg(M_FATAL, "%s: EVP_CipherUpdate() failed", __func__);
    }
    return 1;
}

int cipher_ctx_final(cipher_ctx_t *ctx, uint8_t *dst, int *dst_len) {
    return EVP_CipherFinal(ctx, dst, dst_len);
}

int cipher_ctx_final_check_tag(cipher_ctx_t *ctx, uint8_t *dst, int *dst_len,
                               uint8_t *tag, size_t tag_len) {
#ifdef HAVE_AEAD_CIPHER_MODES
	msg(M_FATAL, "wolfSSL does not support AEAD ciphers in OpenSSL compatibility layer");
#else
    ASSERT(0);
#endif
    return 0;
}

const md_kt_t *md_kt_get(const char *digest) {
    const EVP_MD *md = NULL;
    ASSERT(digest);
    md = EVP_get_digestbyname(digest);
    if (!md) {
        msg(M_FATAL, "Message hash algorithm '%s' not found", digest);
    }
    if (EVP_MD_size(md) > MAX_HMAC_KEY_LENGTH) {
        msg(M_FATAL, "Message hash algorithm '%s' uses a default hash "
        		 	 "size (%d bytes) which is larger than " PACKAGE_NAME "'s current "
					 "maximum hash size (%d bytes)",
					 digest, EVP_MD_size(md), MAX_HMAC_KEY_LENGTH);
    }
    return md;
}

const char *md_kt_name(const md_kt_t *kt) {
    if (NULL == kt) {
        return "[null-digest]";
    }
    return kt;
}

int md_kt_size(const md_kt_t *kt) {
    return EVP_MD_size(kt);
}

int md_full(const md_kt_t *kt, const uint8_t *src, int src_len, uint8_t *dst) {
    unsigned int in_md_len = 0;
    return EVP_Digest(src, src_len, dst, &in_md_len, kt, NULL);
}

md_ctx_t *md_ctx_new(void) {
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    check_malloc_return(ctx);
    return ctx;
}

void md_ctx_free(md_ctx_t *ctx) {
	EVP_MD_CTX_free(ctx);
}

void md_ctx_init(md_ctx_t *ctx, const md_kt_t *kt) {
    ASSERT(NULL != ctx && NULL != kt);

    EVP_MD_CTX_init(ctx);
    EVP_DigestInit(ctx, kt);
}

void md_ctx_cleanup(md_ctx_t *ctx) {
	EVP_MD_CTX_cleanup(ctx);
}

int md_ctx_size(const md_ctx_t *ctx) {
    return EVP_MD_CTX_size(ctx);
}

void md_ctx_update(md_ctx_t *ctx, const uint8_t *src, int src_len) {
	EVP_DigestUpdate(ctx, src, src_len);
}

void md_ctx_final(md_ctx_t *ctx, uint8_t *dst) {
    unsigned int in_md_len = 0;
    EVP_DigestFinal(ctx, dst, &in_md_len);
}

hmac_ctx_t *hmac_ctx_new(void) {
    int ret;
	HMAC_CTX *ctx = (HMAC_CTX*) malloc(sizeof(HMAC_CTX));
    check_malloc_return(ctx);
    if ((ret = HMAC_CTX_init(ctx)) != WOLFSSL_SUCCESS) {
        msg(M_FATAL, "HMAC_CTX_Init failed. Errno: %d", ret);
    }
    return ctx;
}

void hmac_ctx_free(hmac_ctx_t *ctx) {
	HMAC_cleanup(ctx);
}

static const EVP_MD* wolfSSL_get_MD_from_ctx(const HMAC_CTX* ctx)
{
    switch (ctx->type) {
#ifndef NO_MD4
        case WC_HASH_TYPE_MD4:
        	return wolfSSL_EVP_md4();
#endif
#ifndef NO_MD5
        case WC_HASH_TYPE_MD5:
        	return wolfSSL_EVP_md5();
#endif
#ifndef NO_SHA
        case WC_HASH_TYPE_SHA:
        	return wolfSSL_EVP_md4();
#endif
#ifdef WOLFSSL_SHA224
        case WC_HASH_TYPE_SHA224:
        	return wolfSSL_EVP_sha1();
#endif
#ifndef NO_SHA256
        case WC_HASH_TYPE_SHA256:
        	return wolfSSL_EVP_sha256();
#endif /* !NO_SHA256 */
#ifdef WOLFSSL_SHA384
        case WC_HASH_TYPE_SHA384:
        	return wolfSSL_EVP_sha384();
#endif
#ifdef WOLFSSL_SHA512
        case WC_HASH_TYPE_SHA512:
        	return wolfSSL_EVP_sha512();
#endif /* WOLFSSL_SHA512 */
        default:
            return NULL;
    }
}

void hmac_ctx_init(hmac_ctx_t *ctx, const uint8_t *key, int key_length,
                   const md_kt_t *kt) {
	int ret;
	const EVP_MD* md;
    ASSERT(NULL != kt && NULL != ctx);

    if ((ret = HMAC_cleanup(ctx)) != SSL_SUCCESS) {
        msg(M_FATAL, "HMAC_cleanup failed. Errno: %d", ret);
    }
    if ((ret = HMAC_CTX_init(ctx)) != WOLFSSL_SUCCESS) {
        msg(M_FATAL, "HMAC_CTX_Init failed. Errno: %d", ret);
    }
    if ((ret = HMAC_Init(ctx, key, key_length, kt)) != WOLFSSL_SUCCESS) {
        msg(M_FATAL, "HMAC_Init_ex failed. Errno: %d", ret);
    }

    /* make sure we used a big enough key */
    md = wolfSSL_get_MD_from_ctx(ctx);
    ASSERT(NULL != md);
    ASSERT(EVP_MD_size(md) <= key_length);
}

void hmac_ctx_cleanup(hmac_ctx_t *ctx) {
	int ret;
    if ((ret = HMAC_cleanup(ctx)) != SSL_SUCCESS) {
        msg(M_FATAL, "wolfSSL_HMAC_cleanup failed. Errno: %d", ret);
    }
    if ((ret = HMAC_CTX_init(ctx)) != WOLFSSL_SUCCESS) {
        msg(M_FATAL, "wolfSSL_HMAC_CTX_Init failed. Errno: %d", ret);
    }
}

int hmac_ctx_size(const hmac_ctx_t *ctx) {
	const EVP_MD* md;
    md = wolfSSL_get_MD_from_ctx(ctx);
    ASSERT(NULL != md);
    return EVP_MD_size(md);
}

void hmac_ctx_reset(hmac_ctx_t *ctx) {
	HMAC_Init_ex(ctx, NULL, 0, NULL, NULL);
}

void hmac_ctx_update(hmac_ctx_t *ctx, const uint8_t *src, int src_len) {
	HMAC_Update(ctx, src, src_len);
}

void hmac_ctx_final(hmac_ctx_t *ctx, uint8_t *dst) {
    unsigned int in_hmac_len = 0;
    HMAC_Final(ctx, dst, &in_hmac_len);
}

extern bool cipher_kt_var_key_size(const cipher_kt_t *cipher) {
    return EVP_CIPHER_flags(cipher) & EVP_CIPH_VARIABLE_LENGTH;
}

const cipher_name_pair cipher_name_translation_table[] = {
    { "AES-128-GCM", "id-aes128-GCM" },
    { "AES-192-GCM", "id-aes192-GCM" },
    { "AES-256-GCM", "id-aes256-GCM" },
    { "CHACHA20-POLY1305", "ChaCha20-Poly1305" },
};
const size_t cipher_name_translation_table_count =
    sizeof(cipher_name_translation_table) / sizeof(*cipher_name_translation_table);


#endif /* ENABLE_CRYPTO_WOLFSSL */
