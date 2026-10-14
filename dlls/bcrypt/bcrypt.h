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
typedef void *BCRYPT_SECRET_HANDLE;

#ifndef _WIN32
typedef uint64_t ULONGLONG;
#endif

#ifndef STATUS_ACCESS_DENIED
#define STATUS_ACCESS_DENIED ((NTSTATUS)0xC0000022)
#endif
#ifndef STATUS_DATA_ERROR
#define STATUS_DATA_ERROR ((NTSTATUS)0xC000003E)
#endif
#ifndef STATUS_NOT_SUPPORTED
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BB)
#endif
#ifndef STATUS_INVALID_BUFFER_SIZE
#define STATUS_INVALID_BUFFER_SIZE ((NTSTATUS)0xC0000206)
#endif
#ifndef STATUS_INVALID_SIGNATURE
#define STATUS_INVALID_SIGNATURE ((NTSTATUS)0xC000A000)
#endif
#ifndef STATUS_AUTH_TAG_MISMATCH
#define STATUS_AUTH_TAG_MISMATCH ((NTSTATUS)0xC000A002)
#endif

#define BCRYPT_OBJECT_LENGTH_BYTES 64U
#define BCRYPT_BLOCK_LENGTH_BYTES  16U

#define BCRYPT_KEY_DATA_BLOB_MAGIC    0x4d42444bU
#define BCRYPT_KEY_DATA_BLOB_VERSION1 0x1U

#define BCRYPT_RSAPUBLIC_MAGIC        0x31415352U
#define BCRYPT_RSAPRIVATE_MAGIC       0x32415352U
#define BCRYPT_RSAFULLPRIVATE_MAGIC   0x33415352U

#define BCRYPT_ECDSA_PUBLIC_P256_MAGIC  0x31534345U
#define BCRYPT_ECDSA_PRIVATE_P256_MAGIC 0x32534345U
#define BCRYPT_ECDH_PUBLIC_P256_MAGIC   0x314b4345U
#define BCRYPT_ECDH_PRIVATE_P256_MAGIC  0x324b4345U
#define BCRYPT_ECDSA_PUBLIC_GENERIC_MAGIC  0x50444345U
#define BCRYPT_ECDSA_PRIVATE_GENERIC_MAGIC 0x56444345U

/* Flags */
#define BCRYPT_ALG_HANDLE_HMAC_FLAG      0x00000008U
#define BCRYPT_HASH_REUSABLE_FLAG        0x00000020U
#define BCRYPT_RNG_USE_ENTROPY_IN_BUFFER 0x00000001U
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG  0x00000002U
#define BCRYPT_BLOCK_PADDING             0x00000001U
#define BCRYPT_PAD_NONE                  0x00000001U
#define BCRYPT_PAD_PKCS1                 0x00000002U
#define BCRYPT_PAD_OAEP                  0x00000004U
#define BCRYPT_PAD_PSS                   0x00000008U

/* Pseudo handles */
#define BCRYPT_MD5_ALG_HANDLE          ((BCRYPT_ALG_HANDLE)(uintptr_t)0x00000021U)
#define BCRYPT_SHA1_ALG_HANDLE         ((BCRYPT_ALG_HANDLE)(uintptr_t)0x00000031U)
#define BCRYPT_SHA256_ALG_HANDLE       ((BCRYPT_ALG_HANDLE)(uintptr_t)0x00000041U)
#define BCRYPT_SHA384_ALG_HANDLE       ((BCRYPT_ALG_HANDLE)(uintptr_t)0x00000051U)
#define BCRYPT_SHA512_ALG_HANDLE       ((BCRYPT_ALG_HANDLE)(uintptr_t)0x00000061U)
#define BCRYPT_RNG_ALG_HANDLE          ((BCRYPT_ALG_HANDLE)(uintptr_t)0x00000081U)
#define BCRYPT_HMAC_MD5_ALG_HANDLE     ((BCRYPT_ALG_HANDLE)(uintptr_t)0x00000091U)
#define BCRYPT_HMAC_SHA1_ALG_HANDLE    ((BCRYPT_ALG_HANDLE)(uintptr_t)0x000000a1U)
#define BCRYPT_HMAC_SHA256_ALG_HANDLE  ((BCRYPT_ALG_HANDLE)(uintptr_t)0x000000b1U)
#define BCRYPT_HMAC_SHA384_ALG_HANDLE  ((BCRYPT_ALG_HANDLE)(uintptr_t)0x000000c1U)
#define BCRYPT_HMAC_SHA512_ALG_HANDLE  ((BCRYPT_ALG_HANDLE)(uintptr_t)0x000000d1U)
#define BCRYPT_RSA_ALG_HANDLE          ((BCRYPT_ALG_HANDLE)(uintptr_t)0x000000e1U)
#define BCRYPT_AES_CBC_ALG_HANDLE      ((BCRYPT_ALG_HANDLE)(uintptr_t)0x000001a1U)
#define BCRYPT_AES_ECB_ALG_HANDLE      ((BCRYPT_ALG_HANDLE)(uintptr_t)0x000001b1U)
#define BCRYPT_AES_GCM_ALG_HANDLE      ((BCRYPT_ALG_HANDLE)(uintptr_t)0x000001e1U)
#define BCRYPT_PBKDF2_ALG_HANDLE       ((BCRYPT_ALG_HANDLE)(uintptr_t)0x00000331U)

