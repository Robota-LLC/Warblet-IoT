/* SHA-256 using a 64-word message schedule and the FIPS 180-4 round function. */

#include "sha256.h"

_Static_assert(sizeof(unsigned) == 4, "sha256.c assumes a 32-bit unsigned");

static const unsigned K[64] = {
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

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void compress(unsigned h[8], const unsigned char *p)
{
  unsigned w[64];
  unsigned a, b, c, d, e, f, g, hh;
  int i;

  for (i = 0; i < 16; i++) {
    w[i] = ((unsigned)p[i * 4 + 0] << 24) | ((unsigned)p[i * 4 + 1] << 16) |
           ((unsigned)p[i * 4 + 2] <<  8) |  (unsigned)p[i * 4 + 3];
  }
  for (i = 16; i < 64; i++) {
    unsigned s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
    unsigned s1 = ROR(w[i -  2], 17) ^ ROR(w[i -  2], 19) ^ (w[i -  2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  a = h[0]; b = h[1]; c = h[2]; d = h[3];
  e = h[4]; f = h[5]; g = h[6]; hh = h[7];

  for (i = 0; i < 64; i++) {
    unsigned S1  = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
    unsigned ch  = (e & f) ^ ((~e) & g);
    unsigned t1  = hh + S1 + ch + K[i] + w[i];
    unsigned S0  = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
    unsigned maj = (a & b) ^ (a & c) ^ (b & c);
    unsigned t2  = S0 + maj;

    hh = g; g = f; f = e; e = d + t1;
    d  = c; c = b; b = a; a = t1 + t2;
  }

  h[0] += a; h[1] += b; h[2] += c; h[3] += d;
  h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void sha256_init(sha256_ctx *c)
{
  c->h[0] = 0x6a09e667u; c->h[1] = 0xbb67ae85u;
  c->h[2] = 0x3c6ef372u; c->h[3] = 0xa54ff53au;
  c->h[4] = 0x510e527fu; c->h[5] = 0x9b05688cu;
  c->h[6] = 0x1f83d9abu; c->h[7] = 0x5be0cd19u;
  c->nbits_lo = 0u;
  c->nbits_hi = 0u;
  c->fill     = 0u;
}

void sha256_update(sha256_ctx *c, const void *data, unsigned len)
{
  const unsigned char *p = (const unsigned char *)data;
  unsigned lo = c->nbits_lo + (len << 3);

  if (lo < c->nbits_lo) { c->nbits_hi++; }        /* carry out of the low word */
  c->nbits_lo = lo;
  c->nbits_hi += (len >> 29);

  while (len--) {
    c->buf[c->fill++] = *p++;
    if (c->fill == SHA256_BLOCK) {
      compress(c->h, c->buf);
      c->fill = 0u;
    }
  }
}

void sha256_final(sha256_ctx *c, unsigned char out[SHA256_DIGEST])
{
  unsigned hi = c->nbits_hi, lo = c->nbits_lo;
  int i;

  c->buf[c->fill++] = 0x80u;                      /* the mandatory 1 bit */
  if (c->fill > SHA256_BLOCK - 8u) {              /* no room for the length */
    while (c->fill < SHA256_BLOCK) { c->buf[c->fill++] = 0u; }
    compress(c->h, c->buf);
    c->fill = 0u;
  }
  while (c->fill < SHA256_BLOCK - 8u) { c->buf[c->fill++] = 0u; }

  c->buf[56] = (unsigned char)(hi >> 24); c->buf[57] = (unsigned char)(hi >> 16);
  c->buf[58] = (unsigned char)(hi >>  8); c->buf[59] = (unsigned char)hi;
  c->buf[60] = (unsigned char)(lo >> 24); c->buf[61] = (unsigned char)(lo >> 16);
  c->buf[62] = (unsigned char)(lo >>  8); c->buf[63] = (unsigned char)lo;
  compress(c->h, c->buf);

  for (i = 0; i < 8; i++) {
    out[i * 4 + 0] = (unsigned char)(c->h[i] >> 24);
    out[i * 4 + 1] = (unsigned char)(c->h[i] >> 16);
    out[i * 4 + 2] = (unsigned char)(c->h[i] >>  8);
    out[i * 4 + 3] = (unsigned char)(c->h[i]);
  }
}

void sha256(const void *data, unsigned len, unsigned char out[SHA256_DIGEST])
{
  sha256_ctx c;
  sha256_init(&c);
  sha256_update(&c, data, len);
  sha256_final(&c, out);
}
