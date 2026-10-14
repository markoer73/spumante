/*
 * Wine BCrypt (Cryptography Next Generation) Implementation (macOS First, Linux Second)
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __APPLE__
#include <CommonCrypto/CommonCryptor.h>
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

#define BCRYPT_ALG_MAGIC 0x414C4731U
#define BCRYPT_KEY_MAGIC 0x4B455931U

struct bcrypt_alg_object {
    uint32_t magic;
    uint32_t alg_id;
};

struct bcrypt_key_object {
    uint32_t magic;
    uint32_t heap_allocated;
    uint32_t secret_len;
    uint8_t secret[32];
};

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

NTSTATUS WINAPI BCryptOpenAlgorithmProvider(
    BCRYPT_ALG_HANDLE *phAlgorithm,
    const char *pszAlgId,
    const char *pszImplementation,
    ULONG dwFlags)
{
    struct bcrypt_alg_object *alg;
    (void)pszImplementation;

    TRACE("(%p, %s, %p, %08x)\n", phAlgorithm, pszAlgId ? pszAlgId : "(null)", pszImplementation, dwFlags);

    if (!phAlgorithm || !pszAlgId || dwFlags != 0) {
        return STATUS_INVALID_PARAMETER;
    }
    if (strcmp(pszAlgId, BCRYPT_AES_ALG_ID_STR) != 0) {
        return STATUS_INVALID_PARAMETER;
    }
    alg = (struct bcrypt_alg_object *)calloc(1, sizeof(*alg));
    if (!alg) {
        return STATUS_INVALID_PARAMETER;
    }
    alg->magic = BCRYPT_ALG_MAGIC;
    alg->alg_id = 1;
    *phAlgorithm = (BCRYPT_ALG_HANDLE)alg;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE hAlgorithm, ULONG dwFlags)
{
    struct bcrypt_alg_object *alg = (struct bcrypt_alg_object *)hAlgorithm;
    if (!alg || alg->magic != BCRYPT_ALG_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    if (dwFlags != 0) {
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
    if (!hObject) {
        return STATUS_INVALID_HANDLE;
    }
    if (!pszProperty || !pcbResult || dwFlags != 0) {
        return STATUS_INVALID_PARAMETER;
    }
    if (strcmp(pszProperty, BCRYPT_OBJECT_LENGTH_STR) == 0) {
        val = BCRYPT_OBJECT_LENGTH_BYTES;
    } else if (strcmp(pszProperty, BCRYPT_BLOCK_LENGTH_STR) == 0) {
        val = BCRYPT_BLOCK_LENGTH_BYTES;
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

NTSTATUS WINAPI BCryptGenerateSymmetricKey(
    BCRYPT_ALG_HANDLE hAlgorithm,
    BCRYPT_KEY_HANDLE *phKey,
    PUCHAR pbKeyObject,
    ULONG cbKeyObject,
    PUCHAR pbSecret,
    ULONG cbSecret,
    ULONG dwFlags)
{
    struct bcrypt_alg_object *alg = (struct bcrypt_alg_object *)hAlgorithm;
    struct bcrypt_key_object *key_obj = NULL;
    const char *sim_stub = getenv("WINE_PARITY_SIMULATE_STUB");

    TRACE("(%p, %p, %p, %u, %p, %u, %08x)\n",
          hAlgorithm, phKey, pbKeyObject, cbKeyObject, pbSecret, cbSecret, dwFlags);

    if (sim_stub && strcmp(sim_stub, "1") == 0) {
        FIXME("(%p, %p, %p, %u, %p, %u, %08x): stub\n",
              hAlgorithm, phKey, pbKeyObject, cbKeyObject, pbSecret, cbSecret, dwFlags);
        return STATUS_NOT_IMPLEMENTED;
    }

    /* Windows 11 parameter validation precedence */
    if (!alg || alg->magic != BCRYPT_ALG_MAGIC) {
        WARN("Invalid algorithm handle %p\n", hAlgorithm);
        return STATUS_INVALID_HANDLE;
    }
    if (!phKey) {
        return STATUS_INVALID_PARAMETER;
    }
    *phKey = NULL;

    if (!pbSecret || dwFlags != 0) {
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
        key_obj = (struct bcrypt_key_object *)calloc(1, BCRYPT_OBJECT_LENGTH_BYTES);
        if (!key_obj) {
            return STATUS_INVALID_PARAMETER;
        }
        key_obj->heap_allocated = 1;
    }

    key_obj->magic = BCRYPT_KEY_MAGIC;
    key_obj->secret_len = cbSecret;
    memcpy(key_obj->secret, pbSecret, cbSecret);
    *phKey = (BCRYPT_KEY_HANDLE)key_obj;
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
    (void)pPaddingInfo;
    (void)pbIV;
    (void)cbIV;

    if (!key_obj || key_obj->magic != BCRYPT_KEY_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    if (!pbInput || !pcbResult || dwFlags != 0 || (cbInput % 16U) != 0U || cbInput == 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    *pcbResult = cbInput;
    if (!pbOutput) {
        return STATUS_SUCCESS;
    }
    if (cbOutput < cbInput) {
        return STATUS_BUFFER_TOO_SMALL;
    }

#ifdef __APPLE__
    {
        size_t data_moved = 0;
        CCCryptorStatus cc_status = CCCrypt(
            kCCEncrypt,
            kCCAlgorithmAES,
            kCCOptionECBMode,
            key_obj->secret,
            key_obj->secret_len,
            NULL,
            pbInput,
            cbInput,
            pbOutput,
            cbOutput,
            &data_moved);
        if (cc_status != kCCSuccess) {
            return STATUS_INVALID_PARAMETER;
        }
    }
#else
    {
        uint8_t rk[240];
        uint32_t nr = 0;
        ULONG offset;
        aes_expand_key(key_obj->secret, key_obj->secret_len, rk, &nr);
        for (offset = 0; offset < cbInput; offset += 16U) {
            aes_encrypt_block(rk, nr, pbInput + offset, pbOutput + offset);
        }
    }
#endif
    return STATUS_SUCCESS;
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
    (void)pPaddingInfo;
    (void)pbIV;
    (void)cbIV;

    if (!key_obj || key_obj->magic != BCRYPT_KEY_MAGIC) {
        return STATUS_INVALID_HANDLE;
    }
    if (!pbInput || !pcbResult || dwFlags != 0 || (cbInput % 16U) != 0U || cbInput == 0U) {
        return STATUS_INVALID_PARAMETER;
    }
    *pcbResult = cbInput;
    if (!pbOutput) {
        return STATUS_SUCCESS;
    }
    if (cbOutput < cbInput) {
        return STATUS_BUFFER_TOO_SMALL;
    }

#ifdef __APPLE__
    {
        size_t data_moved = 0;
        CCCryptorStatus cc_status = CCCrypt(
            kCCDecrypt,
            kCCAlgorithmAES,
            kCCOptionECBMode,
            key_obj->secret,
            key_obj->secret_len,
            NULL,
            pbInput,
            cbInput,
            pbOutput,
            cbOutput,
            &data_moved);
        if (cc_status != kCCSuccess) {
            return STATUS_INVALID_PARAMETER;
        }
    }
#else
    {
        uint8_t rk[240];
        uint32_t nr = 0;
        ULONG offset;
        aes_expand_key(key_obj->secret, key_obj->secret_len, rk, &nr);
        for (offset = 0; offset < cbInput; offset += 16U) {
            aes_decrypt_block(rk, nr, pbInput + offset, pbOutput + offset);
        }
    }
#endif
    return STATUS_SUCCESS;
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
    if (hExportKey != NULL || !pszBlobType || !pcbResult || dwFlags != 0) {
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
    (void)hKey;
    (void)phNewKey;
    (void)pbKeyObject;
    (void)cbKeyObject;
    (void)dwFlags;
    FIXME("(%p, %p, %p, %u, %08x): stub\n", hKey, phNewKey, pbKeyObject, cbKeyObject, dwFlags);
    return STATUS_NOT_IMPLEMENTED;
}
