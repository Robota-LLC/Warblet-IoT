/* Produce a hex HMAC-SHA256 for the HTTP signature header.
 * The sender decides which bytes to sign and whether a key is available. */
#ifndef CHIRP_SIGN_H
#define CHIRP_SIGN_H

/* A tag is at most 32 characters; a nonce is a decimal millisecond time.
 * This demo sends neither, but the signed bytes always have a place for both. */
#define CHIRP_TAG_MAX   32u
#define CHIRP_NONCE_MAX 23u

/* Room for the longest prefix chirp_sign_prefix() writes, NUL included. */
#define CHIRP_SIGN_PREFIX_MAX (CHIRP_NONCE_MAX + CHIRP_TAG_MAX + sizeof("\ntag=\n"))

/* Write the two lines every signature covers in front of the body:
 *     <nonce> "\n" "tag=" <tag> "\n"
 * Both lines are always written. NULL or "" means the message has no such
 * field, and it is signed as empty, so a message with neither starts with
 * "\ntag=\n". Keeping both lines means a nonce or tag cannot be moved into the
 * body and still match the same signature.
 * Returns the length without the NUL, or -1 if it does not fit in outsz. */
int chirp_sign_prefix(char *out, unsigned outsz,
                      const char *nonce, const char *tag);

/* HMAC-SHA256 (RFC 2104) as 64 lowercase hex characters plus a NUL: the
 * X-Chirp-Signature header value. msg is the prefix above followed by the body. */
void chirp_sign(const unsigned char *key, unsigned klen,
                const void *msg, unsigned mlen, char out_hex[65]);

/* First four bytes of SHA-256(data) as 8 hex characters plus a NUL, to tell
 * two secrets apart without printing either. */
void chirp_fingerprint(const void *data, unsigned len, char out_hex[9]);

#endif /* CHIRP_SIGN_H */