/* Interface identifiers */
#define BCRYPT_CIPHER_INTERFACE                0x00000001U
#define BCRYPT_HASH_INTERFACE                  0x00000002U
#define BCRYPT_ASYMMETRIC_ENCRYPTION_INTERFACE 0x00000003U
#define BCRYPT_SECRET_AGREEMENT_INTERFACE      0x00000004U
#define BCRYPT_SIGNATURE_INTERFACE             0x00000005U
#define BCRYPT_RNG_INTERFACE                   0x00000006U
#define BCRYPT_KEY_DERIVATION_INTERFACE        0x00000007U

typedef struct _BCRYPT_KEY_DATA_BLOB_HEADER {
    uint32_t dwMagic;
    uint32_t dwVersion;
    uint32_t cbKeyData;
} BCRYPT_KEY_DATA_BLOB_HEADER, *PBCRYPT_KEY_DATA_BLOB_HEADER;

typedef struct _BCRYPT_RSAKEY_BLOB {
    ULONG Magic;
    ULONG BitLength;
    ULONG cbPublicExp;
    ULONG cbModulus;
    ULONG cbPrime1;
    ULONG cbPrime2;
} BCRYPT_RSAKEY_BLOB, *PBCRYPT_RSAKEY_BLOB;

typedef struct _BCRYPT_ECCKEY_BLOB {
    ULONG dwMagic;
    ULONG cbKey;
} BCRYPT_ECCKEY_BLOB, *PBCRYPT_ECCKEY_BLOB;

#define BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO_VERSION 1U

typedef struct _BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO {
    ULONG cbSize;
    ULONG dwInfoVersion;
    PUCHAR pbNonce;
    ULONG cbNonce;
    PUCHAR pbAuthData;
    ULONG cbAuthData;
    PUCHAR pbTag;
    ULONG cbTag;
    PUCHAR pbMacContext;
    ULONG cbMacContext;
    ULONG cbAAD;
    ULONGLONG cbData;
    ULONG dwFlags;
} BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO, *PBCRYPT_AUTHENTICATED_CIPHER_MODE_INFO;

typedef struct _BCRYPT_PROVIDER_NAME {
    const char *pszProviderName;
} BCRYPT_PROVIDER_NAME, *PBCRYPT_PROVIDER_NAME;

typedef struct _CRYPT_PROVIDER_REF {
    ULONG dwInterface;
    const char *pszFunction;
    const char *pszProvider;
    ULONG cProperties;
    void *rgpProperties;
    void *pUM;
    void *pKM;
} CRYPT_PROVIDER_REF, *PCRYPT_PROVIDER_REF;

typedef struct _CRYPT_PROVIDER_REFS {
    ULONG cProviders;
    PCRYPT_PROVIDER_REF *rgpProviders;
} CRYPT_PROVIDER_REFS, *PCRYPT_PROVIDER_REFS;

typedef struct _BCRYPT_INTERFACE_VERSION {
    uint16_t MajorVersion;
    uint16_t MinorVersion;
} BCRYPT_INTERFACE_VERSION;

