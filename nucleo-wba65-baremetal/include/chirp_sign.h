/* Produce a hex HMAC-SHA256 for the HTTP signature header.
 * The sender decides which bytes to sign and whether a key is available. */
#ifndef CHIRP_SIGN_H
#define CHIRP_SIGN_H

/* HMAC-SHA256 (RFC 2104) as 64 lowercase hex characters plus a NUL: the
 * X-Chirp-Signature header value. */
void chirp_sign(const unsigned char *key, unsigned klen,
                const void *msg, unsigned mlen, char out_hex[65]);

/* First four bytes of SHA-256(data) as 8 hex characters plus a NUL, to tell
 * two secrets apart without printing either. */
void chirp_fingerprint(const void *data, unsigned len, char out_hex[9]);

#endif /* CHIRP_SIGN_H */
