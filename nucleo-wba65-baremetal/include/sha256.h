/* SHA-256 for message signing. sha256.c needs only this header; it uses no
 * libc, allocation, or hardware. */
#ifndef SHA256_H
#define SHA256_H

#define SHA256_DIGEST 32u
#define SHA256_BLOCK  64u

typedef struct {
  unsigned      h[8];
  unsigned      nbits_lo;              /* message length in bits, 64-bit split */
  unsigned      nbits_hi;
  unsigned char buf[SHA256_BLOCK];
  unsigned      fill;
} sha256_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, unsigned len);
void sha256_final(sha256_ctx *c, unsigned char out[SHA256_DIGEST]);

/* One-shot convenience. */
void sha256(const void *data, unsigned len, unsigned char out[SHA256_DIGEST]);

#endif /* SHA256_H */