/* Narrow/Wide algorithm and property identifiers for clean-room portable compilation */
#define BCRYPT_AES_ALG_ID_STR          "AES"
#define BCRYPT_RNG_ALG_ID_STR          "RNG"
#define BCRYPT_SHA1_ALG_ID_STR         "SHA1"
#define BCRYPT_SHA256_ALG_ID_STR       "SHA256"
#define BCRYPT_SHA384_ALG_ID_STR       "SHA384"
#define BCRYPT_SHA512_ALG_ID_STR       "SHA512"
#define BCRYPT_MD5_ALG_ID_STR          "MD5"
#define BCRYPT_PBKDF2_ALG_ID_STR       "PBKDF2"
#define BCRYPT_RSA_ALG_ID_STR          "RSA"
#define BCRYPT_ECDSA_ALG_ID_STR        "ECDSA"
#define BCRYPT_ECDSA_P256_ALG_ID_STR   "ECDSA_P256"
#define BCRYPT_ECDH_P256_ALG_ID_STR    "ECDH_P256"

#define BCRYPT_OBJECT_LENGTH_STR       "ObjectLength"
#define BCRYPT_BLOCK_LENGTH_STR        "BlockLength"
#define BCRYPT_HASH_LENGTH_STR         "HashDigestLength"
#define BCRYPT_HASH_BLOCK_LENGTH_STR   "HashBlockLength"
#define BCRYPT_CHAINING_MODE_STR       "ChainingMode"
#define BCRYPT_CHAIN_MODE_ECB_STR      "ChainingModeECB"
#define BCRYPT_CHAIN_MODE_CBC_STR      "ChainingModeCBC"
#define BCRYPT_CHAIN_MODE_GCM_STR      "ChainingModeGCM"
#define BCRYPT_ECC_CURVE_NAME_STR      "ECCCurveName"
#define BCRYPT_ECC_CURVE_SECP256R1_STR "secP256r1"
#define BCRYPT_ECC_CURVE_BRAINPOOLP256R1_STR "brainpoolP256r1"

#define BCRYPT_KEY_DATA_BLOB_STR       "KeyDataBlob"
#define BCRYPT_OPAQUE_KEY_BLOB_STR     "OpaqueKeyBlob"
#define BCRYPT_RSAPUBLIC_BLOB_STR      "RSAPUBLICBLOB"
#define BCRYPT_RSAPRIVATE_BLOB_STR     "RSAPRIVATEBLOB"
#define BCRYPT_ECCPUBLIC_BLOB_STR      "ECCPUBLICBLOB"
#define BCRYPT_ECCPRIVATE_BLOB_STR     "ECCPRIVATEBLOB"

#define BCRYPT_KEY_LENGTHS_STR         "KeyLengths"
#define BCRYPT_KEY_STRENGTH_STR        "KeyStrength"
#define BCRYPT_KEY_LENGTH_STR          "KeyLength"

#define MS_PRIMITIVE_PROVIDER_STR      "Microsoft Primitive Provider"

/* Standard Windows CNG macro aliases for upstream winetest compatibility */
#ifndef BCRYPT_AES_ALGORITHM
#define BCRYPT_AES_ALGORITHM           BCRYPT_AES_ALG_ID_STR
#define BCRYPT_RNG_ALGORITHM           BCRYPT_RNG_ALG_ID_STR
#define BCRYPT_SHA1_ALGORITHM          BCRYPT_SHA1_ALG_ID_STR
#define BCRYPT_SHA256_ALGORITHM        BCRYPT_SHA256_ALG_ID_STR
#define BCRYPT_SHA384_ALGORITHM        BCRYPT_SHA384_ALG_ID_STR
#define BCRYPT_SHA512_ALGORITHM        BCRYPT_SHA512_ALG_ID_STR
#define BCRYPT_MD5_ALGORITHM           BCRYPT_MD5_ALG_ID_STR
#define BCRYPT_PBKDF2_ALGORITHM        BCRYPT_PBKDF2_ALG_ID_STR
#define BCRYPT_RSA_ALGORITHM           BCRYPT_RSA_ALG_ID_STR
#define BCRYPT_ECDSA_ALGORITHM         BCRYPT_ECDSA_ALG_ID_STR
#define BCRYPT_ECDSA_P256_ALGORITHM    BCRYPT_ECDSA_P256_ALG_ID_STR
#define BCRYPT_ECDH_P256_ALGORITHM     BCRYPT_ECDH_P256_ALG_ID_STR
#define MS_PRIMITIVE_PROVIDER          MS_PRIMITIVE_PROVIDER_STR
#define BCRYPT_OBJECT_LENGTH           BCRYPT_OBJECT_LENGTH_STR
#define BCRYPT_BLOCK_LENGTH            BCRYPT_BLOCK_LENGTH_STR
#define BCRYPT_HASH_LENGTH             BCRYPT_HASH_LENGTH_STR
#define BCRYPT_HASH_BLOCK_LENGTH       BCRYPT_HASH_BLOCK_LENGTH_STR
#define BCRYPT_CHAINING_MODE           BCRYPT_CHAINING_MODE_STR
#define BCRYPT_CHAIN_MODE_ECB          BCRYPT_CHAIN_MODE_ECB_STR
#define BCRYPT_CHAIN_MODE_CBC          BCRYPT_CHAIN_MODE_CBC_STR
#define BCRYPT_CHAIN_MODE_GCM          BCRYPT_CHAIN_MODE_GCM_STR
#define BCRYPT_KEY_LENGTHS             BCRYPT_KEY_LENGTHS_STR
#define BCRYPT_KEY_STRENGTH            BCRYPT_KEY_STRENGTH_STR
#define BCRYPT_KEY_LENGTH              BCRYPT_KEY_LENGTH_STR
#define BCRYPT_KEY_DATA_BLOB           BCRYPT_KEY_DATA_BLOB_STR
#define BCRYPT_OPAQUE_KEY_BLOB         BCRYPT_OPAQUE_KEY_BLOB_STR
#define BCRYPT_RSAPUBLIC_BLOB          BCRYPT_RSAPUBLIC_BLOB_STR
#define BCRYPT_RSAPRIVATE_BLOB         BCRYPT_RSAPRIVATE_BLOB_STR
#define BCRYPT_ECCPUBLIC_BLOB          BCRYPT_ECCPUBLIC_BLOB_STR
#define BCRYPT_ECCPRIVATE_BLOB         BCRYPT_ECCPRIVATE_BLOB_STR
#endif

