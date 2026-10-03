/* Host stand-in for mbedTLS's SHA-256, used only by `make test`.
 * On the board this header comes from ST's mbedTLS. Here it lets the host
 * tests build the real chirp/src/chirp_sign.c. It declares only the four calls
 * chirp_sign.c makes, with mbedTLS's names and signatures. */
#ifndef CHIRP_TEST_MBEDTLS_SHA256_H
#define CHIRP_TEST_MBEDTLS_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t      state[8];
  uint64_t      total;      /* message bytes seen so far */
  unsigned char block[64];
  unsigned      pending;    /* bytes waiting in block */
} mbedtls_sha256_context;

void mbedtls_sha256_init(mbedtls_sha256_context *ctx);
void mbedtls_sha256_free(mbedtls_sha256_context *ctx);

/* is224 must be 0: SHA-224 is not implemented, so any other value returns -1. */
int mbedtls_sha256_starts(mbedtls_sha256_context *ctx, int is224);
int mbedtls_sha256_update(mbedtls_sha256_context *ctx,
                          const unsigned char *input, size_t ilen);
int mbedtls_sha256_finish(mbedtls_sha256_context *ctx, unsigned char output[32]);

#endif /* CHIRP_TEST_MBEDTLS_SHA256_H */
