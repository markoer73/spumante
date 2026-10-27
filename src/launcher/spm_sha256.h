/*
 * SPDX-License-Identifier: Apache-2.0
 * Spumante native runtime: portable SHA-256 (FIPS 180-4).
 */

#ifndef SPUMANTE_SPM_SHA256_H
#define SPUMANTE_SPM_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint32_t state[8];
    uint64_t bit_len;
    unsigned char block[64];
    size_t block_len;
} spm_sha256_ctx;

void spm_sha256_init(spm_sha256_ctx *ctx);
void spm_sha256_update(spm_sha256_ctx *ctx, const void *data, size_t len);
void spm_sha256_final(spm_sha256_ctx *ctx, unsigned char digest[32]);
void spm_sha256(const void *data, size_t len, unsigned char digest[32]);
void spm_hex_encode(const unsigned char *bytes, size_t len, char *out_hex);

#endif /* SPUMANTE_SPM_SHA256_H */