#ifndef BCRYPT_CIPHER_OPERATION
#define BCRYPT_CIPHER_OPERATION                0x00000001U
#define BCRYPT_HASH_OPERATION                  0x00000002U
#define BCRYPT_ASYMMETRIC_ENCRYPTION_OPERATION 0x00000004U
#define BCRYPT_SECRET_AGREEMENT_OPERATION      0x00000008U
#define BCRYPT_SIGNATURE_OPERATION             0x00000010U
#define BCRYPT_RNG_OPERATION                   0x00000020U
#define BCRYPT_KEY_DERIVATION_OPERATION        0x00000040U
#endif

#ifndef CRYPT_UM
#define CRYPT_UM            0x00000001U
#define CRYPT_KM            0x00000002U
#define CRYPT_ALL_PROVIDERS 0x00000004U
#endif

typedef struct __BCRYPT_KEY_LENGTHS_STRUCT {
    ULONG dwMinLength;
    ULONG dwMaxLength;
    ULONG dwIncrement;
} BCRYPT_KEY_LENGTHS_STRUCT;

typedef struct _BCRYPT_ALGORITHM_IDENTIFIER {
    const char *pszName;
    ULONG dwClass;
    ULONG dwFlags;
} BCRYPT_ALGORITHM_IDENTIFIER;

NTSTATUS WINAPI BCryptEnumAlgorithms(
    ULONG dwAlgOperations,
    ULONG *pAlgCount,
    BCRYPT_ALGORITHM_IDENTIFIER **ppAlgList,
    ULONG dwFlags);

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

NTSTATUS WINAPI BCryptSetProperty(
    BCRYPT_HANDLE hObject,
    const char *pszProperty,
    PUCHAR pbInput,
    ULONG cbInput,
    ULONG dwFlags);

NTSTATUS WINAPI BCryptGenerateSymmetricKey(
    BCRYPT_ALG_HANDLE hAlgorithm,
    BCRYPT_KEY_HANDLE *phKey,
    PUCHAR pbKeyObject,
    ULONG cbKeyObject,
    PUCHAR pbSecret,
    ULONG cbSecret,
    ULONG dwFlags);

NTSTATUS WINAPI BCryptImportKey(
    BCRYPT_ALG_HANDLE hAlgorithm,
    BCRYPT_KEY_HANDLE hImportKey,
    const char *pszBlobType,
    BCRYPT_KEY_HANDLE *phKey,
    PUCHAR pbKeyObject,
    ULONG cbKeyObject,
    PUCHAR pbInput,
    ULONG cbInput,
    ULONG dwFlags);

