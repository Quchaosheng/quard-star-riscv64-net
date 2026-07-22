#ifndef M7E_SHA256_H
#define M7E_SHA256_H

#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bits;
    unsigned char block[64];
    unsigned int used;
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const unsigned char *data, unsigned int length);
void sha256_final(sha256_ctx_t *ctx, unsigned char digest[32]);

#endif
