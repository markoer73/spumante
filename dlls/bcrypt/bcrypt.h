/*
 * Wine BCrypt (Cryptography Next Generation) Public Header Definitions
 *
 * Copyright 2026 Wine Parity Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef _WINE_BCRYPT_H
#define _WINE_BCRYPT_H

#include <stdint.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

typedef void *BCRYPT_HANDLE;
typedef void *BCRYPT_ALG_HANDLE;
typedef void *BCRYPT_KEY_HANDLE;
typedef void *BCRYPT_HASH_HANDLE;

#define BCRYPT_OBJECT_LENGTH_BYTES 64U
#define BCRYPT_BLOCK_LENGTH_BYTES  16U

#define BCRYPT_KEY_DATA_BLOB_MAGIC   0x4d42444bU
#define BCRYPT_KEY_DATA_BLOB_VERSION1 0x1U

typedef struct _BCRYPT_KEY_DATA_BLOB_HEADER {
    uint32_t dwMagic;
    uint32_t dwVersion;
    uint32_t cbKeyData;
} BCRYPT_KEY_DATA_BLOB_HEADER, *PBCRYPT_KEY_DATA_BLOB_HEADER;

/* Narrow/Wide algorithm and property identifiers for clean-room portable compilation */
#define BCRYPT_AES_ALG_ID_STR        "AES"
#define BCRYPT_OBJECT_LENGTH_STR     "ObjectLength"
#define BCRYPT_BLOCK_LENGTH_STR      "BlockLength"
#define BCRYPT_KEY_DATA_BLOB_STR     "KeyDataBlob"

NTSTATUS WINAPI BCryptOpenAlgorithmProvider(
    BCRYPT_ALG_HANDLE *phAlgorithm,
    const char *pszAlgId,
    const char *pszImplementation,
    ULONG dwFlags);

NTSTATUS WINAPI BCryptCloseAlgorithmProvider(
    BCRYPT_ALG_HANDLE hAlgorithm,
    ULONG dwFlags);

NTSTATUS WINAPI BCryptGetProperty(
    BCRYPT_HANDLE hObject,
    const char *pszProperty,
    PUCHAR pbOutput,
    ULONG cbOutput,
    ULONG *pcbResult,
    ULONG dwFlags);

NTSTATUS WINAPI BCryptGenerateSymmetricKey(
    BCRYPT_ALG_HANDLE hAlgorithm,
    BCRYPT_KEY_HANDLE *phKey,
    PUCHAR pbKeyObject,
    ULONG cbKeyObject,
    PUCHAR pbSecret,
    ULONG cbSecret,
    ULONG dwFlags);

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
    ULONG dwFlags);

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
    ULONG dwFlags);

NTSTATUS WINAPI BCryptExportKey(
    BCRYPT_KEY_HANDLE hKey,
    BCRYPT_KEY_HANDLE hExportKey,
    const char *pszBlobType,
    PUCHAR pbOutput,
    ULONG cbOutput,
    ULONG *pcbResult,
    ULONG dwFlags);

NTSTATUS WINAPI BCryptDestroyKey(
    BCRYPT_KEY_HANDLE hKey);

#endif /* _WINE_BCRYPT_H */
