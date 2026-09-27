/* chirp_sign.c — see chirp_sign.h. RFC 2104 HMAC over mbedTLS's SHA-256. */

#include "chirp_sign.h"

#include "mbedtls/sha256.h"

#define SHA_BLOCK 64u
#define SHA_DIGEST 32u

/* mbedTLS's SHA-256 over two chunks; ST's prebuilt archive already carries it for OpenThread. */
static int sha256(const unsigned char *a, unsigned an,
                  const unsigned char *b, unsigned bn, unsigned char out[SHA_DIGEST])
{
  mbedtls_sha256_context c;
  int rc;

  mbedtls_sha256_init(&c);
  rc = mbedtls_sha256_starts(&c, 0);                       /* 0 = SHA-256, not -224 */
  if (rc == 0 && an != 0u) { rc = mbedtls_sha256_update(&c, a, an); }
  if (rc == 0 && bn != 0u) { rc = mbedtls_sha256_update(&c, b, bn); }
  if (rc == 0)             { rc = mbedtls_sha256_finish(&c, out);   }
  mbedtls_sha256_free(&c);
  return rc;
}

/* HMAC uses inner and outer pads with SHA-256.
 * Reject keys longer than the 64-byte block; device keys are 32 bytes. */
static int hmac_sha256(const unsigned char *key, unsigned kn,
                       const unsigned char *msg, unsigned mn, unsigned char out[SHA_DIGEST])
{
  unsigned char ipad[SHA_BLOCK], opad[SHA_BLOCK], inner[SHA_DIGEST];
  unsigned      i;

  for (i = 0u; i < SHA_BLOCK; i++) {
    unsigned char k = (i < kn) ? key[i] : 0u;
    ipad[i] = (unsigned char)(k ^ 0x36u);
    opad[i] = (unsigned char)(k ^ 0x5Cu);
  }
  if (sha256(ipad, SHA_BLOCK, msg, mn, inner) != 0) { return -1; }
  return sha256(opad, SHA_BLOCK, inner, SHA_DIGEST, out);
}

/* Standard base64, padding optional. `n` is a maximum: a NUL ends the key.
 * Returns the byte count, or -1 on a stray character or overflow. */
static int b64dec(const char *s, unsigned n, unsigned char *out, unsigned outsz)
{
  unsigned acc = 0u, bits = 0u, o = 0u, i;

  for (i = 0u; i < n; i++) {
    char c = s[i];
    int  v;

    if (c == '\0')                                      { break;    }
    if (c == '=' || c == '\r' || c == '\n' || c == ' ') { continue; }
    if      (c >= 'A' && c <= 'Z') { v = c - 'A';      }
    else if (c >= 'a' && c <= 'z') { v = c - 'a' + 26; }
    else if (c >= '0' && c <= '9') { v = c - '0' + 52; }
    else if (c == '+')             { v = 62;           }
    else if (c == '/')             { v = 63;           }
    else                           { return -1;         }

    acc  = (acc << 6) | (unsigned)v;
    bits += 6u;
    if (bits >= 8u) {
      bits -= 8u;
      if (o >= outsz) { return -1; }
      out[o++] = (unsigned char)((acc >> bits) & 0xFFu);
    }
  }
  return (int)o;
}

int chirp_sign_fingerprint(char out[9], const char *s, unsigned n)
{
  static const char hex[] = "0123456789abcdef";
  unsigned char     d[SHA_DIGEST];
  unsigned          i;

  if (out == 0 || s == 0 || sha256((const unsigned char *)s, n, 0, 0u, d) != 0) { return -1; }
  for (i = 0u; i < 4u; i++) {
    out[2u * i]      = hex[(d[i] >> 4) & 0x0Fu];
    out[2u * i + 1u] = hex[d[i] & 0x0Fu];
  }
  out[8] = '\0';
  return 0;
}

int chirp_sign_hex(char *out, unsigned outsz,
                   const char *key_b64, unsigned key_n,
                   const unsigned char *msg, unsigned msg_n)
{
  static const char hex[] = "0123456789abcdef";
  unsigned char     key[SHA_BLOCK];
  unsigned char     mac[SHA_DIGEST];
  int               kn;
  unsigned          i;

  if (out == 0 || outsz < CHIRP_SIGN_HEX_LEN + 1u || key_b64 == 0) { return -1; }

  kn = b64dec(key_b64, key_n, key, sizeof key);
  if (kn <= 0) { return -1; }

  if (hmac_sha256(key, (unsigned)kn, msg, msg_n, mac) != 0) { return -1; }

  for (i = 0u; i < SHA_DIGEST; i++) {
    out[2u * i]      = hex[(mac[i] >> 4) & 0x0Fu];
    out[2u * i + 1u] = hex[mac[i] & 0x0Fu];
  }
  out[CHIRP_SIGN_HEX_LEN] = '\0';
  return 0;
}
