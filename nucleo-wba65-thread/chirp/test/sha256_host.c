/* Plain SHA-256 (FIPS 180-4) for `make test` only; it is not in the firmware.
 * valtest.c checks it against published examples before trusting it. */

#include "mbedtls/sha256.h"

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32u - (n))))

static const uint32_t K[64] = {
  0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
  0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
  0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
  0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
  0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
  0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
  0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
  0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
  0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
  0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
  0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
  0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
  0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
  0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
  0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
  0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

/* Mix one 64-byte block into the running hash. */
static void compress(uint32_t h[8], const unsigned char b[64])
{
  uint32_t w[64], a, bb, c, d, e, f, g, hh, t1, t2;
  unsigned i;

  for (i = 0u; i < 16u; i++) {
    w[i] = ((uint32_t)b[i * 4u] << 24) | ((uint32_t)b[i * 4u + 1u] << 16) |
           ((uint32_t)b[i * 4u + 2u] << 8) | (uint32_t)b[i * 4u + 3u];
  }
  for (i = 16u; i < 64u; i++) {
    uint32_t s0 = ROTR(w[i - 15u], 7) ^ ROTR(w[i - 15u], 18) ^ (w[i - 15u] >> 3);
    uint32_t s1 = ROTR(w[i - 2u], 17) ^ ROTR(w[i - 2u], 19) ^ (w[i - 2u] >> 10);
    w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
  }

  a = h[0]; bb = h[1]; c = h[2]; d = h[3];
  e = h[4]; f  = h[5]; g = h[6]; hh = h[7];

  for (i = 0u; i < 64u; i++) {
    uint32_t S1  = ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25);
    uint32_t ch  = (e & f) ^ (~e & g);
    uint32_t S0  = ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22);
    uint32_t maj = (a & bb) ^ (a & c) ^ (bb & c);
    t1 = hh + S1 + ch + K[i] + w[i];
    t2 = S0 + maj;
    hh = g; g = f; f = e; e = d + t1;
    d = c; c = bb; bb = a; a = t1 + t2;
  }

  h[0] += a; h[1] += bb; h[2] += c; h[3] += d;
  h[4] += e; h[5] += f;  h[6] += g; h[7] += hh;
}

void mbedtls_sha256_init(mbedtls_sha256_context *ctx) { (void)ctx; }
void mbedtls_sha256_free(mbedtls_sha256_context *ctx) { (void)ctx; }

int mbedtls_sha256_starts(mbedtls_sha256_context *ctx, int is224)
{
  if (ctx == 0 || is224 != 0) { return -1; }

  ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
  ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
  ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
  ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
  ctx->total    = 0u;
  ctx->pending  = 0u;
  return 0;
}

int mbedtls_sha256_update(mbedtls_sha256_context *ctx,
                          const unsigned char *input, size_t ilen)
{
  size_t i;

  if (ctx == 0 || (input == 0 && ilen != 0u)) { return -1; }

  for (i = 0u; i < ilen; i++) {
    ctx->block[ctx->pending++] = input[i];
    ctx->total++;
    if (ctx->pending == 64u) {
      compress(ctx->state, ctx->block);
      ctx->pending = 0u;
    }
  }
  return 0;
}

int mbedtls_sha256_finish(mbedtls_sha256_context *ctx, unsigned char output[32])
{
  uint64_t      bits;
  unsigned char pad[72];
  unsigned      padlen, i;

  if (ctx == 0 || output == 0) { return -1; }

  /* Pad with 0x80, zeros, then the message length in bits. */
  bits   = ctx->total * 8u;
  padlen = (ctx->pending < 56u) ? (56u - ctx->pending) : (120u - ctx->pending);

  pad[0] = 0x80u;
  for (i = 1u; i < padlen; i++) { pad[i] = 0u; }
  for (i = 0u; i < 8u; i++) {
    pad[padlen + i] = (unsigned char)((bits >> (56u - 8u * i)) & 0xFFu);
  }
  if (mbedtls_sha256_update(ctx, pad, padlen + 8u) != 0) { return -1; }

  for (i = 0u; i < 8u; i++) {
    output[i * 4u]      = (unsigned char)((ctx->state[i] >> 24) & 0xFFu);
    output[i * 4u + 1u] = (unsigned char)((ctx->state[i] >> 16) & 0xFFu);
    output[i * 4u + 2u] = (unsigned char)((ctx->state[i] >> 8) & 0xFFu);
    output[i * 4u + 3u] = (unsigned char)(ctx->state[i] & 0xFFu);
  }
  return 0;
}
