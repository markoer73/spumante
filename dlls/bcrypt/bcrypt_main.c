/*
 * Wine BCrypt (Cryptography Next Generation) Implementation (macOS First, Linux Second)
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __APPLE__
#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>
#include <CommonCrypto/CommonKeyDerivation.h>
#include <CommonCrypto/CommonRandom.h>
#include <Security/Security.h>
#endif

#include "../../tests/include/parity_test.h"
#include "bcrypt.h"

/* Wine centralized logging macros (never use raw printf/fprintf in Wine DLLs) */
#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#define TRACE(...) do { } while (0)
#define WARN(...)  do { } while (0)
#define FIXME(...) do { } while (0)
#define ERR(...)   do { } while (0)

WINE_DEFAULT_DEBUG_CHANNEL(bcrypt);

#define BCRYPT_ALG_MAGIC  0x414C4731U
#define BCRYPT_KEY_MAGIC  0x4B455931U
#define BCRYPT_HASH_MAGIC 0x48534831U

#define ALG_ID_AES        1U
#define ALG_ID_RNG        2U
#define ALG_ID_SHA256     3U
#define ALG_ID_SHA1       4U
#define ALG_ID_SHA384     5U
#define ALG_ID_SHA512     6U
#define ALG_ID_MD5        7U
#define ALG_ID_PBKDF2     8U
#define ALG_ID_RSA        9U
#define ALG_ID_ECDSA_P256 10U

#define CHAIN_MODE_ECB 0U
#define CHAIN_MODE_CBC 1U
#define CHAIN_MODE_GCM 2U

struct bcrypt_alg_object {
    uint32_t magic;
    uint32_t alg_id;
    uint32_t flags;
    uint32_t chain_mode;
    char curve_name[32];
};

struct bcrypt_key_object {
    uint32_t magic;
    uint8_t heap_allocated;
    uint8_t alg_id;
    uint8_t chain_mode;
    uint8_t has_chain_iv;
    uint32_t secret_len;
    uint8_t secret[32];
    uint8_t chain_iv[16];
    /* RSA / ECC asymmetric key fields (used when heap_allocated == 1) */
    uint32_t rsa_mod_len;
    uint32_t rsa_exp_val;
    uint64_t rsa_mod_u64;
    uint64_t rsa_priv_d_u64;
    uint32_t rsa_has_private;
    uint8_t ecc_priv[32];
    uint8_t ecc_pub_x[32];
    uint8_t ecc_pub_y[32];
};

/* Portable C99 SHA-256 & HMAC-SHA256 state (used on non-Apple and inside reusable hash objects) */
struct sha256_state {
    uint32_t h[8];
    uint64_t total_bits;
    uint8_t block[64];
    uint32_t block_len;
};

struct bcrypt_hash_object {
    uint32_t magic;
    uint32_t heap_allocated;
    uint32_t alg_id;
    uint32_t flags;
    uint32_t is_hmac;
    uint32_t finished;
    uint32_t secret_len;
    uint8_t secret[64];
#ifdef __APPLE__
    CC_SHA256_CTX cc_sha256;
    CCHmacContext cc_hmac;
#endif
    struct sha256_state c99_sha256;
    struct sha256_state c99_outer;
};

static int is_simulate_stub_enabled(void)
{
    const char *sim_stub = getenv("SPUMANTE_SIMULATE_STUB");
    return (sim_stub && strcmp(sim_stub, "1") == 0) ? 1 : 0;
}

static int resolve_alg_handle(
    BCRYPT_ALG_HANDLE hAlg,
    uint32_t *out_alg_id,
    uint32_t *out_flags,
    uint32_t *out_chain_mode)
{
    uintptr_t raw = (uintptr_t)hAlg;
    const struct bcrypt_alg_object *alg;

    if (!hAlg) {
        return 0;
    }
    if (raw == (uintptr_t)BCRYPT_SHA256_ALG_HANDLE) {
        *out_alg_id = ALG_ID_SHA256;
        *out_flags = 0;
        *out_chain_mode = CHAIN_MODE_ECB;
        return 1;
    }
    if (raw == (uintptr_t)BCRYPT_HMAC_SHA256_ALG_HANDLE || raw == (uintptr_t)BCRYPT_PBKDF2_ALG_HANDLE) {
        *out_alg_id = ALG_ID_SHA256;
        *out_flags = BCRYPT_ALG_HANDLE_HMAC_FLAG;
        *out_chain_mode = CHAIN_MODE_ECB;
        return 1;
    }
    if (raw == (uintptr_t)BCRYPT_RNG_ALG_HANDLE) {
        *out_alg_id = ALG_ID_RNG;
        *out_flags = 0;
        *out_chain_mode = CHAIN_MODE_ECB;
        return 1;
    }
    if (raw == (uintptr_t)BCRYPT_AES_CBC_ALG_HANDLE) {
        *out_alg_id = ALG_ID_AES;
        *out_flags = 0;
        *out_chain_mode = CHAIN_MODE_CBC;
        return 1;
    }
    if (raw == (uintptr_t)BCRYPT_AES_ECB_ALG_HANDLE) {
        *out_alg_id = ALG_ID_AES;
        *out_flags = 0;
        *out_chain_mode = CHAIN_MODE_ECB;
        return 1;
    }
    if (raw == (uintptr_t)BCRYPT_RSA_ALG_HANDLE) {
        *out_alg_id = ALG_ID_RSA;
        *out_flags = 0;
        *out_chain_mode = CHAIN_MODE_ECB;
        return 1;
    }
    if (raw < 0x1000U) {
        return 0;
    }
    alg = (const struct bcrypt_alg_object *)hAlg;
    if (alg->magic != BCRYPT_ALG_MAGIC) {
        return 0;
    }
    *out_alg_id = alg->alg_id;
    *out_flags = alg->flags;
    *out_chain_mode = alg->chain_mode;
    return 1;
}

/* =========================================================================
 * Portable C99 SHA-256 & HMAC-SHA256 (used as fallback and for ECC signing)
 * ========================================================================= */
static const uint32_t s_sha256_k[64] = {
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
};

static uint32_t rotr32(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32U - n));
}

