/* Compute hex HMAC-SHA256 from a base64 key and message using mbedTLS. */
#ifndef CHIRP_SIGN_H
#define CHIRP_SIGN_H

/* Length of the hex digest, excluding the NUL. */
#define CHIRP_SIGN_HEX_LEN 64u

/* Write a NUL-terminated hex HMAC-SHA256; out needs CHIRP_SIGN_HEX_LEN+1 bytes.
 * key_n bounds the key string. Return 0 on success, -1 for invalid input or digest failure. */
int chirp_sign_hex(char *out, unsigned outsz,
                   const char *key_b64, unsigned key_n,
                   const unsigned char *msg, unsigned msg_n);

/* The first four bytes of SHA-256(s) as 8 hex characters plus a NUL: tells two
 * stored secrets apart on a console without printing any of either.
 * Return 0 on success, -1 if the digest failed. */
int chirp_sign_fingerprint(char out[9], const char *s, unsigned n);

#endif /* CHIRP_SIGN_H */
