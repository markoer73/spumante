/*
 * Differential Conformance Test for BCryptGenerateSymmetricKey
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../include/parity_test.h"
#include "../../../dlls/bcrypt/bcrypt.h"

int main(void)
{
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    UCHAR key_obj[BCRYPT_OBJECT_LENGTH_BYTES];
    UCHAR small_obj[8];
    ULONG out_len = 0;
    NTSTATUS status;

    /* NIST FIPS-197 AES-128 / AES-192 / AES-256 test vectors */
    static const UCHAR secret_128[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const UCHAR secret_192[24] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
    };
    static const UCHAR secret_256[32] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    static const UCHAR plaintext[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };
    UCHAR ciphertext[16];
    UCHAR decrypted[16];
    UCHAR exported_blob[64];

    parity_begin_document("bcrypt", "test_symmetric_key", "x86_64", "wine_macos");

    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALG_ID_STR, NULL, 0);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS opening AES provider, got 0x%08x\n", status);

    /* 1. Validation Precedence: NULL hAlgorithm returns STATUS_INVALID_HANDLE even if phKey is NULL */
    SetLastError(0);
    status = BCryptGenerateSymmetricKey(NULL, NULL, key_obj, sizeof(key_obj), (PUCHAR)secret_128, 16, 0);
    ok(status == STATUS_INVALID_HANDLE, "Expected STATUS_INVALID_HANDLE, got 0x%08x\n", status);
    parity_record_exact("err_null_alg", "InvalidHandle_NullAlgorithm", "BCryptGenerateSymmetricKey",
                        status, GetLastError(), 0, NULL, 0, 0);

    /* 2. NULL phKey returns STATUS_INVALID_PARAMETER */
    SetLastError(0);
    status = BCryptGenerateSymmetricKey(hAlg, NULL, key_obj, sizeof(key_obj), (PUCHAR)secret_128, 16, 0);
    ok(status == STATUS_INVALID_PARAMETER, "Expected STATUS_INVALID_PARAMETER, got 0x%08x\n", status);
    parity_record_exact("err_null_phkey", "InvalidParam_NullPhKey", "BCryptGenerateSymmetricKey",
                        status, GetLastError(), 0, NULL, 0, 0);

    /* 3. NULL pbSecret returns STATUS_INVALID_PARAMETER */
    SetLastError(0);
    hKey = NULL;
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, key_obj, sizeof(key_obj), NULL, 16, 0);
    ok(status == STATUS_INVALID_PARAMETER, "Expected STATUS_INVALID_PARAMETER, got 0x%08x\n", status);
    parity_record_exact("err_null_secret", "InvalidParam_NullSecret", "BCryptGenerateSymmetricKey",
                        status, GetLastError(), 0, NULL, 0, hKey != NULL);

    /* 4. Non-zero dwFlags returns STATUS_INVALID_PARAMETER */
    SetLastError(0);
    hKey = NULL;
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, key_obj, sizeof(key_obj), (PUCHAR)secret_128, 16, 0x100);
    ok(status == STATUS_INVALID_PARAMETER, "Expected STATUS_INVALID_PARAMETER, got 0x%08x\n", status);
    parity_record_exact("err_bad_flags", "InvalidParam_NonZeroFlags", "BCryptGenerateSymmetricKey",
                        status, GetLastError(), 0, NULL, 0, hKey != NULL);

    /* 5. Invalid AES key length (7 bytes) returns STATUS_INVALID_PARAMETER */
    SetLastError(0);
    hKey = NULL;
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, key_obj, sizeof(key_obj), (PUCHAR)secret_128, 7, 0);
    ok(status == STATUS_INVALID_PARAMETER, "Expected STATUS_INVALID_PARAMETER, got 0x%08x\n", status);
    parity_record_exact("err_bad_secret_len", "InvalidParam_BadKeySize7", "BCryptGenerateSymmetricKey",
                        status, GetLastError(), 0, NULL, 0, hKey != NULL);

    /* 6. Buffer boundary: pbKeyObject != NULL with cbKeyObject < BCRYPT_OBJECT_LENGTH returns STATUS_BUFFER_TOO_SMALL */
    SetLastError(0);
    hKey = NULL;
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, small_obj, sizeof(small_obj), (PUCHAR)secret_128, 16, 0);
    ok(status == STATUS_BUFFER_TOO_SMALL, "Expected STATUS_BUFFER_TOO_SMALL, got 0x%08x\n", status);
    parity_record_exact("err_buffer_too_small", "BufferBoundary_SmallKeyObject", "BCryptGenerateSymmetricKey",
                        status, GetLastError(), 0, NULL, 0, hKey != NULL);

    /* 7. Caller-allocated pbKeyObject + AES-128 + NIST FIPS-197 Encrypt/Decrypt/Export round-trip */
    SetLastError(0);
    hKey = NULL;
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, key_obj, sizeof(key_obj), (PUCHAR)secret_128, 16, 0);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS for AES-128, got 0x%08x\n", status);
    if (status == STATUS_SUCCESS && hKey != NULL) {
        memset(ciphertext, 0, sizeof(ciphertext));
        memset(decrypted, 0, sizeof(decrypted));
        out_len = 0;
        status = BCryptEncrypt(hKey, (PUCHAR)plaintext, 16, NULL, NULL, 0, ciphertext, 16, &out_len, 0);
        ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS in BCryptEncrypt, got 0x%08x\n", status);
        parity_record_exact("aes128_encrypt", "AES128_EncryptVector", "BCryptEncrypt",
                            status, GetLastError(), out_len, ciphertext, 16, 1);

        out_len = 0;
        status = BCryptDecrypt(hKey, ciphertext, 16, NULL, NULL, 0, decrypted, 16, &out_len, 0);
        ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS in BCryptDecrypt, got 0x%08x\n", status);
        parity_record_roundtrip("aes128_roundtrip", "AES128_DecryptRoundTrip", "BCryptDecrypt",
                                status, GetLastError(), out_len, decrypted, 16,
                                memcmp(plaintext, decrypted, 16) == 0);

        memset(exported_blob, 0, sizeof(exported_blob));
        out_len = 0;
        status = BCryptExportKey(hKey, NULL, BCRYPT_KEY_DATA_BLOB_STR, exported_blob, sizeof(exported_blob), &out_len, 0);
        ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS in BCryptExportKey, got 0x%08x\n", status);
        parity_record_exact("aes128_export", "AES128_ExportKeyDataBlob", "BCryptExportKey",
                            status, GetLastError(), out_len, exported_blob, out_len, 1);

        BCryptDestroyKey(hKey);
    } else {
        parity_record_exact("aes128_encrypt", "AES128_EncryptVector", "BCryptEncrypt",
                            status, GetLastError(), 0, NULL, 0, 0);
    }

    /* 8. Windows 8+ NULL pbKeyObject (cbKeyObject == 0) heap-allocated key object with AES-192 & AES-256 */
    SetLastError(0);
    hKey = NULL;
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, (PUCHAR)secret_192, 24, 0);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS for Win8+ NULL pbKeyObject AES-192, got 0x%08x\n", status);
    parity_record_handle_state("aes192_null_keyobj", "AES192_Win8NullKeyObject", "BCryptGenerateSymmetricKey",
                               status, GetLastError(), hKey, 8);
    if (hKey != NULL) {
        BCryptDestroyKey(hKey);
    }

    SetLastError(0);
    hKey = NULL;
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, (PUCHAR)secret_256, 32, 0);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS for AES-256, got 0x%08x\n", status);
    if (status == STATUS_SUCCESS && hKey != NULL) {
        memset(ciphertext, 0, sizeof(ciphertext));
        out_len = 0;
        status = BCryptEncrypt(hKey, (PUCHAR)plaintext, 16, NULL, NULL, 0, ciphertext, 16, &out_len, 0);
        ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS for AES-256 encrypt, got 0x%08x\n", status);
        parity_record_exact("aes256_encrypt", "AES256_EncryptVector", "BCryptEncrypt",
                            status, GetLastError(), out_len, ciphertext, 16, 1);
        BCryptDestroyKey(hKey);
    } else {
        parity_record_exact("aes256_encrypt", "AES256_EncryptVector", "BCryptEncrypt",
                            status, GetLastError(), 0, NULL, 0, 0);
    }

    BCryptCloseAlgorithmProvider(hAlg, 0);
    parity_end_document();
    return g_winetest_failures == 0 ? 0 : 1;
}
