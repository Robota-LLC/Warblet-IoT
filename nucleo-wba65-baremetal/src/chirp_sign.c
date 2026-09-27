/* chirp_sign.c — see chirp_sign.h. RFC 2104, no libc, no hardware. */

#include "chirp_sign.h"
#include "sha256.h"

void chirp_sign(const unsigned char *key, unsigned klen,
                const void *msg, unsigned mlen, char out_hex[65])
{
  static const char H[] = "0123456789abcdef";
  unsigned char k[SHA256_BLOCK], pad[SHA256_BLOCK], d[SHA256_DIGEST];
  sha256_ctx c;
  unsigned i;

  /* RFC 2104: hash keys longer than the block, zero-pad shorter ones. */
  for (i = 0u; i < SHA256_BLOCK; i++) { k[i] = 0u; }
  if (klen > SHA256_BLOCK) { sha256(key, klen, k); }
  else                     { for (i = 0u; i < klen; i++) { k[i] = key[i]; } }

  for (i = 0u; i < SHA256_BLOCK; i++) { pad[i] = k[i] ^ 0x36u; }   /* ipad */
  sha256_init(&c);
  sha256_update(&c, pad, SHA256_BLOCK);
  sha256_update(&c, msg, mlen);
  sha256_final(&c, d);

  for (i = 0u; i < SHA256_BLOCK; i++) { pad[i] = k[i] ^ 0x5Cu; }   /* opad */
  sha256_init(&c);
  sha256_update(&c, pad, SHA256_BLOCK);
  sha256_update(&c, d, SHA256_DIGEST);
  sha256_final(&c, d);

  for (i = 0u; i < SHA256_DIGEST; i++) {
    out_hex[i * 2u]      = H[d[i] >> 4];
    out_hex[i * 2u + 1u] = H[d[i] & 0xFu];
  }
  out_hex[64] = '\0';
}

void chirp_fingerprint(const void *data, unsigned len, char out_hex[9])
{
  static const char H[] = "0123456789abcdef";
  unsigned char d[SHA256_DIGEST];
  unsigned i;

  sha256(data, len, d);
  for (i = 0u; i < 4u; i++) {
    out_hex[i * 2u]      = H[d[i] >> 4];
    out_hex[i * 2u + 1u] = H[d[i] & 0xFu];
  }
  out_hex[8] = '\0';
}
