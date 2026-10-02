/* chirp_sign.c — see chirp_sign.h. RFC 2104, no libc, no hardware. */

#include "chirp_sign.h"
#include "sha256.h"

/* Append a string (NULL appends nothing) and return the new length. It keeps
 * counting past outsz so the caller can tell that the text did not fit. */
static unsigned put(char *out, unsigned outsz, unsigned n, const char *s)
{
  unsigned i;
  for (i = 0u; s != 0 && s[i] != '\0'; i++) {
    if (n < outsz) { out[n] = s[i]; }
    n++;
  }
  return n;
}

int chirp_sign_prefix(char *out, unsigned outsz,
                      const char *nonce, const char *tag)
{
  unsigned n = 0u;

  /* <nonce> "\n" "tag=" <tag> "\n", with empty fields left empty. */
  n = put(out, outsz, n, nonce);
  n = put(out, outsz, n, "\ntag=");
  n = put(out, outsz, n, tag);
  n = put(out, outsz, n, "\n");

  if (n >= outsz) { return -1; }   /* the NUL needs a byte too */
  out[n] = '\0';
  return (int)n;
}

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