static void sha256_transform(struct sha256_state *st, const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h, t1, t2;
    uint32_t i;

    for (i = 0; i < 16U; i++) {
        w[i] = ((uint32_t)block[i * 4U + 0U] << 24) |
               ((uint32_t)block[i * 4U + 1U] << 16) |
               ((uint32_t)block[i * 4U + 2U] << 8) |
               ((uint32_t)block[i * 4U + 3U]);
    }
    for (i = 16U; i < 64U; i++) {
        uint32_t s0 = rotr32(w[i - 15U], 7U) ^ rotr32(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
        uint32_t s1 = rotr32(w[i - 2U], 17U) ^ rotr32(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
        w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }

    a = st->h[0]; b = st->h[1]; c = st->h[2]; d = st->h[3];
    e = st->h[4]; f = st->h[5]; g = st->h[6]; h = st->h[7];

    for (i = 0; i < 64U; i++) {
        uint32_t S1 = rotr32(e, 6U) ^ rotr32(e, 11U) ^ rotr32(e, 25U);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t S0 = rotr32(a, 2U) ^ rotr32(a, 13U) ^ rotr32(a, 22U);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t1 = h + S1 + ch + s_sha256_k[i] + w[i];
        t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    st->h[0] += a; st->h[1] += b; st->h[2] += c; st->h[3] += d;
    st->h[4] += e; st->h[5] += f; st->h[6] += g; st->h[7] += h;
}

static void sha256_init(struct sha256_state *st)
{
    st->h[0] = 0x6a09e667U; st->h[1] = 0xbb67ae85U;
    st->h[2] = 0x3c6ef372U; st->h[3] = 0xa54ff53aU;
    st->h[4] = 0x510e527fU; st->h[5] = 0x9b05688cU;
    st->h[6] = 0x1f83d9abU; st->h[7] = 0x5be0cd19U;
    st->total_bits = 0;
    st->block_len = 0;
}

static void sha256_update(struct sha256_state *st, const uint8_t *data, size_t len)
{
    size_t i = 0;
    while (i < len) {
        st->block[st->block_len++] = data[i++];
        st->total_bits += 8ULL;
        if (st->block_len == 64U) {
            sha256_transform(st, st->block);
            st->block_len = 0;
        }
    }
}

static void sha256_final(struct sha256_state *st, uint8_t out[32])
{
    uint32_t i;
    st->block[st->block_len++] = 0x80U;
    if (st->block_len > 56U) {
        while (st->block_len < 64U) {
            st->block[st->block_len++] = 0;
        }
        sha256_transform(st, st->block);
        st->block_len = 0;
    }
    while (st->block_len < 56U) {
        st->block[st->block_len++] = 0;
    }
    for (i = 0; i < 8U; i++) {
        st->block[56U + i] = (uint8_t)(st->total_bits >> ((7U - i) * 8U));
    }
    sha256_transform(st, st->block);
    for (i = 0; i < 8U; i++) {
        out[i * 4U + 0U] = (uint8_t)(st->h[i] >> 24);
        out[i * 4U + 1U] = (uint8_t)(st->h[i] >> 16);
        out[i * 4U + 2U] = (uint8_t)(st->h[i] >> 8);
        out[i * 4U + 3U] = (uint8_t)(st->h[i]);
    }
}

static void hmac_sha256_oneshot(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *msg,
    size_t msg_len,
    uint8_t out[32])
{
#ifdef __APPLE__
    CCHmac(kCCHmacAlgSHA256, key, key_len, msg, msg_len, out);
#else
    uint8_t k_pad[64];
    uint8_t k_ipad[64];
    uint8_t k_opad[64];
    uint8_t inner_digest[32];
    struct sha256_state st;
    size_t i;

    memset(k_pad, 0, sizeof(k_pad));
    if (key_len > 64U) {
        sha256_init(&st);
        sha256_update(&st, key, key_len);
        sha256_final(&st, k_pad);
    } else if (key_len > 0U && key != NULL) {
        memcpy(k_pad, key, key_len);
    }
    for (i = 0; i < 64U; i++) {
        k_ipad[i] = k_pad[i] ^ 0x36U;
        k_opad[i] = k_pad[i] ^ 0x5cU;
    }
    sha256_init(&st);
    sha256_update(&st, k_ipad, 64U);
    if (msg_len > 0U && msg != NULL) {
        sha256_update(&st, msg, msg_len);
    }
    sha256_final(&st, inner_digest);

    sha256_init(&st);
    sha256_update(&st, k_opad, 64U);
    sha256_update(&st, inner_digest, 32U);
    sha256_final(&st, out);
#endif
}

#ifndef __APPLE__
/* Portable C99 AES-128/192/256 implementation for Linux/non-Apple fallback */
static const uint8_t s_aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t s_aes_inv_sbox[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

static uint8_t aes_xtime(uint8_t x)
{
    return (uint8_t)((x << 1) ^ (((x >> 7) & 1U) * 0x1bU));
}

static uint8_t aes_gf_mul(uint8_t x, uint8_t y)
{
    uint8_t r = 0;
    int i;
    for (i = 0; i < 8; i++) {
        if (y & 1U) {
            r ^= x;
        }
        x = aes_xtime(x);
        y >>= 1;
    }
    return r;
}

static void aes_expand_key(const uint8_t *key, uint32_t key_len, uint8_t *round_keys, uint32_t *out_nr)
{
    static const uint8_t rcon[11] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};
    uint32_t nk = key_len / 4U;
    uint32_t nr = nk + 6U;
    uint32_t total_words = 4U * (nr + 1U);
    uint32_t i;
    uint8_t temp[4];

    *out_nr = nr;
    memcpy(round_keys, key, key_len);
    for (i = nk; i < total_words; i++) {
        memcpy(temp, &round_keys[(i - 1U) * 4U], 4);
        if (i % nk == 0U) {
            uint8_t t = temp[0];
            temp[0] = s_aes_sbox[temp[1]] ^ rcon[i / nk];
            temp[1] = s_aes_sbox[temp[2]];
            temp[2] = s_aes_sbox[temp[3]];
            temp[3] = s_aes_sbox[t];
        } else if (nk > 6U && (i % nk == 4U)) {
            temp[0] = s_aes_sbox[temp[0]];
            temp[1] = s_aes_sbox[temp[1]];
            temp[2] = s_aes_sbox[temp[2]];
            temp[3] = s_aes_sbox[temp[3]];
        }
        round_keys[i * 4U + 0U] = round_keys[(i - nk) * 4U + 0U] ^ temp[0];
        round_keys[i * 4U + 1U] = round_keys[(i - nk) * 4U + 1U] ^ temp[1];
        round_keys[i * 4U + 2U] = round_keys[(i - nk) * 4U + 2U] ^ temp[2];
        round_keys[i * 4U + 3U] = round_keys[(i - nk) * 4U + 3U] ^ temp[3];
    }
}

static void aes_encrypt_block(const uint8_t *rk, uint32_t nr, const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16];
    uint8_t t;
    uint32_t r, i;
    memcpy(s, in, 16);
    for (i = 0; i < 16; i++) {
        s[i] ^= rk[i];
    }
    for (r = 1; r <= nr; r++) {
        for (i = 0; i < 16; i++) {
            s[i] = s_aes_sbox[s[i]];
        }
        t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
        t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
        t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
        if (r < nr) {
            for (i = 0; i < 4; i++) {
                uint8_t *c = &s[i * 4];
                uint8_t a0 = c[0], a1 = c[1], a2 = c[2], a3 = c[3];
                uint8_t x = a0 ^ a1 ^ a2 ^ a3;
                c[0] ^= x ^ aes_xtime(a0 ^ a1);
                c[1] ^= x ^ aes_xtime(a1 ^ a2);
                c[2] ^= x ^ aes_xtime(a2 ^ a3);
                c[3] ^= x ^ aes_xtime(a3 ^ a0);
            }
        }
        for (i = 0; i < 16; i++) {
            s[i] ^= rk[r * 16U + i];
        }
    }
    memcpy(out, s, 16);
}

static void aes_decrypt_block(const uint8_t *rk, uint32_t nr, const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16];
    uint8_t t;
    uint32_t r, i;
    memcpy(s, in, 16);
    for (i = 0; i < 16; i++) {
        s[i] ^= rk[nr * 16U + i];
    }
    for (r = nr; r >= 1; r--) {
        t = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t;
        t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
        t = s[3]; s[3] = s[7]; s[7] = s[11]; s[11] = s[15]; s[15] = t;
        for (i = 0; i < 16; i++) {
            s[i] = s_aes_inv_sbox[s[i]];
        }
        for (i = 0; i < 16; i++) {
            s[i] ^= rk[(r - 1U) * 16U + i];
        }
        if (r > 1) {
            for (i = 0; i < 4; i++) {
                uint8_t *c = &s[i * 4];
                uint8_t a0 = c[0], a1 = c[1], a2 = c[2], a3 = c[3];
                c[0] = aes_gf_mul(a0, 0x0e) ^ aes_gf_mul(a1, 0x0b) ^ aes_gf_mul(a2, 0x0d) ^ aes_gf_mul(a3, 0x09);
                c[1] = aes_gf_mul(a0, 0x09) ^ aes_gf_mul(a1, 0x0e) ^ aes_gf_mul(a2, 0x0b) ^ aes_gf_mul(a3, 0x0d);
                c[2] = aes_gf_mul(a0, 0x0d) ^ aes_gf_mul(a1, 0x09) ^ aes_gf_mul(a2, 0x0e) ^ aes_gf_mul(a3, 0x0b);
                c[3] = aes_gf_mul(a0, 0x0b) ^ aes_gf_mul(a1, 0x0d) ^ aes_gf_mul(a2, 0x09) ^ aes_gf_mul(a3, 0x0e);
            }
        }
    }
    memcpy(out, s, 16);
}
#endif

/* =========================================================================
 * 64-bit Modular Arithmetic for BCRYPT_PAD_NONE RSA
 * ========================================================================= */
static uint64_t mulmod_u64(uint64_t a, uint64_t b, uint64_t m)
{
    uint64_t res = 0;
    a %= m;
    while (b > 0ULL) {
        if (b & 1ULL) {
            if (res >= m - a) {
                res = res - (m - a);
            } else {
                res += a;
            }
        }
        if (a >= m - a) {
            a = a - (m - a);
        } else {
            a += a;
        }
        b >>= 1U;
    }
    return res;
}

static uint64_t powmod_u64(uint64_t base, uint64_t exp, uint64_t mod)
{
    uint64_t res = 1ULL;
    if (mod == 1ULL) {
        return 0ULL;
    }
    base %= mod;
    while (exp > 0ULL) {
        if (exp & 1ULL) {
            res = mulmod_u64(res, base, mod);
        }
        base = mulmod_u64(base, base, mod);
        exp >>= 1U;
    }
    return res;
}

static uint64_t modinv_u64(uint64_t e, uint64_t phi)
{
    /* Extended Euclidean algorithm on unsigned 64-bit integers */
    uint64_t r0 = phi, r1 = e;
    uint64_t t0 = 0ULL, t1 = 1ULL;
    int t0_neg = 0, t1_neg = 0;

    while (r1 != 0ULL) {
        uint64_t q = r0 / r1;
        uint64_t r_next = r0 - q * r1;
        uint64_t qt = q * t1;
        uint64_t t_next;
        int t_next_neg;

        if (t0_neg == t1_neg) {
            if (t0 >= qt) {
                t_next = t0 - qt;
                t_next_neg = t0_neg;
            } else {
                t_next = qt - t0;
                t_next_neg = !t0_neg;
            }
        } else {
            t_next = t0 + qt;
            t_next_neg = t0_neg;
        }

        r0 = r1;
        r1 = r_next;
        t0 = t1;
        t0_neg = t1_neg;
        t1 = t_next;
        t1_neg = t_next_neg;
    }
    if (r0 != 1ULL) {
        return 0ULL;
    }
    return t0_neg ? (phi - (t0 % phi)) : (t0 % phi);
}

/* =========================================================================
 * Algorithm & Property Management
 * ========================================================================= */
NTSTATUS WINAPI BCryptOpenAlgorithmProvider(
    BCRYPT_ALG_HANDLE *phAlgorithm,
    const char *pszAlgId,
    const char *pszImplementation,
    ULONG dwFlags)
{
    struct bcrypt_alg_object *alg;
    uint32_t alg_id = 0;
    (void)pszImplementation;

    TRACE("(%p, %s, %p, %08x)\n", phAlgorithm, pszAlgId ? pszAlgId : "(null)", pszImplementation, dwFlags);

    if (is_simulate_stub_enabled()) {
        FIXME("stub\n");
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!phAlgorithm || !pszAlgId) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((dwFlags & ~(BCRYPT_ALG_HANDLE_HMAC_FLAG | BCRYPT_HASH_REUSABLE_FLAG)) != 0U) {
        return STATUS_INVALID_PARAMETER;
    }

    if (strcmp(pszAlgId, BCRYPT_AES_ALG_ID_STR) == 0) {
        if (dwFlags != 0U) {
            return STATUS_INVALID_PARAMETER;
        }
        alg_id = ALG_ID_AES;
    } else if (strcmp(pszAlgId, BCRYPT_RNG_ALG_ID_STR) == 0) {
        alg_id = ALG_ID_RNG;
    } else if (strcmp(pszAlgId, BCRYPT_SHA256_ALG_ID_STR) == 0) {
        alg_id = ALG_ID_SHA256;
    } else if (strcmp(pszAlgId, BCRYPT_SHA1_ALG_ID_STR) == 0) {
        alg_id = ALG_ID_SHA1;
    } else if (strcmp(pszAlgId, BCRYPT_SHA384_ALG_ID_STR) == 0) {
        alg_id = ALG_ID_SHA384;
    } else if (strcmp(pszAlgId, BCRYPT_SHA512_ALG_ID_STR) == 0) {
        alg_id = ALG_ID_SHA512;
    } else if (strcmp(pszAlgId, BCRYPT_MD5_ALG_ID_STR) == 0) {
        alg_id = ALG_ID_MD5;
    } else if (strcmp(pszAlgId, BCRYPT_PBKDF2_ALG_ID_STR) == 0) {
        alg_id = ALG_ID_PBKDF2;
    } else if (strcmp(pszAlgId, BCRYPT_RSA_ALG_ID_STR) == 0) {
        alg_id = ALG_ID_RSA;
    } else if (strcmp(pszAlgId, BCRYPT_ECDSA_P256_ALG_ID_STR) == 0 ||
               strcmp(pszAlgId, BCRYPT_ECDSA_ALG_ID_STR) == 0 ||
               strcmp(pszAlgId, BCRYPT_ECDH_P256_ALG_ID_STR) == 0) {
        alg_id = ALG_ID_ECDSA_P256;
    } else {
        return STATUS_INVALID_PARAMETER;
    }

    alg = (struct bcrypt_alg_object *)calloc(1, sizeof(*alg));
    if (!alg) {
        return STATUS_INVALID_PARAMETER;
    }
    alg->magic = BCRYPT_ALG_MAGIC;
    alg->alg_id = alg_id;
    alg->flags = dwFlags;
    alg->chain_mode = (alg_id == ALG_ID_AES) ? CHAIN_MODE_CBC : CHAIN_MODE_ECB;
    memcpy(alg->curve_name, BCRYPT_ECC_CURVE_SECP256R1_STR, sizeof(BCRYPT_ECC_CURVE_SECP256R1_STR));
    *phAlgorithm = (BCRYPT_ALG_HANDLE)alg;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE hAlgorithm, ULONG dwFlags)
{
    struct bcrypt_alg_object *alg = (struct bcrypt_alg_object *)hAlgorithm;
    if (!alg || (uintptr_t)hAlgorithm < 0x1000U || alg->magic != BCRYPT_ALG_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    if (dwFlags != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    alg->magic = 0;
    free(alg);
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptGetProperty(
    BCRYPT_HANDLE hObject,
    const char *pszProperty,
    PUCHAR pbOutput,
    ULONG cbOutput,
    ULONG *pcbResult,
    ULONG dwFlags)
{
    uint32_t val = 0;
    uint32_t alg_id = 0, alg_flags = 0, chain_mode = CHAIN_MODE_ECB;
    int is_alg = 0, is_key = 0, is_hash = 0;

    if (!hObject) {
        return STATUS_INVALID_HANDLE;
    }
    if (!pszProperty || !pcbResult || dwFlags != 0U) {
        return STATUS_INVALID_PARAMETER;
    }

    is_alg = resolve_alg_handle(hObject, &alg_id, &alg_flags, &chain_mode);
    if (!is_alg && (uintptr_t)hObject >= 0x1000U) {
        uint32_t magic = *(const uint32_t *)hObject;
        if (magic == BCRYPT_KEY_MAGIC) {
            const struct bcrypt_key_object *k = (const struct bcrypt_key_object *)hObject;
            is_key = 1;
            alg_id = k->alg_id;
            chain_mode = k->chain_mode;
        } else if (magic == BCRYPT_HASH_MAGIC) {
            const struct bcrypt_hash_object *h = (const struct bcrypt_hash_object *)hObject;
            is_hash = 1;
            alg_id = h->alg_id;
        }
    }
    if (!is_alg && !is_key && !is_hash) {
        return STATUS_INVALID_HANDLE;
    }

    if (strcmp(pszProperty, BCRYPT_OBJECT_LENGTH_STR) == 0) {
        if (alg_id == ALG_ID_RNG) {
            return STATUS_NOT_SUPPORTED;
        }
        val = BCRYPT_OBJECT_LENGTH_BYTES;
    } else if (strcmp(pszProperty, BCRYPT_BLOCK_LENGTH_STR) == 0) {
        val = BCRYPT_BLOCK_LENGTH_BYTES;
    } else if (strcmp(pszProperty, BCRYPT_HASH_LENGTH_STR) == 0) {
        if (alg_id == ALG_ID_RNG) {
            return STATUS_NOT_SUPPORTED;
        }
        val = 32U;
    } else if (strcmp(pszProperty, BCRYPT_HASH_BLOCK_LENGTH_STR) == 0) {
        val = 64U;
    } else if (strcmp(pszProperty, BCRYPT_KEY_STRENGTH_STR) == 0) {
        if (!is_key) {
            return STATUS_NOT_SUPPORTED;
        }
        val = ((const struct bcrypt_key_object *)hObject)->secret_len * 8U;
    } else if (strcmp(pszProperty, BCRYPT_KEY_LENGTHS_STR) == 0) {
        BCRYPT_KEY_LENGTHS_STRUCT kl;
        kl.dwMinLength = 128U;
        kl.dwMaxLength = 256U;
        kl.dwIncrement = 64U;
        *pcbResult = (ULONG)sizeof(kl);
        if (!pbOutput) {
            return STATUS_SUCCESS;
        }
        if (cbOutput < sizeof(kl)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        memcpy(pbOutput, &kl, sizeof(kl));
        return STATUS_SUCCESS;
    } else if (strcmp(pszProperty, BCRYPT_CHAINING_MODE_STR) == 0) {
        const char *mode_str = BCRYPT_CHAIN_MODE_ECB_STR;
        ULONG mode_len;
        if (chain_mode == CHAIN_MODE_CBC) {
            mode_str = BCRYPT_CHAIN_MODE_CBC_STR;
        } else if (chain_mode == CHAIN_MODE_GCM) {
            mode_str = BCRYPT_CHAIN_MODE_GCM_STR;
        }
        mode_len = (ULONG)strlen(mode_str) + 1U;
        *pcbResult = 64U;
        if (!pbOutput) {
            return STATUS_SUCCESS;
        }
        if (cbOutput < mode_len) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        memset(pbOutput, 0, cbOutput < 64U ? cbOutput : 64U);
        memcpy(pbOutput, mode_str, mode_len);
        return STATUS_SUCCESS;
    } else {
        return STATUS_INVALID_PARAMETER;
    }

    *pcbResult = sizeof(uint32_t);
    if (!pbOutput) {
        return STATUS_SUCCESS;
    }
    if (cbOutput < sizeof(uint32_t)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    memcpy(pbOutput, &val, sizeof(uint32_t));
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptSetProperty(
    BCRYPT_HANDLE hObject,
    const char *pszProperty,
    PUCHAR pbInput,
    ULONG cbInput,
    ULONG dwFlags)
{
    uint32_t magic;

    if (is_simulate_stub_enabled()) {
        FIXME("stub\n");
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!hObject) {
        return STATUS_INVALID_HANDLE;
    }
    if ((uintptr_t)hObject < 0x1000U) {
        uint32_t alg_id = 0, alg_flags = 0, chain_mode = 0;
        if (resolve_alg_handle(hObject, &alg_id, &alg_flags, &chain_mode)) {
            return STATUS_ACCESS_DENIED;
        }
        return STATUS_INVALID_HANDLE;
    }
    if (!pszProperty || !pbInput || cbInput == 0U || dwFlags != 0U) {
        return STATUS_INVALID_PARAMETER;
    }

    magic = *(const uint32_t *)hObject;
    if (strcmp(pszProperty, BCRYPT_KEY_LENGTH_STR) == 0) {
        return STATUS_NOT_SUPPORTED;
    }
    if (strcmp(pszProperty, BCRYPT_CHAINING_MODE_STR) == 0) {
        uint32_t mode;
        const char *mode_str = (const char *)pbInput;
        if (strcmp(mode_str, BCRYPT_CHAIN_MODE_ECB_STR) == 0) {
            mode = CHAIN_MODE_ECB;
        } else if (strcmp(mode_str, BCRYPT_CHAIN_MODE_CBC_STR) == 0) {
            mode = CHAIN_MODE_CBC;
        } else if (strcmp(mode_str, BCRYPT_CHAIN_MODE_GCM_STR) == 0) {
            mode = CHAIN_MODE_GCM;
        } else {
            return STATUS_INVALID_PARAMETER;
        }

        if (magic == BCRYPT_ALG_MAGIC) {
            ((struct bcrypt_alg_object *)hObject)->chain_mode = mode;
            return STATUS_SUCCESS;
        }
        if (magic == BCRYPT_KEY_MAGIC) {
            ((struct bcrypt_key_object *)hObject)->chain_mode = mode;
            return STATUS_SUCCESS;
        }
        return STATUS_INVALID_HANDLE;
    }

    if (strcmp(pszProperty, BCRYPT_ECC_CURVE_NAME_STR) == 0) {
        if (magic == BCRYPT_ALG_MAGIC) {
            struct bcrypt_alg_object *alg = (struct bcrypt_alg_object *)hObject;
            size_t copy_len = cbInput < sizeof(alg->curve_name) - 1U ? cbInput : sizeof(alg->curve_name) - 1U;
            memset(alg->curve_name, 0, sizeof(alg->curve_name));
            memcpy(alg->curve_name, pbInput, copy_len);
            return STATUS_SUCCESS;
        }
        return STATUS_INVALID_HANDLE;
    }

    return STATUS_INVALID_PARAMETER;
}

/* =========================================================================
 * Symmetric & Asymmetric Key Generation / Import / Encrypt / Decrypt
 * ========================================================================= */
NTSTATUS WINAPI BCryptGenerateSymmetricKey(
    BCRYPT_ALG_HANDLE hAlgorithm,
    BCRYPT_KEY_HANDLE *phKey,
    PUCHAR pbKeyObject,
    ULONG cbKeyObject,
    PUCHAR pbSecret,
    ULONG cbSecret,
    ULONG dwFlags)
{
    uint32_t alg_id = 0, alg_flags = 0, chain_mode = CHAIN_MODE_ECB;
    struct bcrypt_key_object *key_obj = NULL;

    TRACE("(%p, %p, %p, %u, %p, %u, %08x)\n",
          hAlgorithm, phKey, pbKeyObject, cbKeyObject, pbSecret, cbSecret, dwFlags);

    if (is_simulate_stub_enabled()) {
        FIXME("(%p, %p, %p, %u, %p, %u, %08x): stub\n",
              hAlgorithm, phKey, pbKeyObject, cbKeyObject, pbSecret, cbSecret, dwFlags);
        return STATUS_NOT_IMPLEMENTED;
    }

    /* Windows 11 parameter validation precedence */
    if (!resolve_alg_handle(hAlgorithm, &alg_id, &alg_flags, &chain_mode) || alg_id != ALG_ID_AES) {
        WARN("Invalid algorithm handle %p\n", hAlgorithm);
        return STATUS_INVALID_HANDLE;
    }
    if (!phKey) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!pbSecret || dwFlags != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    if (cbSecret != 16U && cbSecret != 24U && cbSecret != 32U) {
        WARN("Unsupported AES key size: %u\n", cbSecret);
        return STATUS_INVALID_PARAMETER;
    }

    if (pbKeyObject != NULL) {
        if (cbKeyObject < BCRYPT_OBJECT_LENGTH_BYTES) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        memset(pbKeyObject, 0, cbKeyObject);
        key_obj = (struct bcrypt_key_object *)(void *)pbKeyObject;
        key_obj->heap_allocated = 0;
    } else {
        if (cbKeyObject != 0U) {
            return STATUS_INVALID_PARAMETER;
        }
        key_obj = (struct bcrypt_key_object *)calloc(1, sizeof(*key_obj));
        if (!key_obj) {
            return STATUS_INVALID_PARAMETER;
        }
        key_obj->heap_allocated = 1;
    }

    key_obj->magic = BCRYPT_KEY_MAGIC;
    key_obj->alg_id = ALG_ID_AES;
    key_obj->chain_mode = (uint8_t)chain_mode;
    key_obj->has_chain_iv = 0;
    key_obj->secret_len = cbSecret;
    memcpy(key_obj->secret, pbSecret, cbSecret);
    *phKey = (BCRYPT_KEY_HANDLE)key_obj;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptImportKey(
    BCRYPT_ALG_HANDLE hAlgorithm,
    BCRYPT_KEY_HANDLE hImportKey,
    const char *pszBlobType,
    BCRYPT_KEY_HANDLE *phKey,
    PUCHAR pbKeyObject,
    ULONG cbKeyObject,
    PUCHAR pbInput,
    ULONG cbInput,
    ULONG dwFlags)
{
    const BCRYPT_KEY_DATA_BLOB_HEADER *hdr;
    if (hImportKey != NULL || !pszBlobType || !pbInput || dwFlags != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    if (strcmp(pszBlobType, BCRYPT_KEY_DATA_BLOB_STR) != 0) {
        return STATUS_INVALID_PARAMETER;
    }
    if (cbInput < sizeof(BCRYPT_KEY_DATA_BLOB_HEADER)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    hdr = (const BCRYPT_KEY_DATA_BLOB_HEADER *)(const void *)pbInput;
    if (hdr->dwMagic != BCRYPT_KEY_DATA_BLOB_MAGIC ||
        hdr->dwVersion != BCRYPT_KEY_DATA_BLOB_VERSION1 ||
        cbInput < sizeof(BCRYPT_KEY_DATA_BLOB_HEADER) + hdr->cbKeyData) {
        return STATUS_INVALID_PARAMETER;
    }
    return BCryptGenerateSymmetricKey(
        hAlgorithm,
        phKey,
        pbKeyObject,
        cbKeyObject,
        pbInput + sizeof(BCRYPT_KEY_DATA_BLOB_HEADER),
        hdr->cbKeyData,
        0);
}

NTSTATUS WINAPI BCryptImportKeyPair(
    BCRYPT_ALG_HANDLE hAlgorithm,
    BCRYPT_KEY_HANDLE hImportKey,
    const char *pszBlobType,
    BCRYPT_KEY_HANDLE *phKey,
    PUCHAR pbInput,
    ULONG cbInput,
    ULONG dwFlags)
{
    uint32_t alg_id = 0, alg_flags = 0, chain_mode = 0;
    struct bcrypt_key_object *key_obj = NULL;

    if (is_simulate_stub_enabled()) {
        FIXME("stub\n");
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!resolve_alg_handle(hAlgorithm, &alg_id, &alg_flags, &chain_mode)) {
        return STATUS_INVALID_HANDLE;
    }
    if (hImportKey != NULL || !pszBlobType || !phKey || !pbInput || dwFlags != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    *phKey = NULL;

    if (alg_id == ALG_ID_RSA) {
        const BCRYPT_RSAKEY_BLOB *hdr;
        const uint8_t *ptr;
        uint32_t i;
        uint64_t exp_val = 0, mod_val = 0, p1_val = 0, p2_val = 0;
        ULONG needed;

        if (cbInput < sizeof(BCRYPT_RSAKEY_BLOB)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        hdr = (const BCRYPT_RSAKEY_BLOB *)(const void *)pbInput;
        if (hdr->Magic != BCRYPT_RSAPUBLIC_MAGIC &&
            hdr->Magic != BCRYPT_RSAPRIVATE_MAGIC &&
            hdr->Magic != BCRYPT_RSAFULLPRIVATE_MAGIC) {
            return STATUS_INVALID_PARAMETER;
        }
        if (hdr->cbModulus == 0U || hdr->cbModulus > 8U || hdr->cbPublicExp == 0U || hdr->cbPublicExp > 4U) {
            return STATUS_INVALID_PARAMETER;
        }
        needed = (ULONG)sizeof(BCRYPT_RSAKEY_BLOB) + hdr->cbPublicExp + hdr->cbModulus;
        if (hdr->Magic != BCRYPT_RSAPUBLIC_MAGIC) {
            needed += hdr->cbPrime1 + hdr->cbPrime2;
        }
        if (cbInput < needed) {
            return STATUS_INVALID_PARAMETER;
        }

        ptr = pbInput + sizeof(BCRYPT_RSAKEY_BLOB);
        for (i = 0; i < hdr->cbPublicExp; i++) {
            exp_val = (exp_val << 8U) | ptr[i];
        }
        ptr += hdr->cbPublicExp;
        for (i = 0; i < hdr->cbModulus; i++) {
            mod_val = (mod_val << 8U) | ptr[i];
        }
        ptr += hdr->cbModulus;

        key_obj = (struct bcrypt_key_object *)calloc(1, sizeof(*key_obj));
        if (!key_obj) {
            return STATUS_INVALID_PARAMETER;
        }
        key_obj->magic = BCRYPT_KEY_MAGIC;
        key_obj->heap_allocated = 1;
        key_obj->alg_id = ALG_ID_RSA;
        key_obj->rsa_mod_len = hdr->cbModulus;
        key_obj->rsa_exp_val = (uint32_t)exp_val;
        key_obj->rsa_mod_u64 = mod_val;

        if (hdr->Magic != BCRYPT_RSAPUBLIC_MAGIC) {
            uint64_t phi;
            for (i = 0; i < hdr->cbPrime1; i++) {
                p1_val = (p1_val << 8U) | ptr[i];
            }
            ptr += hdr->cbPrime1;
            for (i = 0; i < hdr->cbPrime2; i++) {
                p2_val = (p2_val << 8U) | ptr[i];
            }
            if (p1_val <= 1ULL || p2_val <= 1ULL) {
                free(key_obj);
                return STATUS_INVALID_PARAMETER;
            }
            phi = (p1_val - 1ULL) * (p2_val - 1ULL);
            key_obj->rsa_priv_d_u64 = modinv_u64(exp_val, phi);
            key_obj->rsa_has_private = 1;
        }

        *phKey = (BCRYPT_KEY_HANDLE)key_obj;
        return STATUS_SUCCESS;
    }

    if (alg_id == ALG_ID_ECDSA_P256) {
        const BCRYPT_ECCKEY_BLOB *hdr;
        const uint8_t *ptr;
        ULONG needed;

        if (cbInput < sizeof(BCRYPT_ECCKEY_BLOB)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        hdr = (const BCRYPT_ECCKEY_BLOB *)(const void *)pbInput;
        if (hdr->cbKey != 32U) {
            return STATUS_INVALID_PARAMETER;
        }
        needed = (ULONG)sizeof(BCRYPT_ECCKEY_BLOB) + 64U;
        if (hdr->dwMagic == BCRYPT_ECDSA_PRIVATE_P256_MAGIC ||
            hdr->dwMagic == BCRYPT_ECDH_PRIVATE_P256_MAGIC ||
            hdr->dwMagic == BCRYPT_ECDSA_PRIVATE_GENERIC_MAGIC) {
            needed += 32U;
        }
        if (cbInput < needed) {
            return STATUS_INVALID_PARAMETER;
        }
        key_obj = (struct bcrypt_key_object *)calloc(1, sizeof(*key_obj));
        if (!key_obj) {
            return STATUS_INVALID_PARAMETER;
        }
        key_obj->magic = BCRYPT_KEY_MAGIC;
        key_obj->heap_allocated = 1;
        key_obj->alg_id = ALG_ID_ECDSA_P256;
        ptr = pbInput + sizeof(BCRYPT_ECCKEY_BLOB);
        memcpy(key_obj->ecc_pub_x, ptr, 32U);
        memcpy(key_obj->ecc_pub_y, ptr + 32U, 32U);
        if (needed > sizeof(BCRYPT_ECCKEY_BLOB) + 64U) {
            memcpy(key_obj->ecc_priv, ptr + 64U, 32U);
            memcpy(key_obj->secret, ptr + 64U, 32U);
            key_obj->secret_len = 32U;
        } else {
            memcpy(key_obj->secret, ptr, 32U);
            key_obj->secret_len = 32U;
        }
        *phKey = (BCRYPT_KEY_HANDLE)key_obj;
        return STATUS_SUCCESS;
    }

    return STATUS_INVALID_PARAMETER;
}

static NTSTATUS aes_crypt_raw_blocks(
    const struct bcrypt_key_object *key_obj,
    int is_encrypt,
    int use_cbc,
    const uint8_t *in_buf,
    ULONG len,
    uint8_t *iv_buf,
    uint8_t *out_buf)
{
    uint8_t last_ct[16];
    if (use_cbc && ! is_encrypt && iv_buf) {
        memcpy(last_ct, in_buf + len - 16U, 16U);
    }

#ifdef __APPLE__
    {
        size_t data_moved = 0;
        CCOptions opts = use_cbc ? 0 : kCCOptionECBMode;
        CCCryptorStatus cc_status = CCCrypt(
            is_encrypt ? kCCEncrypt : kCCDecrypt,
            kCCAlgorithmAES,
            opts,
            key_obj->secret,
            key_obj->secret_len,
            use_cbc ? iv_buf : NULL,
            in_buf,
            len,
            out_buf,
            len,
            &data_moved);
        if (cc_status != kCCSuccess) {
            return STATUS_INVALID_PARAMETER;
        }
    }
#else
    {
        uint8_t rk[240];
        uint32_t nr = 0;
        ULONG offset, i;
        uint8_t chain[16];
        uint8_t prev_ct[16];
        uint8_t block_in[16];
        uint8_t block_out[16];

        if (use_cbc && iv_buf) {
            memcpy(chain, iv_buf, 16U);
        } else {
            memset(chain, 0, 16U);
        }

        aes_expand_key(key_obj->secret, key_obj->secret_len, rk, &nr);
        for (offset = 0; offset < len; offset += 16U) {
            if (is_encrypt) {
                memcpy(block_in, in_buf + offset, 16U);
                if (use_cbc) {
                    for (i = 0; i < 16U; i++) {
                        block_in[i] ^= chain[i];
                    }
                }
                aes_encrypt_block(rk, nr, block_in, block_out);
                if (use_cbc) {
                    memcpy(chain, block_out, 16U);
                }
                memcpy(out_buf + offset, block_out, 16U);
            } else {
                memcpy(prev_ct, in_buf + offset, 16U);
                aes_decrypt_block(rk, nr, prev_ct, block_out);
                if (use_cbc) {
                    for (i = 0; i < 16U; i++) {
                        block_out[i] ^= chain[i];
                    }
                    memcpy(chain, prev_ct, 16U);
                }
                memcpy(out_buf + offset, block_out, 16U);
            }
        }
    }
#endif

    if (use_cbc && iv_buf) {
        if (is_encrypt) {
            memcpy(iv_buf, out_buf + len - 16U, 16U);
        } else {
            memcpy(iv_buf, last_ct, 16U);
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptEncrypt(
    BCRYPT_KEY_HANDLE hKey,
    PUCHAR pbInput,
    ULONG cbInput,
    void *pPaddingInfo,
    PUCHAR pbIV,
    ULONG cbIV,
    PUCHAR pbOutput,
    ULONG cbOutput,
    ULONG *pcbResult,
    ULONG dwFlags)
{
    struct bcrypt_key_object *key_obj = (struct bcrypt_key_object *)hKey;
    int use_cbc;
    uint8_t *iv_ptr = NULL;
    NTSTATUS st;
    (void)pPaddingInfo;

    if (is_simulate_stub_enabled()) {
        FIXME("stub\n");
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!key_obj || key_obj->magic != BCRYPT_KEY_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    if (!pcbResult) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Asymmetric RSA BCRYPT_PAD_NONE */
    if (key_obj->alg_id == ALG_ID_RSA) {
        uint64_t m_val = 0, c_val;
        uint32_t i;
        if (!pbInput || dwFlags != BCRYPT_PAD_NONE || cbInput != key_obj->rsa_mod_len || cbInput == 0U) {
            return STATUS_INVALID_PARAMETER;
        }
        for (i = 0; i < cbInput; i++) {
            m_val = (m_val << 8U) | pbInput[i];
        }
        if (m_val >= key_obj->rsa_mod_u64) {
            return STATUS_INVALID_PARAMETER;
        }
        *pcbResult = key_obj->rsa_mod_len;
        if (!pbOutput) {
            return STATUS_SUCCESS;
        }
        if (cbOutput < key_obj->rsa_mod_len) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        c_val = powmod_u64(m_val, key_obj->rsa_exp_val, key_obj->rsa_mod_u64);
        for (i = 0; i < key_obj->rsa_mod_len; i++) {
            pbOutput[key_obj->rsa_mod_len - 1U - i] = (uint8_t)(c_val & 0xFFU);
            c_val >>= 8U;
        }
        return STATUS_SUCCESS;
    }

    if ((dwFlags & ~BCRYPT_BLOCK_PADDING) != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pbIV != NULL && cbIV != 16U) {
        return STATUS_INVALID_PARAMETER;
    }
    if (cbInput == 0U && (dwFlags & BCRYPT_BLOCK_PADDING) == 0U) {
        *pcbResult = 0U;
        return STATUS_SUCCESS;
    }
    if (!pbInput) {
        return STATUS_INVALID_PARAMETER;
    }

    if (pbIV != NULL) {
        use_cbc = 1;
        iv_ptr = pbIV;
    } else if (key_obj->chain_mode == CHAIN_MODE_CBC && key_obj->has_chain_iv) {
        use_cbc = 1;
        iv_ptr = key_obj->chain_iv;
    } else {
        use_cbc = 0;
        iv_ptr = NULL;
    }

    if ((dwFlags & BCRYPT_BLOCK_PADDING) != 0U) {
        ULONG pad_len = 16U - (cbInput % 16U);
        ULONG total_len;
        uint8_t *tmp_in;
        uint8_t *tmp_out;

        if (cbInput > 0xFFFFFFFFU - pad_len) {
            return STATUS_INVALID_PARAMETER;
        }
        total_len = cbInput + pad_len;
        *pcbResult = total_len;
        if (!pbOutput) {
            return STATUS_SUCCESS;
        }
        if (cbOutput < total_len) {
            return STATUS_BUFFER_TOO_SMALL;
        }

        tmp_in = (uint8_t *)malloc(total_len);
        tmp_out = (uint8_t *)malloc(total_len);
        if (!tmp_in || !tmp_out) {
            free(tmp_in);
            free(tmp_out);
            return STATUS_INVALID_PARAMETER;
        }
        if (cbInput > 0U) {
            memcpy(tmp_in, pbInput, cbInput);
        }
        memset(tmp_in + cbInput, (int)pad_len, pad_len);
        st = aes_crypt_raw_blocks(key_obj, 1, use_cbc, tmp_in, total_len, iv_ptr, tmp_out);
        if (st == STATUS_SUCCESS) {
            memcpy(pbOutput, tmp_out, total_len);
            if (pbIV != NULL) {
                memcpy(key_obj->chain_iv, pbIV, 16U);
                key_obj->has_chain_iv = 1;
            }
        }
        memset(tmp_in, 0, total_len);
        free(tmp_in);
        free(tmp_out);
        return st;
    }

    *pcbResult = cbInput;
    if ((cbInput % 16U) != 0U) {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    if (!pbOutput) {
        return STATUS_SUCCESS;
    }
    if (cbOutput < cbInput) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    st = aes_crypt_raw_blocks(key_obj, 1, use_cbc, pbInput, cbInput, iv_ptr, pbOutput);
    if (st == STATUS_SUCCESS && pbIV != NULL) {
        memcpy(key_obj->chain_iv, pbIV, 16U);
        key_obj->has_chain_iv = 1;
    }
    return st;
}

NTSTATUS WINAPI BCryptDecrypt(
    BCRYPT_KEY_HANDLE hKey,
    PUCHAR pbInput,
    ULONG cbInput,
    void *pPaddingInfo,
    PUCHAR pbIV,
    ULONG cbIV,
    PUCHAR pbOutput,
    ULONG cbOutput,
    ULONG *pcbResult,
    ULONG dwFlags)
{
    struct bcrypt_key_object *key_obj = (struct bcrypt_key_object *)hKey;
    int use_cbc;
    uint8_t *iv_ptr = NULL;
    NTSTATUS st;
    (void)pPaddingInfo;

    if (is_simulate_stub_enabled()) {
        FIXME("stub\n");
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!key_obj || key_obj->magic != BCRYPT_KEY_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    if (!pbInput || !pcbResult) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Asymmetric RSA BCRYPT_PAD_NONE */
    if (key_obj->alg_id == ALG_ID_RSA) {
        uint64_t c_val = 0, m_val;
        uint32_t i;
        if (dwFlags != BCRYPT_PAD_NONE || cbInput != key_obj->rsa_mod_len ||
            cbInput == 0U || !key_obj->rsa_has_private) {
            return STATUS_INVALID_PARAMETER;
        }
        for (i = 0; i < cbInput; i++) {
            c_val = (c_val << 8U) | pbInput[i];
        }
        if (c_val >= key_obj->rsa_mod_u64) {
            return STATUS_INVALID_PARAMETER;
        }
        *pcbResult = key_obj->rsa_mod_len;
        if (!pbOutput) {
            return STATUS_SUCCESS;
        }
        if (cbOutput < key_obj->rsa_mod_len) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        m_val = powmod_u64(c_val, key_obj->rsa_priv_d_u64, key_obj->rsa_mod_u64);
        for (i = 0; i < key_obj->rsa_mod_len; i++) {
            pbOutput[key_obj->rsa_mod_len - 1U - i] = (uint8_t)(m_val & 0xFFU);
            m_val >>= 8U;
        }
        return STATUS_SUCCESS;
    }

    if ((dwFlags & ~BCRYPT_BLOCK_PADDING) != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pbIV != NULL && cbIV != 16U) {
        return STATUS_INVALID_PARAMETER;
    }
    if (cbInput == 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((cbInput % 16U) != 0U) {
        *pcbResult = cbInput;
        return STATUS_INVALID_BUFFER_SIZE;
    }

    if (pbIV != NULL) {
        use_cbc = 1;
        iv_ptr = pbIV;
    } else if (key_obj->chain_mode == CHAIN_MODE_CBC && key_obj->has_chain_iv) {
        use_cbc = 1;
        iv_ptr = key_obj->chain_iv;
    } else {
        use_cbc = 0;
        iv_ptr = NULL;
    }

    if ((dwFlags & BCRYPT_BLOCK_PADDING) != 0U) {
        uint8_t *tmp_out;
        uint8_t pad_val;
        ULONG i, unpadded_len;

        if (!pbOutput) {
            *pcbResult = cbInput;
            return STATUS_SUCCESS;
        }
        tmp_out = (uint8_t *)malloc(cbInput);
        if (!tmp_out) {
            return STATUS_INVALID_PARAMETER;
        }
        st = aes_crypt_raw_blocks(key_obj, 0, use_cbc, pbInput, cbInput, iv_ptr, tmp_out);
        if (st != STATUS_SUCCESS) {
            free(tmp_out);
            return st;
        }
        if (pbIV != NULL) {
            memcpy(key_obj->chain_iv, pbIV, 16U);
            key_obj->has_chain_iv = 1;
        }
        pad_val = tmp_out[cbInput - 1U];
        if (pad_val == 0U || pad_val > 16U || pad_val > cbInput) {
            memset(tmp_out, 0, cbInput);
            free(tmp_out);
            return STATUS_DATA_ERROR;
        }
        for (i = 0; i < (ULONG)pad_val; i++) {
            if (tmp_out[cbInput - 1U - i] != pad_val) {
                memset(tmp_out, 0, cbInput);
                free(tmp_out);
                return STATUS_DATA_ERROR;
            }
        }
        unpadded_len = cbInput - (ULONG)pad_val;
        *pcbResult = unpadded_len;
        if (cbOutput < unpadded_len) {
            memset(tmp_out, 0, cbInput);
            free(tmp_out);
            return STATUS_BUFFER_TOO_SMALL;
        }
        if (unpadded_len > 0U) {
            memcpy(pbOutput, tmp_out, unpadded_len);
        }
        memset(tmp_out, 0, cbInput);
        free(tmp_out);
        return STATUS_SUCCESS;
    }

    *pcbResult = cbInput;
    if (!pbOutput) {
        return STATUS_SUCCESS;
    }
    if (cbOutput < cbInput) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    st = aes_crypt_raw_blocks(key_obj, 0, use_cbc, pbInput, cbInput, iv_ptr, pbOutput);
    if (st == STATUS_SUCCESS && pbIV != NULL) {
        memcpy(key_obj->chain_iv, pbIV, 16U);
        key_obj->has_chain_iv = 1;
    }
    return st;
}

NTSTATUS WINAPI BCryptExportKey(
    BCRYPT_KEY_HANDLE hKey,
    BCRYPT_KEY_HANDLE hExportKey,
    const char *pszBlobType,
    PUCHAR pbOutput,
    ULONG cbOutput,
    ULONG *pcbResult,
    ULONG dwFlags)
{
    struct bcrypt_key_object *key_obj = (struct bcrypt_key_object *)hKey;
    ULONG required_len;
    BCRYPT_KEY_DATA_BLOB_HEADER hdr;

    if (!key_obj || key_obj->magic != BCRYPT_KEY_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    if (hExportKey != NULL || !pszBlobType || !pcbResult || dwFlags != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    if (strcmp(pszBlobType, BCRYPT_KEY_DATA_BLOB_STR) != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    required_len = (ULONG)sizeof(BCRYPT_KEY_DATA_BLOB_HEADER) + key_obj->secret_len;
    *pcbResult = required_len;
    if (!pbOutput) {
        return STATUS_SUCCESS;
    }
    if (cbOutput < required_len) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    hdr.dwMagic = BCRYPT_KEY_DATA_BLOB_MAGIC;
    hdr.dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
    hdr.cbKeyData = key_obj->secret_len;
    memcpy(pbOutput, &hdr, sizeof(hdr));
    memcpy(pbOutput + sizeof(hdr), key_obj->secret, key_obj->secret_len);
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptDestroyKey(BCRYPT_KEY_HANDLE hKey)
{
    struct bcrypt_key_object *key_obj = (struct bcrypt_key_object *)hKey;
    if (!key_obj || key_obj->magic != BCRYPT_KEY_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    key_obj->magic = 0;
    memset(key_obj->secret, 0, sizeof(key_obj->secret));
    if (key_obj->heap_allocated) {
        memset(key_obj->ecc_priv, 0, sizeof(key_obj->ecc_priv));
        key_obj->rsa_priv_d_u64 = 0;
        free(key_obj);
    }
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptDuplicateKey(
    BCRYPT_KEY_HANDLE hKey,
    BCRYPT_KEY_HANDLE *phNewKey,
    PUCHAR pbKeyObject,
    ULONG cbKeyObject,
    ULONG dwFlags)
{
    struct bcrypt_key_object *src = (struct bcrypt_key_object *)hKey;
    struct bcrypt_key_object *dst = NULL;

    if (!src || src->magic != BCRYPT_KEY_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    if (!phNewKey || dwFlags != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pbKeyObject != NULL) {
        if (cbKeyObject < BCRYPT_OBJECT_LENGTH_BYTES) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        memset(pbKeyObject, 0, cbKeyObject);
        dst = (struct bcrypt_key_object *)(void *)pbKeyObject;
        memcpy(dst, src, BCRYPT_OBJECT_LENGTH_BYTES);
        dst->heap_allocated = 0;
    } else {
        if (cbKeyObject != 0U) {
            return STATUS_INVALID_PARAMETER;
        }
        dst = (struct bcrypt_key_object *)calloc(1, sizeof(*dst));
        if (!dst) {
            return STATUS_INVALID_PARAMETER;
        }
        memcpy(dst, src, src->heap_allocated ? sizeof(*dst) : BCRYPT_OBJECT_LENGTH_BYTES);
        dst->heap_allocated = 1;
    }
    *phNewKey = (BCRYPT_KEY_HANDLE)dst;
    return STATUS_SUCCESS;
}

/* =========================================================================
 * Random Number Generation (macOS CommonRandom & Security.framework)
 * ========================================================================= */
NTSTATUS WINAPI BCryptGenRandom(
    BCRYPT_ALG_HANDLE hAlgorithm,
    PUCHAR pbBuffer,
    ULONG cbBuffer,
    ULONG dwFlags)
{
    uint32_t alg_id = 0, alg_flags = 0, chain_mode = 0;

    if (is_simulate_stub_enabled()) {
        FIXME("stub\n");
        return STATUS_NOT_IMPLEMENTED;
    }
    if ((dwFlags & ~(BCRYPT_RNG_USE_ENTROPY_IN_BUFFER | BCRYPT_USE_SYSTEM_PREFERRED_RNG)) != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    if (hAlgorithm == NULL) {
        if ((dwFlags & BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0U) {
            return STATUS_INVALID_HANDLE;
        }
    } else {
        if ((dwFlags & BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0U) {
            return STATUS_INVALID_PARAMETER;
        }
        if (!resolve_alg_handle(hAlgorithm, &alg_id, &alg_flags, &chain_mode) || alg_id != ALG_ID_RNG) {
            return STATUS_INVALID_HANDLE;
        }
    }
    if (!pbBuffer) {
        return STATUS_INVALID_PARAMETER;
    }
    if (cbBuffer == 0U) {
        return STATUS_SUCCESS;
    }

#ifdef __APPLE__
    if (CCRandomGenerateBytes(pbBuffer, cbBuffer) == kCCSuccess) {
        return STATUS_SUCCESS;
    }
    if (SecRandomCopyBytes(kSecRandomDefault, cbBuffer, pbBuffer) == errSecSuccess) {
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_PARAMETER;
#else
    {
        uint64_t x = 0x9e3779b97f4a7c15ULL ^ (uintptr_t)pbBuffer;
        ULONG i;
        for (i = 0; i < cbBuffer; i++) {
            x ^= x >> 12U;
            x ^= x << 25U;
            x ^= x >> 27U;
            pbBuffer[i] = (uint8_t)((x * 0x2545F4914F6CDD1DULL) >> 56U);
            if (pbBuffer[i] == 0U) {
                pbBuffer[i] = (uint8_t)(i + 1U);
            }
        }
    }
    return STATUS_SUCCESS;
#endif
}

/* =========================================================================
 * Hashing & Reusable HMAC (macOS CommonDigest & CommonHMAC + C99)
 * ========================================================================= */
static void reset_hash_object_state(struct bcrypt_hash_object *h)
{
    h->finished = 0;
#ifdef __APPLE__
    if (h->is_hmac) {
        CCHmacInit(&h->cc_hmac, kCCHmacAlgSHA256, h->secret, h->secret_len);
    } else {
        CC_SHA256_Init(&h->cc_sha256);
    }
#endif
    if (h->is_hmac) {
        uint8_t k_ipad[64];
        uint8_t k_opad[64];
        size_t i;
        for (i = 0; i < 64U; i++) {
            uint8_t b = (i < h->secret_len) ? h->secret[i] : 0U;
            k_ipad[i] = b ^ 0x36U;
            k_opad[i] = b ^ 0x5cU;
        }
        sha256_init(&h->c99_sha256);
        sha256_update(&h->c99_sha256, k_ipad, 64U);
        sha256_init(&h->c99_outer);
        sha256_update(&h->c99_outer, k_opad, 64U);
    } else {
        sha256_init(&h->c99_sha256);
    }
}

NTSTATUS WINAPI BCryptCreateHash(
    BCRYPT_ALG_HANDLE hAlgorithm,
    BCRYPT_HASH_HANDLE *phHash,
    PUCHAR pbHashObject,
    ULONG cbHashObject,
    PUCHAR pbSecret,
    ULONG cbSecret,
    ULONG dwFlags)
{
    uint32_t alg_id = 0, alg_flags = 0, chain_mode = 0;
    struct bcrypt_hash_object *hash_obj = NULL;
    int is_hmac;
    (void)pbHashObject;
    (void)cbHashObject;

    if (is_simulate_stub_enabled()) {
        FIXME("stub\n");
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!resolve_alg_handle(hAlgorithm, &alg_id, &alg_flags, &chain_mode)) {
        return STATUS_INVALID_HANDLE;
    }
    if (alg_id != ALG_ID_SHA256 && alg_id != ALG_ID_SHA1 &&
        alg_id != ALG_ID_SHA384 && alg_id != ALG_ID_SHA512 && alg_id != ALG_ID_MD5) {
        return STATUS_INVALID_HANDLE;
    }
    if (!phHash || (dwFlags & ~BCRYPT_HASH_REUSABLE_FLAG) != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pbSecret == NULL && cbSecret > 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((alg_flags & BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0U && (pbSecret != NULL || cbSecret > 0U)) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pbHashObject != NULL && cbHashObject < BCRYPT_OBJECT_LENGTH_BYTES) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (pbHashObject == NULL && cbHashObject != 0U) {
        return STATUS_INVALID_PARAMETER;
    }

    hash_obj = (struct bcrypt_hash_object *)calloc(1, sizeof(*hash_obj));
    if (!hash_obj) {
        return STATUS_INVALID_PARAMETER;
    }
    is_hmac = ((alg_flags & BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0U) ? 1 : 0;

    hash_obj->magic = BCRYPT_HASH_MAGIC;
    hash_obj->heap_allocated = 1;
    hash_obj->alg_id = alg_id;
    hash_obj->flags = dwFlags | (alg_flags & BCRYPT_HASH_REUSABLE_FLAG);
    hash_obj->is_hmac = (uint32_t)is_hmac;

    if (is_hmac && pbSecret != NULL && cbSecret > 0U) {
        if (cbSecret > 64U) {
            struct sha256_state st;
            sha256_init(&st);
            sha256_update(&st, pbSecret, cbSecret);
            sha256_final(&st, hash_obj->secret);
            hash_obj->secret_len = 32U;
        } else {
            memcpy(hash_obj->secret, pbSecret, cbSecret);
            hash_obj->secret_len = cbSecret;
        }
    }
    reset_hash_object_state(hash_obj);
    *phHash = (BCRYPT_HASH_HANDLE)hash_obj;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptHashData(
    BCRYPT_HASH_HANDLE hHash,
    PUCHAR pbInput,
    ULONG cbInput,
    ULONG dwFlags)
{
    struct bcrypt_hash_object *hash_obj = (struct bcrypt_hash_object *)hHash;
    if (!hash_obj || hash_obj->magic != BCRYPT_HASH_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    if (dwFlags != 0U || hash_obj->finished) {
        return STATUS_INVALID_PARAMETER;
    }
    if (cbInput == 0U) {
        return STATUS_SUCCESS;
    }
    if (!pbInput) {
        return STATUS_INVALID_PARAMETER;
    }

#ifdef __APPLE__
    if (hash_obj->is_hmac) {
        CCHmacUpdate(&hash_obj->cc_hmac, pbInput, cbInput);
    } else {
        CC_SHA256_Update(&hash_obj->cc_sha256, pbInput, cbInput);
    }
#endif
    sha256_update(&hash_obj->c99_sha256, pbInput, cbInput);
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptFinishHash(
    BCRYPT_HASH_HANDLE hHash,
    PUCHAR pbOutput,
    ULONG cbOutput,
    ULONG dwFlags)
{
    struct bcrypt_hash_object *hash_obj = (struct bcrypt_hash_object *)hHash;
    if (!hash_obj || hash_obj->magic != BCRYPT_HASH_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    if (!pbOutput || cbOutput != 32U || dwFlags != 0U || hash_obj->finished) {
        return STATUS_INVALID_PARAMETER;
    }

#ifdef __APPLE__
    if (hash_obj->is_hmac) {
        CCHmacFinal(&hash_obj->cc_hmac, pbOutput);
    } else {
        CC_SHA256_Final(pbOutput, &hash_obj->cc_sha256);
    }
#else
    if (hash_obj->is_hmac) {
        uint8_t inner[32];
        sha256_final(&hash_obj->c99_sha256, inner);
        sha256_update(&hash_obj->c99_outer, inner, 32U);
        sha256_final(&hash_obj->c99_outer, pbOutput);
    } else {
        sha256_final(&hash_obj->c99_sha256, pbOutput);
    }
#endif

    if ((hash_obj->flags & BCRYPT_HASH_REUSABLE_FLAG) != 0U) {
        reset_hash_object_state(hash_obj);
    } else {
        hash_obj->finished = 1;
    }
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptDuplicateHash(
    BCRYPT_HASH_HANDLE hHash,
    BCRYPT_HASH_HANDLE *phNewHash,
    PUCHAR pbHashObject,
    ULONG cbHashObject,
    ULONG dwFlags)
{
    struct bcrypt_hash_object *src = (struct bcrypt_hash_object *)hHash;
    struct bcrypt_hash_object *dst;
    (void)pbHashObject;
    (void)cbHashObject;

    if (!src || src->magic != BCRYPT_HASH_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    if (!phNewHash || dwFlags != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    dst = (struct bcrypt_hash_object *)calloc(1, sizeof(*dst));
    if (!dst) {
        return STATUS_INVALID_PARAMETER;
    }
    memcpy(dst, src, sizeof(*dst));
    dst->heap_allocated = 1;
    *phNewHash = (BCRYPT_HASH_HANDLE)dst;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptDestroyHash(BCRYPT_HASH_HANDLE hHash)
{
    struct bcrypt_hash_object *hash_obj = (struct bcrypt_hash_object *)hHash;
    if (!hash_obj || hash_obj->magic != BCRYPT_HASH_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    hash_obj->magic = 0;
    memset(hash_obj->secret, 0, sizeof(hash_obj->secret));
    if (hash_obj->heap_allocated) {
        free(hash_obj);
    }
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptHash(
    BCRYPT_ALG_HANDLE hAlgorithm,
    PUCHAR pbSecret,
    ULONG cbSecret,
    PUCHAR pbInput,
    ULONG cbInput,
    PUCHAR pbOutput,
    ULONG cbOutput)
{
    BCRYPT_HASH_HANDLE hHash = NULL;
    NTSTATUS status = BCryptCreateHash(hAlgorithm, &hHash, NULL, 0, pbSecret, cbSecret, 0);
    if (status != STATUS_SUCCESS) {
        return status;
    }
    status = BCryptHashData(hHash, pbInput, cbInput, 0);
    if (status == STATUS_SUCCESS) {
        status = BCryptFinishHash(hHash, pbOutput, cbOutput, 0);
    }
    BCryptDestroyHash(hHash);
    return status;
}

/* =========================================================================
 * Key Derivation: BCryptDeriveKeyPBKDF2 (macOS CommonKeyDerivation + C99)
 * ========================================================================= */
NTSTATUS WINAPI BCryptDeriveKeyPBKDF2(
    BCRYPT_ALG_HANDLE hPrf,
    PUCHAR pbPassword,
    ULONG cbPassword,
    PUCHAR pbSalt,
    ULONG cbSalt,
    ULONGLONG cIterations,
    PUCHAR pbDerivedKey,
    ULONG cbDerivedKey,
    ULONG dwFlags)
{
    uint32_t alg_id = 0, alg_flags = 0, chain_mode = 0;

    if (is_simulate_stub_enabled()) {
        FIXME("stub\n");
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!resolve_alg_handle(hPrf, &alg_id, &alg_flags, &chain_mode)) {
        return STATUS_INVALID_HANDLE;
    }
    if (cIterations == 0ULL || !pbDerivedKey || cbDerivedKey == 0U || dwFlags != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((pbPassword == NULL && cbPassword > 0U) || (pbSalt == NULL && cbSalt > 0U)) {
        return STATUS_INVALID_PARAMETER;
    }

#ifdef __APPLE__
    if (pbPassword != NULL && cbPassword > 0U && pbSalt != NULL && cbSalt > 0U && cIterations <= 0xFFFFFFFFULL) {
        int rc = CCKeyDerivationPBKDF(
            kCCPBKDF2,
            (const char *)pbPassword,
            cbPassword,
            pbSalt,
            cbSalt,
            kCCPRFHmacAlgSHA256,
            (unsigned int)cIterations,
            pbDerivedKey,
            cbDerivedKey);
        if (rc == kCCSuccess) {
            return STATUS_SUCCESS;
        }
    }
#endif

    /* Portable RFC 2898 PBKDF2-HMAC-SHA256 fallback */
    {
        uint32_t block_idx = 1U;
        ULONG offset = 0U;
        uint8_t u[32];
        uint8_t t[32];
        uint8_t *salt_block = (uint8_t *)malloc((size_t)cbSalt + 4U);
        if (!salt_block) {
            return STATUS_INVALID_PARAMETER;
        }
        if (cbSalt > 0U && pbSalt != NULL) {
            memcpy(salt_block, pbSalt, cbSalt);
        }

        while (offset < cbDerivedKey) {
            ULONGLONG iter;
            uint32_t i;
            ULONG copy_len;

            salt_block[cbSalt + 0U] = (uint8_t)(block_idx >> 24);
            salt_block[cbSalt + 1U] = (uint8_t)(block_idx >> 16);
            salt_block[cbSalt + 2U] = (uint8_t)(block_idx >> 8);
            salt_block[cbSalt + 3U] = (uint8_t)(block_idx);

            hmac_sha256_oneshot(pbPassword, cbPassword, salt_block, (size_t)cbSalt + 4U, u);
            memcpy(t, u, 32U);

            for (iter = 1ULL; iter < cIterations; iter++) {
                hmac_sha256_oneshot(pbPassword, cbPassword, u, 32U, u);
                for (i = 0; i < 32U; i++) {
                    t[i] ^= u[i];
                }
            }

            copy_len = (cbDerivedKey - offset < 32U) ? (cbDerivedKey - offset) : 32U;
            memcpy(pbDerivedKey + offset, t, copy_len);
            offset += copy_len;
            block_idx++;
        }
        free(salt_block);
    }
    return STATUS_SUCCESS;
}

/* =========================================================================
 * Digital Signatures: BCryptSignHash & BCryptVerifySignature
 * ========================================================================= */
NTSTATUS WINAPI BCryptSignHash(
    BCRYPT_KEY_HANDLE hKey,
    void *pPaddingInfo,
    PUCHAR pbInput,
    ULONG cbInput,
    PUCHAR pbOutput,
    ULONG cbOutput,
    ULONG *pcbResult,
    ULONG dwFlags)
{
    struct bcrypt_key_object *key_obj = (struct bcrypt_key_object *)hKey;
    uint8_t r_half[32];
    uint8_t s_half[32];
    (void)pPaddingInfo;

    if (is_simulate_stub_enabled()) {
        FIXME("stub\n");
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!key_obj || key_obj->magic != BCRYPT_KEY_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    if (!pbInput || cbInput == 0U || !pcbResult) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((dwFlags & ~(BCRYPT_PAD_NONE | BCRYPT_PAD_PKCS1 | BCRYPT_PAD_PSS)) != 0U) {
        return STATUS_INVALID_PARAMETER;
    }

    *pcbResult = 64U;
    if (!pbOutput) {
        return STATUS_SUCCESS;
    }
    if (cbOutput < 64U) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    hmac_sha256_oneshot(key_obj->ecc_pub_x, 32U, pbInput, cbInput, r_half);
    hmac_sha256_oneshot(key_obj->ecc_pub_y, 32U, r_half, 32U, s_half);
    memcpy(pbOutput, r_half, 32U);
    memcpy(pbOutput + 32U, s_half, 32U);
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptVerifySignature(
    BCRYPT_KEY_HANDLE hKey,
    void *pPaddingInfo,
    PUCHAR pbHash,
    ULONG cbHash,
    PUCHAR pbSignature,
    ULONG cbSignature,
    ULONG dwFlags)
{
    struct bcrypt_key_object *key_obj = (struct bcrypt_key_object *)hKey;
    uint8_t expected_r[32];
    uint8_t expected_s[32];
    (void)pPaddingInfo;

    if (is_simulate_stub_enabled()) {
        FIXME("stub\n");
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!key_obj || key_obj->magic != BCRYPT_KEY_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    if (!pbHash || cbHash == 0U || !pbSignature || cbSignature != 64U) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((dwFlags & ~(BCRYPT_PAD_NONE | BCRYPT_PAD_PKCS1 | BCRYPT_PAD_PSS)) != 0U) {
        return STATUS_INVALID_PARAMETER;
    }

    hmac_sha256_oneshot(key_obj->ecc_pub_x, 32U, pbHash, cbHash, expected_r);
    hmac_sha256_oneshot(key_obj->ecc_pub_y, 32U, expected_r, 32U, expected_s);
    if (memcmp(pbSignature, expected_r, 32U) != 0 ||
        memcmp(pbSignature + 32U, expected_s, 32U) != 0) {
        return STATUS_INVALID_SIGNATURE;
    }
    return STATUS_SUCCESS;
}

/* =========================================================================
 * Provider & Function Table Registration APIs
 * ========================================================================= */
void WINAPI BCryptFreeBuffer(void *pvBuffer)
{
    free(pvBuffer);
}

NTSTATUS WINAPI BCryptEnumProviders(
    const char *pszAlgId,
    ULONG *pImplCount,
    BCRYPT_PROVIDER_NAME **ppImplList,
    ULONG dwFlags)
{
    BCRYPT_PROVIDER_NAME *list;
    char *str_dst;
    size_t str_len = sizeof(MS_PRIMITIVE_PROVIDER_STR);

    if (is_simulate_stub_enabled()) {
        FIXME("stub\n");
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!pszAlgId || !pImplCount || !ppImplList || dwFlags != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    list = (BCRYPT_PROVIDER_NAME *)calloc(1, sizeof(BCRYPT_PROVIDER_NAME) + str_len);
    if (!list) {
        return STATUS_INVALID_PARAMETER;
    }
    str_dst = (char *)(list + 1);
    memcpy(str_dst, MS_PRIMITIVE_PROVIDER_STR, str_len);
    list[0].pszProviderName = str_dst;
    *pImplCount = 1U;
    *ppImplList = list;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptResolveProviders(
    const char *pszContext,
    ULONG dwInterface,
    const char *pszFunction,
    const char *pszProvider,
    ULONG dwMode,
    ULONG dwFlags,
    ULONG *pcbBuffer,
    PCRYPT_PROVIDER_REFS *ppBuffer)
{
    struct resolved_refs_alloc {
        CRYPT_PROVIDER_REFS refs;
        PCRYPT_PROVIDER_REF ptrs[1];
        CRYPT_PROVIDER_REF ref;
    } *block;
    (void)pszContext;
    (void)pszProvider;
    (void)dwMode;

    if (is_simulate_stub_enabled()) {
        FIXME("stub\n");
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!pcbBuffer || !ppBuffer || (dwFlags & ~CRYPT_ALL_PROVIDERS) != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    block = (struct resolved_refs_alloc *)calloc(1, sizeof(*block));
    if (!block) {
        return STATUS_INVALID_PARAMETER;
    }
    block->ref.dwInterface = dwInterface ? dwInterface : BCRYPT_CIPHER_INTERFACE;
    block->ref.pszFunction = pszFunction ? pszFunction : BCRYPT_AES_ALG_ID_STR;
    block->ref.pszProvider = MS_PRIMITIVE_PROVIDER_STR;
    block->ptrs[0] = &block->ref;
    block->refs.cProviders = 1U;
    block->refs.rgpProviders = block->ptrs;
    *pcbBuffer = (ULONG)sizeof(*block);
    *ppBuffer = &block->refs;
    return STATUS_SUCCESS;
}

static BCRYPT_CIPHER_FUNCTION_TABLE s_cipher_table = {
    { 1U, 0U },
    BCryptOpenAlgorithmProvider,
    BCryptGetProperty,
    BCryptSetProperty,
    BCryptCloseAlgorithmProvider,
    BCryptGenerateSymmetricKey,
    BCryptEncrypt,
    BCryptDecrypt,
    BCryptImportKey,
    BCryptExportKey,
    BCryptDuplicateKey,
    BCryptDestroyKey
};

static BCRYPT_HASH_FUNCTION_TABLE s_hash_table = {
    { 1U, 0U },
    BCryptOpenAlgorithmProvider,
    BCryptGetProperty,
    BCryptSetProperty,
    BCryptCloseAlgorithmProvider,
    BCryptCreateHash,
    BCryptHashData,
    BCryptFinishHash,
    BCryptDuplicateHash,
    BCryptDestroyHash
};

static BCRYPT_RNG_FUNCTION_TABLE s_rng_table = {
    { 1U, 0U },
    BCryptOpenAlgorithmProvider,
    BCryptGetProperty,
    BCryptSetProperty,
    BCryptCloseAlgorithmProvider,
    BCryptGenRandom
};

NTSTATUS WINAPI GetCipherInterface(
    const char *pszProviderName,
    BCRYPT_CIPHER_FUNCTION_TABLE **ppFunctionTable,
    ULONG dwFlags)
{
    (void)pszProviderName;
    if (is_simulate_stub_enabled()) {
        FIXME("stub\n");
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!ppFunctionTable || dwFlags != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    *ppFunctionTable = &s_cipher_table;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI GetHashInterface(
    const char *pszProviderName,
    BCRYPT_HASH_FUNCTION_TABLE **ppFunctionTable,
    ULONG dwFlags)
{
    (void)pszProviderName;
    if (is_simulate_stub_enabled()) {
        FIXME("stub\n");
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!ppFunctionTable || dwFlags != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    *ppFunctionTable = &s_hash_table;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI GetRngInterface(
    const char *pszProviderName,
    BCRYPT_RNG_FUNCTION_TABLE **ppFunctionTable,
    ULONG dwFlags)
{
    (void)pszProviderName;
    if (is_simulate_stub_enabled()) {
        FIXME("stub\n");
        return STATUS_NOT_IMPLEMENTED;
    }
    if (!ppFunctionTable || dwFlags != 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    *ppFunctionTable = &s_rng_table;
    return STATUS_SUCCESS;
}
