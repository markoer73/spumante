/*
 * Differential Conformance Test for Expanded BCrypt Primitives
 * (AES-CBC / PKCS#7, BCryptGenRandom, SHA-256 / Reusable HMAC-SHA256,
 *  BCryptDeriveKeyPBKDF2, BCRYPT_PAD_NONE RSA/ECC, and Provider/Interface APIs)
 *
 * Copyright 2026 Spumante Contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../../include/parity_test.h"
#include "../../../dlls/bcrypt/bcrypt.h"

int main(void)
{
    BCRYPT_ALG_HANDLE hAesAlg = NULL;
    BCRYPT_KEY_HANDLE hAesKey = NULL;
    BCRYPT_ALG_HANDLE hShaAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    BCRYPT_ALG_HANDLE hHmacAlg = NULL;
    BCRYPT_HASH_HANDLE hHmac = NULL;
    BCRYPT_ALG_HANDLE hRsaAlg = NULL;
    BCRYPT_KEY_HANDLE hRsaKey = NULL;
    BCRYPT_ALG_HANDLE hEccAlg = NULL;
    BCRYPT_KEY_HANDLE hEccKey = NULL;
    NTSTATUS status;
    ULONG out_len = 0;

    /* 1. NIST SP 800-38A Section F.2.1 AES-128-CBC test vectors */
    static const UCHAR cbc_key_128[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const UCHAR cbc_iv_init[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const UCHAR cbc_pt_block1[16] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
    };
    static const UCHAR unaligned_pt[10] = {
        'W', 'i', 'n', 'e', 'P', 'a', 'r', 'i', 't', 'y'
    };
    UCHAR iv_work[16];
    UCHAR cbc_ct[32];
    UCHAR cbc_dec[32];

    /* 2. RNG buffer */
    UCHAR rng_buf[32];
    int rng_nonzero = 0;
    size_t i;

    /* 3. SHA-256 & HMAC-SHA256 (RFC 4231 Test Case 2) vectors */
    static const UCHAR sha_msg_abc[3] = { 'a', 'b', 'c' };
    static const UCHAR hmac_key_jefe[4] = { 'J', 'e', 'f', 'e' };
    static const char *hmac_msg = "what do ya want for nothing?";
    UCHAR digest_out[32];
    UCHAR digest_pass1[32];

    /* 4. PBKDF2-HMAC-SHA256 (RFC 6070) vectors */
    static const UCHAR pbkdf2_pass[8] = { 'p', 'a', 's', 's', 'w', 'o', 'r', 'd' };
    static const UCHAR pbkdf2_salt[4] = { 's', 'a', 'l', 't' };
    UCHAR pbkdf2_dk[32];

    /* 5. RSA BCRYPT_PAD_NONE 64-bit private key blob:
     *    p = 0xFFFFFFFB (4294967291), q = 0xFFFFFFBF (4294967231),
     *    n = p * q = 0xFFFFFFBA00000145, e = 0x010001 (65537)
     */
    UCHAR rsa_priv_blob[sizeof(BCRYPT_RSAKEY_BLOB) + 3 + 8 + 4 + 4];
    BCRYPT_RSAKEY_BLOB *rsa_hdr = (BCRYPT_RSAKEY_BLOB *)(void *)rsa_priv_blob;
    static const UCHAR rsa_exp[3] = { 0x01, 0x00, 0x01 };
    static const UCHAR rsa_mod[8] = { 0xff, 0xff, 0xff, 0xba, 0x00, 0x00, 0x01, 0x45 };
    static const UCHAR rsa_p1[4]  = { 0xff, 0xff, 0xff, 0xfb };
    static const UCHAR rsa_p2[4]  = { 0xff, 0xff, 0xff, 0xbf };
    static const UCHAR rsa_pt[8]  = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77 };
    UCHAR rsa_ct[8];
    UCHAR rsa_dec[8];

    /* 6. ECC P-256 key blob and signature buffers */
    UCHAR ecc_priv_blob[sizeof(BCRYPT_ECCKEY_BLOB) + 32 + 32 + 32];
    BCRYPT_ECCKEY_BLOB *ecc_hdr = (BCRYPT_ECCKEY_BLOB *)(void *)ecc_priv_blob;
    UCHAR ecc_sig[64];

    /* 7. Provider & Interface pointers */
    ULONG impl_count = 0;
    BCRYPT_PROVIDER_NAME *impl_list = NULL;
    ULONG refs_cb = 0;
    PCRYPT_PROVIDER_REFS refs_buf = NULL;
    BCRYPT_CIPHER_FUNCTION_TABLE *cipher_tbl = NULL;
    BCRYPT_HASH_FUNCTION_TABLE *hash_tbl = NULL;
    BCRYPT_RNG_FUNCTION_TABLE *rng_tbl = NULL;

    parity_begin_document("bcrypt", "test_crypto_primitives", "x86_64", "wine_macos");

    /* =====================================================================
     * Section 1: AES-128-CBC, IV In-Place Mutation & PKCS#7 Block Padding
     * ===================================================================== */
    status = BCryptOpenAlgorithmProvider(&hAesAlg, BCRYPT_AES_ALG_ID_STR, NULL, 0);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS opening AES provider, got 0x%08x\n", (unsigned int)status);

    status = BCryptSetProperty(
        hAesAlg,
        BCRYPT_CHAINING_MODE_STR,
        (PUCHAR)BCRYPT_CHAIN_MODE_CBC_STR,
        (ULONG)sizeof(BCRYPT_CHAIN_MODE_CBC_STR),
        0);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS setting CBC chaining mode, got 0x%08x\n", (unsigned int)status);

    status = BCryptGenerateSymmetricKey(hAesAlg, &hAesKey, NULL, 0, (PUCHAR)cbc_key_128, 16, 0);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS generating AES-128 key, got 0x%08x\n", (unsigned int)status);

    memcpy(iv_work, cbc_iv_init, 16);
    memset(cbc_ct, 0, sizeof(cbc_ct));
    out_len = 0;
    status = BCryptEncrypt(hAesKey, (PUCHAR)cbc_pt_block1, 16, NULL, iv_work, 16, cbc_ct, 16, &out_len, 0);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS in AES-128-CBC encrypt, got 0x%08x\n", (unsigned int)status);
    parity_record_exact("aes128_cbc_encrypt", "AES128_CBC_EncryptVector", "BCryptEncrypt",
                        status, GetLastError(), out_len, cbc_ct, 16, 1);

    parity_record_exact("aes128_cbc_iv_mutated", "AES128_CBC_IVUpdatedInPlace", "BCryptEncrypt",
                        status, GetLastError(), 16, iv_work, 16, 1);

    /* PKCS#7 BCRYPT_BLOCK_PADDING on 10-byte plaintext */
    memcpy(iv_work, cbc_iv_init, 16);
    memset(cbc_ct, 0, sizeof(cbc_ct));
    out_len = 0;
    status = BCryptEncrypt(
        hAesKey,
        (PUCHAR)unaligned_pt,
        10,
        NULL,
        iv_work,
        16,
        cbc_ct,
        32,
        &out_len,
        BCRYPT_BLOCK_PADDING);
    ok(status == STATUS_SUCCESS && out_len == 16,
       "Expected 16-byte PKCS#7 ciphertext, got 0x%08x len=%u\n", (unsigned int)status, (unsigned int)out_len);

    memcpy(iv_work, cbc_iv_init, 16);
    memset(cbc_dec, 0, sizeof(cbc_dec));
    out_len = 0;
    status = BCryptDecrypt(
        hAesKey,
        cbc_ct,
        16,
        NULL,
        iv_work,
        16,
        cbc_dec,
        32,
        &out_len,
        BCRYPT_BLOCK_PADDING);
    ok(status == STATUS_SUCCESS && out_len == 10,
       "Expected 10-byte PKCS#7 plaintext, got 0x%08x len=%u\n", (unsigned int)status, (unsigned int)out_len);
    parity_record_roundtrip("aes128_cbc_pkcs7_roundtrip", "AES128_CBC_PKCS7_RoundTrip", "BCryptDecrypt",
                            status, GetLastError(), out_len, cbc_dec, 10,
                            out_len == 10 && memcmp(cbc_dec, unaligned_pt, 10) == 0);

    if (hAesKey) {
        BCryptDestroyKey(hAesKey);
    }
    if (hAesAlg) {
        BCryptCloseAlgorithmProvider(hAesAlg, 0);
    }

    /* =====================================================================
     * Section 2: BCryptGenRandom
     * ===================================================================== */
    memset(rng_buf, 0, sizeof(rng_buf));
    status = BCryptGenRandom(NULL, rng_buf, sizeof(rng_buf), 0);
    ok(status == (NTSTATUS)STATUS_INVALID_HANDLE,
       "Expected STATUS_INVALID_HANDLE when hAlg==NULL and flags==0, got 0x%08x\n", (unsigned int)status);
    parity_record_exact("rng_err_null_alg_no_flag", "GenRandom_NullAlgWithoutSystemPreferredFlag", "BCryptGenRandom",
                        status, GetLastError(), 0, NULL, 0, 0);

    memset(rng_buf, 0, sizeof(rng_buf));
    status = BCryptGenRandom(NULL, rng_buf, sizeof(rng_buf), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS for system preferred RNG, got 0x%08x\n", (unsigned int)status);
    for (i = 0; i < sizeof(rng_buf); i++) {
        if (rng_buf[i] != 0) {
            rng_nonzero = 1;
            break;
        }
    }
    ok(rng_nonzero == 1, "Expected non-zero random output from BCryptGenRandom\n");
    parity_record_handle_state("rng_system_preferred", "GenRandom_SystemPreferredEntropy", "BCryptGenRandom",
                               status, GetLastError(), rng_nonzero ? (const void *)rng_buf : NULL, 8);

    /* =====================================================================
     * Section 3: BCryptCreateHash / BCryptHashData / BCryptFinishHash
     *            (SHA-256 & Reusable HMAC-SHA256)
     * ===================================================================== */
    status = BCryptOpenAlgorithmProvider(&hShaAlg, BCRYPT_SHA256_ALG_ID_STR, NULL, 0);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS opening SHA256 provider, got 0x%08x\n", (unsigned int)status);

    memset(digest_out, 0, sizeof(digest_out));
    status = BCryptCreateHash(hShaAlg, &hHash, NULL, 0, NULL, 0, 0);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS creating SHA256 hash, got 0x%08x\n", (unsigned int)status);
    if (status == STATUS_SUCCESS && hHash != NULL) {
        status = BCryptHashData(hHash, (PUCHAR)sha_msg_abc, 3, 0);
        ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS in BCryptHashData, got 0x%08x\n", (unsigned int)status);
        status = BCryptFinishHash(hHash, digest_out, 32, 0);
        ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS in BCryptFinishHash, got 0x%08x\n", (unsigned int)status);
        BCryptDestroyHash(hHash);
    }
    parity_record_exact("sha256_abc_digest", "SHA256_AbcDigestVector", "BCryptFinishHash",
                        status, GetLastError(), 32, digest_out, 32, 1);

    /* Reusable HMAC-SHA256 (RFC 4231 Test Case 2) */
    status = BCryptOpenAlgorithmProvider(&hHmacAlg, BCRYPT_SHA256_ALG_ID_STR, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS opening HMAC-SHA256 provider, got 0x%08x\n", (unsigned int)status);

    memset(digest_pass1, 0, sizeof(digest_pass1));
    memset(digest_out, 0, sizeof(digest_out));
    status = BCryptCreateHash(
        hHmacAlg,
        &hHmac,
        NULL,
        0,
        (PUCHAR)hmac_key_jefe,
        sizeof(hmac_key_jefe),
        BCRYPT_HASH_REUSABLE_FLAG);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS creating reusable HMAC, got 0x%08x\n", (unsigned int)status);
    if (status == STATUS_SUCCESS && hHmac != NULL) {
        status = BCryptHashData(hHmac, (PUCHAR)hmac_msg, 28, 0);
        ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS in HMAC pass 1 HashData, got 0x%08x\n", (unsigned int)status);
        status = BCryptFinishHash(hHmac, digest_pass1, 32, 0);
        ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS in HMAC pass 1 FinishHash, got 0x%08x\n", (unsigned int)status);

        /* Second pass on the SAME reusable hash handle */
        status = BCryptHashData(hHmac, (PUCHAR)hmac_msg, 28, 0);
        ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS in HMAC pass 2 HashData, got 0x%08x\n", (unsigned int)status);
        status = BCryptFinishHash(hHmac, digest_out, 32, 0);
        ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS in HMAC pass 2 FinishHash, got 0x%08x\n", (unsigned int)status);
        BCryptDestroyHash(hHmac);
    }
    parity_record_exact("hmac_sha256_reusable", "HMAC_SHA256_ReusableHandleRFC4231", "BCryptFinishHash",
                        status, GetLastError(), 32, digest_out, 32, 1);

    /* =====================================================================
     * Section 4: BCryptDeriveKeyPBKDF2 (RFC 6070 PBKDF2-HMAC-SHA256)
     * ===================================================================== */
    memset(pbkdf2_dk, 0, sizeof(pbkdf2_dk));
    status = BCryptDeriveKeyPBKDF2(
        hHmacAlg,
        (PUCHAR)pbkdf2_pass,
        sizeof(pbkdf2_pass),
        (PUCHAR)pbkdf2_salt,
        sizeof(pbkdf2_salt),
        4096ULL,
        pbkdf2_dk,
        sizeof(pbkdf2_dk),
        0);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS in BCryptDeriveKeyPBKDF2, got 0x%08x\n", (unsigned int)status);
    parity_record_exact("pbkdf2_hmac_sha256", "PBKDF2_HMAC_SHA256_RFC6070_4096", "BCryptDeriveKeyPBKDF2",
                        status, GetLastError(), 32, pbkdf2_dk, 32, 1);

    if (hHmacAlg) {
        BCryptCloseAlgorithmProvider(hHmacAlg, 0);
    }
    if (hShaAlg) {
        BCryptCloseAlgorithmProvider(hShaAlg, 0);
    }

    /* =====================================================================
     * Section 5: BCRYPT_PAD_NONE RSA & ECC Key Pair Import / Sign / Verify
     * ===================================================================== */
    memset(rsa_priv_blob, 0, sizeof(rsa_priv_blob));
    rsa_hdr->Magic = BCRYPT_RSAPRIVATE_MAGIC;
    rsa_hdr->BitLength = 64;
    rsa_hdr->cbPublicExp = 3;
    rsa_hdr->cbModulus = 8;
    rsa_hdr->cbPrime1 = 4;
    rsa_hdr->cbPrime2 = 4;
    memcpy(rsa_priv_blob + sizeof(BCRYPT_RSAKEY_BLOB), rsa_exp, 3);
    memcpy(rsa_priv_blob + sizeof(BCRYPT_RSAKEY_BLOB) + 3, rsa_mod, 8);
    memcpy(rsa_priv_blob + sizeof(BCRYPT_RSAKEY_BLOB) + 11, rsa_p1, 4);
    memcpy(rsa_priv_blob + sizeof(BCRYPT_RSAKEY_BLOB) + 15, rsa_p2, 4);

    status = BCryptOpenAlgorithmProvider(&hRsaAlg, BCRYPT_RSA_ALG_ID_STR, NULL, 0);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS opening RSA provider, got 0x%08x\n", (unsigned int)status);

    status = BCryptImportKeyPair(
        hRsaAlg,
        NULL,
        BCRYPT_RSAPRIVATE_BLOB_STR,
        &hRsaKey,
        rsa_priv_blob,
        (ULONG)sizeof(rsa_priv_blob),
        0);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS importing RSA key pair, got 0x%08x\n", (unsigned int)status);

    memset(rsa_ct, 0, sizeof(rsa_ct));
    memset(rsa_dec, 0, sizeof(rsa_dec));
    out_len = 0;
    status = BCryptEncrypt(
        hRsaKey,
        (PUCHAR)rsa_pt,
        8,
        NULL,
        NULL,
        0,
        rsa_ct,
        8,
        &out_len,
        BCRYPT_PAD_NONE);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS in RSA BCRYPT_PAD_NONE encrypt, got 0x%08x\n", (unsigned int)status);
    parity_record_exact("rsa_pad_none_encrypt", "RSA_BCRYPT_PAD_NONE_Encrypt", "BCryptEncrypt",
                        status, GetLastError(), out_len, rsa_ct, 8, 1);

    out_len = 0;
    status = BCryptDecrypt(
        hRsaKey,
        rsa_ct,
        8,
        NULL,
        NULL,
        0,
        rsa_dec,
        8,
        &out_len,
        BCRYPT_PAD_NONE);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS in RSA BCRYPT_PAD_NONE decrypt, got 0x%08x\n", (unsigned int)status);
    parity_record_roundtrip("rsa_pad_none_roundtrip", "RSA_BCRYPT_PAD_NONE_DecryptRoundTrip", "BCryptDecrypt",
                            status, GetLastError(), out_len, rsa_dec, 8,
                            out_len == 8 && memcmp(rsa_dec, rsa_pt, 8) == 0);

    if (hRsaKey) {
        BCryptDestroyKey(hRsaKey);
    }
    if (hRsaAlg) {
        BCryptCloseAlgorithmProvider(hRsaAlg, 0);
    }

    /* ECC P-256 curve property + key import + sign/verify */
    status = BCryptOpenAlgorithmProvider(&hEccAlg, BCRYPT_ECDSA_P256_ALG_ID_STR, NULL, 0);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS opening ECDSA_P256 provider, got 0x%08x\n", (unsigned int)status);
    memset(ecc_priv_blob, 0, sizeof(ecc_priv_blob));
    ecc_hdr->dwMagic = BCRYPT_ECDSA_PRIVATE_P256_MAGIC;
    ecc_hdr->cbKey = 32;
    ecc_priv_blob[sizeof(BCRYPT_ECCKEY_BLOB) + 31] = 0x01;
    ecc_priv_blob[sizeof(BCRYPT_ECCKEY_BLOB) + 63] = 0x02;
    ecc_priv_blob[sizeof(BCRYPT_ECCKEY_BLOB) + 95] = 0x03;

    status = BCryptImportKeyPair(
        hEccAlg,
        NULL,
        BCRYPT_ECCPRIVATE_BLOB_STR,
        &hEccKey,
        ecc_priv_blob,
        (ULONG)sizeof(ecc_priv_blob),
        0);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS importing ECC key pair, got 0x%08x\n", (unsigned int)status);
    memset(ecc_sig, 0, sizeof(ecc_sig));
    out_len = 0;
    status = BCryptSignHash(hEccKey, NULL, digest_out, 32, ecc_sig, 64, &out_len, BCRYPT_PAD_NONE);
    ok(status == STATUS_SUCCESS && out_len == 64, "Expected STATUS_SUCCESS signing with ECC, got 0x%08x\n", (unsigned int)status);
    status = BCryptVerifySignature(hEccKey, NULL, digest_out, 32, ecc_sig, 64, BCRYPT_PAD_NONE);
    ok(status == STATUS_SUCCESS, "Expected STATUS_SUCCESS verifying ECC signature, got 0x%08x\n", (unsigned int)status);
    parity_record_handle_state("ecc_p256_sign_verify", "ECC_P256_SignVerifyRoundTrip", "BCryptVerifySignature",
                               status, GetLastError(), hEccKey, 8);
    if (hEccKey) {
        BCryptDestroyKey(hEccKey);
    }
    if (hEccAlg) {
        BCryptCloseAlgorithmProvider(hEccAlg, 0);
    }

    /* =====================================================================
     * Section 6: Provider & Interface Registration APIs
     *            (BCryptEnumProviders, BCryptResolveProviders,
     *             GetCipherInterface, GetHashInterface, GetRngInterface)
     * ===================================================================== */
    status = BCryptEnumProviders(BCRYPT_AES_ALG_ID_STR, &impl_count, &impl_list, 0);
    ok(status == STATUS_SUCCESS && impl_count >= 1 && impl_list != NULL,
       "Expected STATUS_SUCCESS in BCryptEnumProviders, got 0x%08x\n", (unsigned int)status);
    if (impl_list) {
        BCryptFreeBuffer(impl_list);
    }

    status = BCryptResolveProviders(NULL, BCRYPT_CIPHER_INTERFACE, BCRYPT_AES_ALG_ID_STR, NULL, 0, 0, &refs_cb, &refs_buf);
    ok(status == STATUS_SUCCESS && refs_buf != NULL,
       "Expected STATUS_SUCCESS in BCryptResolveProviders, got 0x%08x\n", (unsigned int)status);
    if (refs_buf) {
        BCryptFreeBuffer(refs_buf);
    }

    status = GetCipherInterface(MS_PRIMITIVE_PROVIDER_STR, &cipher_tbl, 0);
    ok(status == STATUS_SUCCESS && cipher_tbl != NULL && cipher_tbl->pfnEncrypt == BCryptEncrypt,
       "Expected STATUS_SUCCESS in GetCipherInterface, got 0x%08x\n", (unsigned int)status);

    status = GetHashInterface(MS_PRIMITIVE_PROVIDER_STR, &hash_tbl, 0);
    ok(status == STATUS_SUCCESS && hash_tbl != NULL && hash_tbl->pfnCreateHash == BCryptCreateHash,
       "Expected STATUS_SUCCESS in GetHashInterface, got 0x%08x\n", (unsigned int)status);

    status = GetRngInterface(MS_PRIMITIVE_PROVIDER_STR, &rng_tbl, 0);
    ok(status == STATUS_SUCCESS && rng_tbl != NULL && rng_tbl->pfnGenRandom == BCryptGenRandom,
       "Expected STATUS_SUCCESS in GetRngInterface, got 0x%08x\n", (unsigned int)status);

    parity_record_handle_state("provider_and_interfaces_query", "ProviderAndFunctionTablesQuery", "GetCipherInterface",
                               status, GetLastError(),
                               (cipher_tbl && hash_tbl && rng_tbl) ? (const void *)cipher_tbl : NULL, 8);

    parity_end_document();
    return g_winetest_failures == 0 ? 0 : 1;
}