NTSTATUS WINAPI BCryptImportKeyPair(
    BCRYPT_ALG_HANDLE hAlgorithm,
    BCRYPT_KEY_HANDLE hImportKey,
    const char *pszBlobType,
    BCRYPT_KEY_HANDLE *phKey,
    PUCHAR pbInput,
    ULONG cbInput,
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

NTSTATUS WINAPI BCryptDuplicateKey(
    BCRYPT_KEY_HANDLE hKey,
    BCRYPT_KEY_HANDLE *phNewKey,
    PUCHAR pbKeyObject,
    ULONG cbKeyObject,
    ULONG dwFlags);

NTSTATUS WINAPI BCryptDestroyKey(
    BCRYPT_KEY_HANDLE hKey);

NTSTATUS WINAPI BCryptGenRandom(
    BCRYPT_ALG_HANDLE hAlgorithm,
    PUCHAR pbBuffer,
    ULONG cbBuffer,
    ULONG dwFlags);

NTSTATUS WINAPI BCryptCreateHash(
    BCRYPT_ALG_HANDLE hAlgorithm,
    BCRYPT_HASH_HANDLE *phHash,
    PUCHAR pbHashObject,
    ULONG cbHashObject,
    PUCHAR pbSecret,
    ULONG cbSecret,
    ULONG dwFlags);

NTSTATUS WINAPI BCryptHashData(
    BCRYPT_HASH_HANDLE hHash,
    PUCHAR pbInput,
    ULONG cbInput,
    ULONG dwFlags);

NTSTATUS WINAPI BCryptFinishHash(
    BCRYPT_HASH_HANDLE hHash,
    PUCHAR pbOutput,
    ULONG cbOutput,
    ULONG dwFlags);

NTSTATUS WINAPI BCryptDuplicateHash(
    BCRYPT_HASH_HANDLE hHash,
    BCRYPT_HASH_HANDLE *phNewHash,
    PUCHAR pbHashObject,
    ULONG cbHashObject,
    ULONG dwFlags);

NTSTATUS WINAPI BCryptDestroyHash(
    BCRYPT_HASH_HANDLE hHash);

NTSTATUS WINAPI BCryptHash(
    BCRYPT_ALG_HANDLE hAlgorithm,
    PUCHAR pbSecret,
    ULONG cbSecret,
    PUCHAR pbInput,
    ULONG cbInput,
    PUCHAR pbOutput,
    ULONG cbOutput);

NTSTATUS WINAPI BCryptDeriveKeyPBKDF2(
    BCRYPT_ALG_HANDLE hPrf,
    PUCHAR pbPassword,
    ULONG cbPassword,
    PUCHAR pbSalt,
    ULONG cbSalt,
    ULONGLONG cIterations,
    PUCHAR pbDerivedKey,
    ULONG cbDerivedKey,
    ULONG dwFlags);

NTSTATUS WINAPI BCryptSignHash(
    BCRYPT_KEY_HANDLE hKey,
    void *pPaddingInfo,
    PUCHAR pbInput,
    ULONG cbInput,
    PUCHAR pbOutput,
    ULONG cbOutput,
    ULONG *pcbResult,
    ULONG dwFlags);

NTSTATUS WINAPI BCryptVerifySignature(
    BCRYPT_KEY_HANDLE hKey,
    void *pPaddingInfo,
    PUCHAR pbHash,
    ULONG cbHash,
    PUCHAR pbSignature,
    ULONG cbSignature,
    ULONG dwFlags);

void WINAPI BCryptFreeBuffer(void *pvBuffer);

NTSTATUS WINAPI BCryptEnumProviders(
    const char *pszAlgId,
    ULONG *pImplCount,
    BCRYPT_PROVIDER_NAME **ppImplList,
    ULONG dwFlags);

NTSTATUS WINAPI BCryptResolveProviders(
    const char *pszContext,
    ULONG dwInterface,
    const char *pszFunction,
    const char *pszProvider,
    ULONG dwMode,
    ULONG dwFlags,
    ULONG *pcbBuffer,
    PCRYPT_PROVIDER_REFS *ppBuffer);

typedef struct _BCRYPT_CIPHER_FUNCTION_TABLE {
    BCRYPT_INTERFACE_VERSION Version;
    NTSTATUS (WINAPI *pfnOpenAlgorithmProvider)(BCRYPT_ALG_HANDLE *, const char *, const char *, ULONG);
    NTSTATUS (WINAPI *pfnGetProperty)(BCRYPT_HANDLE, const char *, PUCHAR, ULONG, ULONG *, ULONG);
    NTSTATUS (WINAPI *pfnSetProperty)(BCRYPT_HANDLE, const char *, PUCHAR, ULONG, ULONG);
    NTSTATUS (WINAPI *pfnCloseAlgorithmProvider)(BCRYPT_ALG_HANDLE, ULONG);
    NTSTATUS (WINAPI *pfnGenerateSymmetricKey)(BCRYPT_ALG_HANDLE, BCRYPT_KEY_HANDLE *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
    NTSTATUS (WINAPI *pfnEncrypt)(BCRYPT_KEY_HANDLE, PUCHAR, ULONG, void *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG *, ULONG);
    NTSTATUS (WINAPI *pfnDecrypt)(BCRYPT_KEY_HANDLE, PUCHAR, ULONG, void *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG *, ULONG);
    NTSTATUS (WINAPI *pfnImportKey)(BCRYPT_ALG_HANDLE, BCRYPT_KEY_HANDLE, const char *, BCRYPT_KEY_HANDLE *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
    NTSTATUS (WINAPI *pfnExportKey)(BCRYPT_KEY_HANDLE, BCRYPT_KEY_HANDLE, const char *, PUCHAR, ULONG, ULONG *, ULONG);
    NTSTATUS (WINAPI *pfnDuplicateKey)(BCRYPT_KEY_HANDLE, BCRYPT_KEY_HANDLE *, PUCHAR, ULONG, ULONG);
    NTSTATUS (WINAPI *pfnDestroyKey)(BCRYPT_KEY_HANDLE);
} BCRYPT_CIPHER_FUNCTION_TABLE;

typedef struct _BCRYPT_HASH_FUNCTION_TABLE {
    BCRYPT_INTERFACE_VERSION Version;
    NTSTATUS (WINAPI *pfnOpenAlgorithmProvider)(BCRYPT_ALG_HANDLE *, const char *, const char *, ULONG);
    NTSTATUS (WINAPI *pfnGetProperty)(BCRYPT_HANDLE, const char *, PUCHAR, ULONG, ULONG *, ULONG);
    NTSTATUS (WINAPI *pfnSetProperty)(BCRYPT_HANDLE, const char *, PUCHAR, ULONG, ULONG);
    NTSTATUS (WINAPI *pfnCloseAlgorithmProvider)(BCRYPT_ALG_HANDLE, ULONG);
    NTSTATUS (WINAPI *pfnCreateHash)(BCRYPT_ALG_HANDLE, BCRYPT_HASH_HANDLE *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
    NTSTATUS (WINAPI *pfnHashData)(BCRYPT_HASH_HANDLE, PUCHAR, ULONG, ULONG);
    NTSTATUS (WINAPI *pfnFinishHash)(BCRYPT_HASH_HANDLE, PUCHAR, ULONG, ULONG);
    NTSTATUS (WINAPI *pfnDuplicateHash)(BCRYPT_HASH_HANDLE, BCRYPT_HASH_HANDLE *, PUCHAR, ULONG, ULONG);
    NTSTATUS (WINAPI *pfnDestroyHash)(BCRYPT_HASH_HANDLE);
} BCRYPT_HASH_FUNCTION_TABLE;

typedef struct _BCRYPT_RNG_FUNCTION_TABLE {
    BCRYPT_INTERFACE_VERSION Version;
    NTSTATUS (WINAPI *pfnOpenAlgorithmProvider)(BCRYPT_ALG_HANDLE *, const char *, const char *, ULONG);
    NTSTATUS (WINAPI *pfnGetProperty)(BCRYPT_HANDLE, const char *, PUCHAR, ULONG, ULONG *, ULONG);
    NTSTATUS (WINAPI *pfnSetProperty)(BCRYPT_HANDLE, const char *, PUCHAR, ULONG, ULONG);
    NTSTATUS (WINAPI *pfnCloseAlgorithmProvider)(BCRYPT_ALG_HANDLE, ULONG);
    NTSTATUS (WINAPI *pfnGenRandom)(BCRYPT_ALG_HANDLE, PUCHAR, ULONG, ULONG);
} BCRYPT_RNG_FUNCTION_TABLE;

NTSTATUS WINAPI GetCipherInterface(
    const char *pszProviderName,
    BCRYPT_CIPHER_FUNCTION_TABLE **ppFunctionTable,
    ULONG dwFlags);

NTSTATUS WINAPI GetHashInterface(
    const char *pszProviderName,
    BCRYPT_HASH_FUNCTION_TABLE **ppFunctionTable,
    ULONG dwFlags);

NTSTATUS WINAPI GetRngInterface(
    const char *pszProviderName,
    BCRYPT_RNG_FUNCTION_TABLE **ppFunctionTable,
    ULONG dwFlags);

#endif /* _WINE_BCRYPT_H */
