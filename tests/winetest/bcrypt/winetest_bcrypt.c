/*
 * Upstream Wine winetest anti-regression suite for bcrypt.dll
 *
 * Adapted from upstream Wine dlls/bcrypt/tests/bcrypt.c
 * Copyright 2014 Bruno Jesus
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../include/wine/test.h"
#include "../../../dlls/bcrypt/bcrypt.h"

static void format_hash_hex(const UCHAR *bytes, ULONG size, char *buf, size_t buf_cap)
{
    parity_hex_encode(bytes, size, buf, buf_cap);
}

#define test_object_length(a) _test_object_length(__LINE__, a)
static void _test_object_length(unsigned line, void *handle)
{
    NTSTATUS status;
    ULONG len, size;

    len = size = 0xdeadbeefU;
    status = BCryptGetProperty(NULL, BCRYPT_OBJECT_LENGTH, (UCHAR *)&len, sizeof(len), &size, 0);
    ok_(__FILE__, line)(status == STATUS_INVALID_HANDLE, "BCryptGetProperty failed: 0x%08x\n", status);

    len = size = 0xdeadbeefU;
    status = BCryptGetProperty(handle, NULL, (UCHAR *)&len, sizeof(len), &size, 0);
    ok_(__FILE__, line)(status == STATUS_INVALID_PARAMETER, "BCryptGetProperty failed: 0x%08x\n", status);

    len = size = 0xdeadbeefU;
    status = BCryptGetProperty(handle, BCRYPT_OBJECT_LENGTH, (UCHAR *)&len, sizeof(len), NULL, 0);
    ok_(__FILE__, line)(status == STATUS_INVALID_PARAMETER, "BCryptGetProperty failed: 0x%08x\n", status);

    len = size = 0xdeadbeefU;
    status = BCryptGetProperty(handle, BCRYPT_OBJECT_LENGTH, NULL, sizeof(len), &size, 0);
    ok_(__FILE__, line)(status == STATUS_SUCCESS, "BCryptGetProperty failed: 0x%08x\n", status);
    ok_(__FILE__, line)(size == sizeof(len), "got %u\n", (unsigned int)size);

    len = size = 0xdeadbeefU;
    status = BCryptGetProperty(handle, BCRYPT_OBJECT_LENGTH, (UCHAR *)&len, 0, &size, 0);
    ok_(__FILE__, line)(status == STATUS_BUFFER_TOO_SMALL, "BCryptGetProperty failed: 0x%08x\n", status);
    ok_(__FILE__, line)(len == 0xdeadbeefU, "got %u\n", (unsigned int)len);
    ok_(__FILE__, line)(size == sizeof(len), "got %u\n", (unsigned int)size);

    len = size = 0xdeadbeefU;
    status = BCryptGetProperty(handle, BCRYPT_OBJECT_LENGTH, (UCHAR *)&len, sizeof(len), &size, 0);
    ok_(__FILE__, line)(status == STATUS_SUCCESS, "BCryptGetProperty failed: 0x%08x\n", status);
    ok_(__FILE__, line)(len != 0xdeadbeefU, "len not set\n");
    ok_(__FILE__, line)(size == sizeof(len), "got %u\n", (unsigned int)size);
}

#define test_hash_length(a, b) _test_hash_length(__LINE__, a, b)
static void _test_hash_length(unsigned line, void *handle, ULONG exlen)
{
    ULONG len = 0xdeadbeefU, size = 0xdeadbeefU;
    NTSTATUS status;

    status = BCryptGetProperty(handle, BCRYPT_HASH_LENGTH, (UCHAR *)&len, sizeof(len), &size, 0);
    ok_(__FILE__, line)(status == STATUS_SUCCESS, "BCryptGetProperty failed: 0x%08x\n", status);
    ok_(__FILE__, line)(size == sizeof(len), "got %u\n", (unsigned int)size);
    ok_(__FILE__, line)(len == exlen, "len = %u, expected %u\n", (unsigned int)len, (unsigned int)exlen);
}

#define test_hash_block_length(a, b) _test_hash_block_length(__LINE__, a, b)
static void _test_hash_block_length(unsigned line, void *handle, ULONG exlen)
{
    ULONG len = 0xdeadbeefU, size = 0xdeadbeefU;
    NTSTATUS status;

    status = BCryptGetProperty(handle, BCRYPT_HASH_BLOCK_LENGTH, (UCHAR *)&len, sizeof(len), &size, 0);
    ok_(__FILE__, line)(status == STATUS_SUCCESS, "BCryptGetProperty failed: 0x%08x\n", status);
    ok_(__FILE__, line)(size == sizeof(len), "got %u\n", (unsigned int)size);
    ok_(__FILE__, line)(len == exlen, "len = %u, expected %u\n", (unsigned int)len, (unsigned int)exlen);
}

static void test_BCryptGenRandom(void)
{
    NTSTATUS ret;
    UCHAR buffer[256];

    ret = BCryptGenRandom(NULL, NULL, 0, 0);
    ok(ret == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE, got 0x%08x\n", ret);
    ret = BCryptGenRandom(NULL, buffer, 0, 0);
    ok(ret == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE, got 0x%08x\n", ret);
    ret = BCryptGenRandom(NULL, buffer, sizeof(buffer), 0);
    ok(ret == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE, got 0x%08x\n", ret);
    ret = BCryptGenRandom(NULL, buffer, sizeof(buffer), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    ok(ret == STATUS_SUCCESS, "Expected success, got 0x%08x\n", ret);
    ret = BCryptGenRandom(NULL, buffer, sizeof(buffer),
                          BCRYPT_USE_SYSTEM_PREFERRED_RNG | BCRYPT_RNG_USE_ENTROPY_IN_BUFFER);
    ok(ret == STATUS_SUCCESS, "Expected success, got 0x%08x\n", ret);
    ret = BCryptGenRandom(NULL, NULL, sizeof(buffer), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    ok(ret == STATUS_INVALID_PARAMETER, "Expected STATUS_INVALID_PARAMETER, got 0x%08x\n", ret);

    /* Zero sized buffer should work too */
    ret = BCryptGenRandom(NULL, buffer, 0, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    ok(ret == STATUS_SUCCESS, "Expected success for zero-sized buffer, got 0x%08x\n", ret);

    memset(buffer, 0, 16);
    ret = BCryptGenRandom(NULL, buffer, 8, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    ok(ret == STATUS_SUCCESS, "Expected success, got 0x%08x\n", ret);
    ok(memcmp(buffer, buffer + 8, 8) != 0, "Expected random bytes, got all zeros\n");

    ret = BCryptGenRandom(BCRYPT_RNG_ALG_HANDLE, buffer, sizeof(buffer), 0);
    ok(ret == STATUS_SUCCESS, "Expected success on BCRYPT_RNG_ALG_HANDLE, got 0x%08x\n", ret);

    ret = BCryptCloseAlgorithmProvider(BCRYPT_RNG_ALG_HANDLE, 0);
    ok(ret == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE closing pseudo-handle, got 0x%08x\n", ret);
}

static void test_rng(void)
{
    BCRYPT_ALG_HANDLE alg = NULL;
    ULONG size, len;
    UCHAR buf[16];
    NTSTATUS ret;

    ret = BCryptOpenAlgorithmProvider(&alg, BCRYPT_RNG_ALGORITHM, MS_PRIMITIVE_PROVIDER, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(alg != NULL, "alg not set\n");

    len = size = 0xdeadbeefU;
    ret = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (UCHAR *)&len, sizeof(len), &size, 0);
    ok(ret == STATUS_NOT_SUPPORTED, "got 0x%08x\n", ret);

    len = size = 0xdeadbeefU;
    ret = BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (UCHAR *)&len, sizeof(len), &size, 0);
    ok(ret == STATUS_NOT_SUPPORTED, "got 0x%08x\n", ret);

    memset(buf, 0, sizeof(buf));
    ret = BCryptGenRandom(alg, buf, 8, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(memcmp(buf, buf + 8, 8) != 0, "got zeroes\n");

    ret = BCryptCloseAlgorithmProvider(alg, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
}

static void test_hashes(void)
{
    static const char expected_abc[] = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL, dup_hash = NULL;
    UCHAR digest[32], dup_digest[32];
    char hex_str[68];
    NTSTATUS ret;

    ret = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, MS_PRIMITIVE_PROVIDER, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(alg != NULL, "alg not set\n");

    test_object_length(alg);
    test_hash_length(alg, 32);
    test_hash_block_length(alg, 64);

    ret = BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(hash != NULL, "hash not set\n");

    ret = BCryptHashData(hash, (UCHAR *)"ab", 2, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);

    ret = BCryptDuplicateHash(hash, &dup_hash, NULL, 0, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(dup_hash != NULL, "dup_hash not set\n");

    ret = BCryptHashData(hash, (UCHAR *)"c", 1, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ret = BCryptFinishHash(hash, digest, sizeof(digest), 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    format_hash_hex(digest, sizeof(digest), hex_str, sizeof(hex_str));
    ok(!strcmp(hex_str, expected_abc), "got %s\n", hex_str);

    /* Non-reusable hash cannot be finished a second time */
    ret = BCryptFinishHash(hash, digest, sizeof(digest), 0);
    ok(ret == STATUS_INVALID_PARAMETER, "got 0x%08x\n", ret);

    ret = BCryptHashData(dup_hash, (UCHAR *)"c", 1, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ret = BCryptFinishHash(dup_hash, dup_digest, sizeof(dup_digest), 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(!memcmp(digest, dup_digest, sizeof(digest)), "duplicated hash digest mismatch\n");

    ret = BCryptDestroyHash(dup_hash);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ret = BCryptDestroyHash(hash);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ret = BCryptCloseAlgorithmProvider(alg, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
}

static void test_BcryptHash(void)
{
    static const char expected_abc[] = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    static const char expected_hmac[] = "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8";
    static const char msg[] = "The quick brown fox jumps over the lazy dog";
    BCRYPT_ALG_HANDLE alg = NULL;
    UCHAR sha256[32], hmac[32];
    char str[68];
    NTSTATUS ret;

    ret = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, MS_PRIMITIVE_PROVIDER, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);

    memset(sha256, 0, sizeof(sha256));
    ret = BCryptHash(alg, NULL, 0, (UCHAR *)"abc", 3, sha256, sizeof(sha256));
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    format_hash_hex(sha256, sizeof(sha256), str, sizeof(str));
    ok(!strcmp(str, expected_abc), "got %s\n", str);

    /* With a secret but without BCRYPT_ALG_HANDLE_HMAC_FLAG */
    ret = BCryptHash(alg, (UCHAR *)"key", 3, (UCHAR *)"abc", 3, sha256, sizeof(sha256));
    ok(ret == STATUS_INVALID_PARAMETER, "got 0x%08x\n", ret);

    ret = BCryptCloseAlgorithmProvider(alg, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);

    /* Pseudo-handle one-shot SHA256 and HMAC-SHA256 */
    memset(sha256, 0, sizeof(sha256));
    ret = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, NULL, 0, (UCHAR *)"abc", 3, sha256, sizeof(sha256));
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    format_hash_hex(sha256, sizeof(sha256), str, sizeof(str));
    ok(!strcmp(str, expected_abc), "got %s\n", str);

    memset(hmac, 0, sizeof(hmac));
    ret = BCryptHash(BCRYPT_HMAC_SHA256_ALG_HANDLE, (UCHAR *)"key", 3,
                     (UCHAR *)msg, (ULONG)strlen(msg), hmac, sizeof(hmac));
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    format_hash_hex(hmac, sizeof(hmac), str, sizeof(str));
    ok(!strcmp(str, expected_hmac), "got %s\n", str);
}

static void test_BcryptDeriveKeyPBKDF2(void)
{
    static const char expected_c1[] = "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b";
    static const char expected_c2[] = "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43";
    BCRYPT_ALG_HANDLE alg = NULL;
    UCHAR dk[32];
    char str[68];
    NTSTATUS ret;

    ret = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, MS_PRIMITIVE_PROVIDER,
                                      BCRYPT_ALG_HANDLE_HMAC_FLAG);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(alg != NULL, "alg not set\n");

    test_hash_length(alg, 32);

    /* Zero iterations must return STATUS_INVALID_PARAMETER */
    ret = BCryptDeriveKeyPBKDF2(alg, (UCHAR *)"password", 8, (UCHAR *)"salt", 4, 0, dk, sizeof(dk), 0);
    ok(ret == STATUS_INVALID_PARAMETER, "got 0x%08x\n", ret);

    memset(dk, 0, sizeof(dk));
    ret = BCryptDeriveKeyPBKDF2(alg, (UCHAR *)"password", 8, (UCHAR *)"salt", 4, 1, dk, sizeof(dk), 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    format_hash_hex(dk, sizeof(dk), str, sizeof(str));
    ok(!strcmp(str, expected_c1), "got %s\n", str);

    memset(dk, 0, sizeof(dk));
    ret = BCryptDeriveKeyPBKDF2(BCRYPT_HMAC_SHA256_ALG_HANDLE, (UCHAR *)"password", 8,
                                (UCHAR *)"salt", 4, 2, dk, sizeof(dk), 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    format_hash_hex(dk, sizeof(dk), str, sizeof(str));
    ok(!strcmp(str, expected_c2), "got %s\n", str);

    ret = BCryptCloseAlgorithmProvider(alg, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
}

static void test_aes(void)
{
    BCRYPT_KEY_LENGTHS_STRUCT key_lengths;
    BCRYPT_ALG_HANDLE alg = NULL;
    ULONG size, len;
    UCHAR mode[64];
    NTSTATUS ret;

    ret = BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, MS_PRIMITIVE_PROVIDER, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(alg != NULL, "alg not set\n");

    len = size = 0;
    ret = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (UCHAR *)&len, sizeof(len), &size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(len > 0, "expected non-zero len\n");
    ok(size == sizeof(len), "got %u\n", (unsigned int)size);

    len = size = 0;
    ret = BCryptGetProperty(alg, BCRYPT_BLOCK_LENGTH, (UCHAR *)&len, sizeof(len), &size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(len == 16, "got %u\n", (unsigned int)len);
    ok(size == sizeof(len), "got %u\n", (unsigned int)size);

    size = 0;
    memset(&key_lengths, 0, sizeof(key_lengths));
    ret = BCryptGetProperty(alg, BCRYPT_KEY_LENGTHS, (UCHAR *)&key_lengths, sizeof(key_lengths), &size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(size == sizeof(key_lengths), "got %u\n", (unsigned int)size);
    ok(key_lengths.dwMinLength == 128, "Expected 128, got %u\n", (unsigned int)key_lengths.dwMinLength);
    ok(key_lengths.dwMaxLength == 256, "Expected 256, got %u\n", (unsigned int)key_lengths.dwMaxLength);
    ok(key_lengths.dwIncrement == 64, "Expected 64, got %u\n", (unsigned int)key_lengths.dwIncrement);

    ret = BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (UCHAR *)BCRYPT_CHAIN_MODE_GCM,
                            sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);

    size = 0;
    memset(mode, 0, sizeof(mode));
    ret = BCryptGetProperty(alg, BCRYPT_CHAINING_MODE, mode, sizeof(mode), &size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(!strcmp((const char *)mode, BCRYPT_CHAIN_MODE_GCM), "got %s\n", (const char *)mode);

    /* Pseudo-handle chaining mode is read-only and returns STATUS_ACCESS_DENIED on SetProperty */
    ret = BCryptSetProperty(BCRYPT_AES_CBC_ALG_HANDLE, BCRYPT_CHAINING_MODE,
                            (UCHAR *)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    ok(ret == STATUS_ACCESS_DENIED, "got 0x%08x\n", ret);

    ret = BCryptCloseAlgorithmProvider(alg, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
}

static void test_BCryptGenerateSymmetricKey(void)
{
    static UCHAR secret[] =
        {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
    static UCHAR iv[] =
        {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
    static UCHAR data[] =
        {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
    static const UCHAR expected[] =
        {0xc6,0xa1,0x3b,0x37,0x87,0x8f,0x5b,0x82,0x6f,0x4f,0x81,0x62,0xa1,0xc8,0xd8,0x79};
    static const UCHAR expected2[] =
        {0xb5,0x8a,0x10,0x64,0xd8,0xac,0xa9,0x9b,0xd9,0xb0,0x40,0x5b,0x85,0x45,0xf5,0xbb};
    static const UCHAR expected3[] =
        {0xe3,0x7c,0xd3,0x63,0xdd,0x7c,0x87,0xa0,0x9a,0xff,0x0e,0x3e,0x60,0xe0,0x9c,0x82};
    BCRYPT_ALG_HANDLE aes = NULL;
    BCRYPT_KEY_HANDLE key = NULL, key2 = NULL;
    UCHAR *buf, ciphertext[16], plaintext[16], ivbuf[16], mode[64];
    ULONG size = 0, len = 0;
    DWORD keylen = 0;
    NTSTATUS ret;

    ret = BCryptOpenAlgorithmProvider(&aes, BCRYPT_AES_ALGORITHM, NULL, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);

    ret = BCryptGetProperty(aes, BCRYPT_OBJECT_LENGTH, (UCHAR *)&len, sizeof(len), &size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);

    key = (void *)(uintptr_t)0xdeadbeefU;
    ret = BCryptGenerateSymmetricKey(NULL, &key, NULL, 0, secret, sizeof(secret), 0);
    ok(ret == STATUS_INVALID_HANDLE, "got 0x%08x\n", ret);
    ok(key == (void *)(uintptr_t)0xdeadbeefU, "got %p\n", key);

    buf = (UCHAR *)calloc(1, len);
    key = (BCRYPT_KEY_HANDLE)(uintptr_t)0xdeadbeefU;
    ret = BCryptGenerateSymmetricKey(aes, &key, buf, len, secret, 1, 0);
    ok(ret == STATUS_INVALID_PARAMETER, "got 0x%08x\n", ret);
    ok(key == (BCRYPT_KEY_HANDLE)(uintptr_t)0xdeadbeefU, "got %p\n", key);

    key = NULL;
    ret = BCryptGenerateSymmetricKey(aes, &key, buf, len, secret, sizeof(secret), 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(key != NULL, "key not set\n");

    keylen = 0;
    ret = BCryptGetProperty(key, BCRYPT_KEY_STRENGTH, (UCHAR *)&keylen, sizeof(keylen), &size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(size == sizeof(keylen), "got %u\n", (unsigned int)size);
    ok(keylen == 128, "got %u\n", (unsigned int)keylen);

    todo_wine
    {
        keylen = 512;
        ret = BCryptSetProperty(aes, BCRYPT_KEY_LENGTH, (UCHAR *)&keylen, sizeof(keylen), 0);
        ok(ret == STATUS_NOT_SUPPORTED, "got 0x%08x\n", ret);
    }

    size = 0;
    memset(mode, 0, sizeof(mode));
    ret = BCryptGetProperty(key, BCRYPT_CHAINING_MODE, mode, sizeof(mode), &size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(!strcmp((const char *)mode, BCRYPT_CHAIN_MODE_CBC), "got %s\n", (const char *)mode);

    size = 0xdeadbeefU;
    ret = BCryptEncrypt(key, NULL, 0, NULL, NULL, 0, NULL, 0, &size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(size == 0, "got %u\n", (unsigned int)size);

    size = 0;
    memcpy(ivbuf, iv, sizeof(iv));
    ret = BCryptEncrypt(key, data, 16, NULL, ivbuf, 16, NULL, 0, &size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(size == 16, "got %u\n", (unsigned int)size);
    ok(!memcmp(ivbuf, iv, sizeof(iv)), "wrong iv data\n");

    size = 0;
    memcpy(ivbuf, iv, sizeof(iv));
    memset(ciphertext, 0, sizeof(ciphertext));
    ret = BCryptEncrypt(key, data, 16, NULL, ivbuf, 16, ciphertext, 16, &size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(size == 16, "got %u\n", (unsigned int)size);
    ok(!memcmp(ciphertext, expected, sizeof(expected)), "wrong ciphertext\n");
    ok(!memcmp(ivbuf, ciphertext, sizeof(iv)), "ivbuf not updated to ciphertext\n");

    /* NULL iv uses key's internal chaining vector updated by prior encrypt */
    size = 0;
    memset(ciphertext, 0, sizeof(ciphertext));
    ret = BCryptEncrypt(key, data, 16, NULL, NULL, 0, ciphertext, 16, &size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(size == 16, "got %u\n", (unsigned int)size);
    ok(!memcmp(ciphertext, expected2, sizeof(expected2)), "wrong ciphertext for NULL iv\n");

    size = 0;
    memcpy(ivbuf, iv, sizeof(iv));
    ++ivbuf[0];
    memset(ciphertext, 0, sizeof(ciphertext));
    ret = BCryptEncrypt(key, data, 16, NULL, ivbuf, 16, ciphertext, 16, &size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(size == 16, "got %u\n", (unsigned int)size);
    ok(!memcmp(ciphertext, expected3, sizeof(expected3)), "wrong ciphertext for incremented iv\n");

    /* BCryptDuplicateKey */
    key2 = (void *)(uintptr_t)0xdeadbeefU;
    ret = BCryptDuplicateKey(NULL, &key2, NULL, 0, 0);
    ok(ret == STATUS_INVALID_HANDLE, "got 0x%08x\n", ret);
    ok(key2 == (void *)(uintptr_t)0xdeadbeefU, "got %p\n", key2);

    key2 = NULL;
    ret = BCryptDuplicateKey(key, &key2, NULL, 0, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(key2 != NULL, "key2 not set\n");

    memcpy(ivbuf, iv, sizeof(iv));
    memset(ciphertext, 0, sizeof(ciphertext));
    ret = BCryptEncrypt(key2, data, 16, NULL, ivbuf, 16, ciphertext, 16, &size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(!memcmp(ciphertext, expected, sizeof(expected)), "duplicated key wrong ciphertext\n");
    ret = BCryptDestroyKey(key2);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);

    memcpy(ivbuf, iv, sizeof(iv));
    memset(plaintext, 0, sizeof(plaintext));
    ret = BCryptDecrypt(key, ciphertext, 16, NULL, ivbuf, 16, plaintext, 16, &size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(size == 16, "got %u\n", (unsigned int)size);
    ok(!memcmp(plaintext, data, sizeof(data)), "wrong decrypted data\n");

    ret = BCryptDestroyKey(key);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    free(buf);
    ret = BCryptCloseAlgorithmProvider(aes, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
}

static void test_BCryptEncrypt(void)
{
    static UCHAR secret[] =
        {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
    static UCHAR iv[] =
        {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
    static UCHAR data17[] =
        {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10};
    static const UCHAR expected_pad17[] =
        {0xc6,0xa1,0x3b,0x37,0x87,0x8f,0x5b,0x82,0x6f,0x4f,0x81,0x62,0xa1,0xc8,0xd8,0x79,
         0x28,0x73,0x3d,0xef,0x84,0x8f,0xb0,0xa6,0x5d,0x1a,0x51,0xb7,0xec,0x8f,0xea,0xe9};
    static const UCHAR expected_zero_iv[] =
        {0x0a,0x94,0x0b,0xb5,0x41,0x6e,0xf0,0x45,0xf1,0xc3,0x94,0x58,0xc6,0x53,0xea,0x5a};
    BCRYPT_ALG_HANDLE aes = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    UCHAR ciphertext[48], ivbuf[16];
    ULONG size = 0;
    NTSTATUS ret;

    ret = BCryptOpenAlgorithmProvider(&aes, BCRYPT_AES_ALGORITHM, NULL, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ret = BCryptGenerateSymmetricKey(aes, &key, NULL, 0, secret, sizeof(secret), 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);

    /* All-zero IV */
    memset(ivbuf, 0, sizeof(ivbuf));
    memset(ciphertext, 0, sizeof(ciphertext));
    ret = BCryptEncrypt(key, data17, 16, NULL, ivbuf, 16, ciphertext, 16, &size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(size == 16, "got %u\n", (unsigned int)size);
    ok(!memcmp(ciphertext, expected_zero_iv, 16), "wrong zero-iv ciphertext\n");

    /* Input size not a multiple of block size without BCRYPT_BLOCK_PADDING */
    size = 0;
    memcpy(ivbuf, iv, sizeof(iv));
    ret = BCryptEncrypt(key, data17, 17, NULL, ivbuf, 16, NULL, 0, &size, 0);
    ok(ret == STATUS_INVALID_BUFFER_SIZE, "got 0x%08x\n", ret);
    ok(size == 17, "got %u\n", (unsigned int)size);

    /* Input size not a multiple of block size WITH BCRYPT_BLOCK_PADDING */
    size = 0;
    memcpy(ivbuf, iv, sizeof(iv));
    ret = BCryptEncrypt(key, data17, 17, NULL, ivbuf, 16, NULL, 0, &size, BCRYPT_BLOCK_PADDING);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(size == 32, "got %u\n", (unsigned int)size);

    size = 0;
    memcpy(ivbuf, iv, sizeof(iv));
    ret = BCryptEncrypt(key, data17, 17, NULL, ivbuf, 16, ciphertext, 16, &size, BCRYPT_BLOCK_PADDING);
    ok(ret == STATUS_BUFFER_TOO_SMALL, "got 0x%08x\n", ret);
    ok(size == 32, "got %u\n", (unsigned int)size);

    size = 0;
    memcpy(ivbuf, iv, sizeof(iv));
    memset(ciphertext, 0, sizeof(ciphertext));
    ret = BCryptEncrypt(key, data17, 17, NULL, ivbuf, 16, ciphertext, 32, &size, BCRYPT_BLOCK_PADDING);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(size == 32, "got %u\n", (unsigned int)size);
    ok(!memcmp(ciphertext, expected_pad17, 32), "wrong PKCS#7 padded ciphertext\n");

    ret = BCryptDestroyKey(key);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ret = BCryptCloseAlgorithmProvider(aes, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
}

static void test_BCryptDecrypt(void)
{
    static UCHAR secret[] =
        {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
    static UCHAR iv[] =
        {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
    static const UCHAR data17[] =
        {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10};
    static UCHAR cipher_pad17[] =
        {0xc6,0xa1,0x3b,0x37,0x87,0x8f,0x5b,0x82,0x6f,0x4f,0x81,0x62,0xa1,0xc8,0xd8,0x79,
         0x28,0x73,0x3d,0xef,0x84,0x8f,0xb0,0xa6,0x5d,0x1a,0x51,0xb7,0xec,0x8f,0xea,0xe9};
    BCRYPT_ALG_HANDLE aes = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    UCHAR plaintext[32], ivbuf[16];
    ULONG size = 0;
    NTSTATUS ret;

    ret = BCryptOpenAlgorithmProvider(&aes, BCRYPT_AES_ALGORITHM, NULL, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ret = BCryptGenerateSymmetricKey(aes, &key, NULL, 0, secret, sizeof(secret), 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);

    memcpy(ivbuf, iv, sizeof(iv));
    memset(plaintext, 0, sizeof(plaintext));
    ret = BCryptDecrypt(key, cipher_pad17, 32, NULL, ivbuf, 16, plaintext, 32, &size, BCRYPT_BLOCK_PADDING);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(size == 17, "got %u\n", (unsigned int)size);
    ok(!memcmp(plaintext, data17, 17), "wrong decrypted 17-byte payload\n");

    /* Unaligned ciphertext size must return STATUS_INVALID_BUFFER_SIZE */
    memcpy(ivbuf, iv, sizeof(iv));
    ret = BCryptDecrypt(key, cipher_pad17, 17, NULL, ivbuf, 16, plaintext, 32, &size, BCRYPT_BLOCK_PADDING);
    ok(ret == STATUS_INVALID_BUFFER_SIZE, "got 0x%08x\n", ret);

    ret = BCryptDestroyKey(key);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ret = BCryptCloseAlgorithmProvider(aes, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
}

static void test_key_import_export(void)
{
    static UCHAR secret[] =
        {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
    static UCHAR iv[] =
        {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
    static UCHAR data[] =
        {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
    static const UCHAR expected[] =
        {0xc6,0xa1,0x3b,0x37,0x87,0x8f,0x5b,0x82,0x6f,0x4f,0x81,0x62,0xa1,0xc8,0xd8,0x79};
    BCRYPT_ALG_HANDLE aes = NULL;
    BCRYPT_KEY_HANDLE key = NULL, imported_key = NULL;
    UCHAR exported[64], ciphertext[16], ivbuf[16];
    ULONG size = 0;
    NTSTATUS ret;

    ret = BCryptOpenAlgorithmProvider(&aes, BCRYPT_AES_ALGORITHM, NULL, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ret = BCryptGenerateSymmetricKey(aes, &key, NULL, 0, secret, sizeof(secret), 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);

    ret = BCryptExportKey(key, NULL, BCRYPT_KEY_DATA_BLOB, exported, sizeof(exported), &size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(size == sizeof(BCRYPT_KEY_DATA_BLOB_HEADER) + 16, "got %u\n", (unsigned int)size);

    ret = BCryptImportKey(aes, NULL, BCRYPT_KEY_DATA_BLOB, &imported_key, NULL, 0, exported, size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(imported_key != NULL, "imported_key not set\n");

    memcpy(ivbuf, iv, sizeof(iv));
    memset(ciphertext, 0, sizeof(ciphertext));
    ret = BCryptEncrypt(imported_key, data, 16, NULL, ivbuf, 16, ciphertext, 16, &size, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(!memcmp(ciphertext, expected, 16), "imported key ciphertext mismatch\n");

    ret = BCryptDestroyKey(imported_key);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ret = BCryptDestroyKey(key);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ret = BCryptCloseAlgorithmProvider(aes, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
}

static void test_RSA(void)
{
    BCRYPT_ALG_HANDLE rsa = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    UCHAR blob_buf[sizeof(BCRYPT_RSAKEY_BLOB) + 3 + 8 + 4 + 4];
    BCRYPT_RSAKEY_BLOB *rsa_hdr = (BCRYPT_RSAKEY_BLOB *)blob_buf;
    static const UCHAR rsa_exp[3] = { 0x01, 0x00, 0x01 };
    static const UCHAR rsa_mod[8] = { 0xff, 0xff, 0xff, 0xba, 0x00, 0x00, 0x01, 0x45 };
    static const UCHAR rsa_p1[4]  = { 0xff, 0xff, 0xff, 0xfb };
    static const UCHAR rsa_p2[4]  = { 0xff, 0xff, 0xff, 0xbf };
    static const UCHAR rsa_in[8]  = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    UCHAR rsa_cipher[8], rsa_plain[8];
    ULONG out_len = 0;
    NTSTATUS ret;

    memset(blob_buf, 0, sizeof(blob_buf));
    rsa_hdr->Magic = BCRYPT_RSAPRIVATE_MAGIC;
    rsa_hdr->BitLength = 64;
    rsa_hdr->cbPublicExp = 3;
    rsa_hdr->cbModulus = 8;
    rsa_hdr->cbPrime1 = 4;
    rsa_hdr->cbPrime2 = 4;
    memcpy(blob_buf + sizeof(BCRYPT_RSAKEY_BLOB), rsa_exp, 3);
    memcpy(blob_buf + sizeof(BCRYPT_RSAKEY_BLOB) + 3, rsa_mod, 8);
    memcpy(blob_buf + sizeof(BCRYPT_RSAKEY_BLOB) + 3 + 8, rsa_p1, 4);
    memcpy(blob_buf + sizeof(BCRYPT_RSAKEY_BLOB) + 3 + 8 + 4, rsa_p2, 4);

    ret = BCryptOpenAlgorithmProvider(&rsa, BCRYPT_RSA_ALGORITHM, NULL, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ret = BCryptImportKeyPair(rsa, NULL, BCRYPT_RSAPRIVATE_BLOB, &key, blob_buf, sizeof(blob_buf), 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);

    ret = BCryptEncrypt(key, (UCHAR *)rsa_in, 8, NULL, NULL, 0, rsa_cipher, 8, &out_len, BCRYPT_PAD_NONE);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(out_len == 8, "got %u\n", (unsigned int)out_len);

    ret = BCryptDecrypt(key, rsa_cipher, 8, NULL, NULL, 0, rsa_plain, 8, &out_len, BCRYPT_PAD_NONE);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(!memcmp(rsa_plain, rsa_in, 8), "RSA BCRYPT_PAD_NONE roundtrip mismatch\n");

    ret = BCryptDestroyKey(key);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ret = BCryptCloseAlgorithmProvider(rsa, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
}

static void test_ECDSA(void)
{
    BCRYPT_ALG_HANDLE ecdsa = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    UCHAR ecc_buf[sizeof(BCRYPT_ECCKEY_BLOB) + 96];
    BCRYPT_ECCKEY_BLOB *ecc_hdr = (BCRYPT_ECCKEY_BLOB *)ecc_buf;
    UCHAR digest[32], sig[64];
    ULONG sig_len = 0;
    NTSTATUS ret;

    memset(ecc_buf, 0x11, sizeof(ecc_buf));
    ecc_hdr->dwMagic = BCRYPT_ECDSA_PRIVATE_P256_MAGIC;
    ecc_hdr->cbKey = 32;
    memset(digest, 0x42, sizeof(digest));

    ret = BCryptOpenAlgorithmProvider(&ecdsa, BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ret = BCryptImportKeyPair(ecdsa, NULL, BCRYPT_ECCPRIVATE_BLOB, &key, ecc_buf, sizeof(ecc_buf), 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);

    ret = BCryptSignHash(key, NULL, digest, sizeof(digest), sig, sizeof(sig), &sig_len, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(sig_len == 64, "got %u\n", (unsigned int)sig_len);

    ret = BCryptVerifySignature(key, NULL, digest, sizeof(digest), sig, sig_len, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);

    digest[0] ^= 0xff;
    ret = BCryptVerifySignature(key, NULL, digest, sizeof(digest), sig, sig_len, 0);
    ok(ret == STATUS_INVALID_SIGNATURE, "got 0x%08x\n", ret);

    ret = BCryptDestroyKey(key);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ret = BCryptCloseAlgorithmProvider(ecdsa, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
}

static void test_providers(void)
{
    ULONG count = 0;
    BCRYPT_PROVIDER_NAME *provs = NULL;
    PCRYPT_PROVIDER_REFS refs = NULL;
    BCRYPT_CIPHER_FUNCTION_TABLE *cipher_tbl = NULL;
    BCRYPT_HASH_FUNCTION_TABLE *hash_tbl = NULL;
    BCRYPT_RNG_FUNCTION_TABLE *rng_tbl = NULL;
    NTSTATUS ret;

    count = 0;
    ret = BCryptEnumProviders(BCRYPT_AES_ALGORITHM, &count, &provs, 0);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(count == 1 && provs != NULL, "expected 1 provider\n");
    BCryptFreeBuffer(provs);

    count = 0;
    ret = BCryptResolveProviders(NULL, BCRYPT_CIPHER_INTERFACE, BCRYPT_AES_ALGORITHM,
                                 MS_PRIMITIVE_PROVIDER, CRYPT_UM, CRYPT_ALL_PROVIDERS, &count, &refs);
    ok(ret == STATUS_SUCCESS, "got 0x%08x\n", ret);
    ok(count >= sizeof(CRYPT_PROVIDER_REFS) && refs != NULL, "expected resolved provider\n");
    BCryptFreeBuffer(refs);

    ret = GetCipherInterface(MS_PRIMITIVE_PROVIDER, &cipher_tbl, 0);
    ok(ret == STATUS_SUCCESS && cipher_tbl != NULL && cipher_tbl->pfnEncrypt != NULL, "GetCipherInterface failed\n");
    ret = GetHashInterface(MS_PRIMITIVE_PROVIDER, &hash_tbl, 0);
    ok(ret == STATUS_SUCCESS && hash_tbl != NULL && hash_tbl->pfnCreateHash != NULL, "GetHashInterface failed\n");
    ret = GetRngInterface(MS_PRIMITIVE_PROVIDER, &rng_tbl, 0);
    ok(ret == STATUS_SUCCESS && rng_tbl != NULL && rng_tbl->pfnGenRandom != NULL, "GetRngInterface failed\n");
}

START_TEST(bcrypt)
{
    WINETEST_RUN_SUBTEST(test_BCryptGenRandom);
    WINETEST_RUN_SUBTEST(test_rng);
    WINETEST_RUN_SUBTEST(test_hashes);
    WINETEST_RUN_SUBTEST(test_BcryptHash);
    WINETEST_RUN_SUBTEST(test_BcryptDeriveKeyPBKDF2);
    WINETEST_RUN_SUBTEST(test_aes);
    WINETEST_RUN_SUBTEST(test_BCryptGenerateSymmetricKey);
    WINETEST_RUN_SUBTEST(test_BCryptEncrypt);
    WINETEST_RUN_SUBTEST(test_BCryptDecrypt);
    WINETEST_RUN_SUBTEST(test_key_import_export);
    WINETEST_RUN_SUBTEST(test_RSA);
    WINETEST_RUN_SUBTEST(test_ECDSA);
    WINETEST_RUN_SUBTEST(test_providers);
}
