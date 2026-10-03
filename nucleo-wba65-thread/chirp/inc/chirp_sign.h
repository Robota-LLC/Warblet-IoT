/* Compute hex HMAC-SHA256 from a base64 key and message using mbedTLS. */
#ifndef CHIRP_SIGN_H
#define CHIRP_SIGN_H

/* Length of the hex digest, excluding the NUL. */
#define CHIRP_SIGN_HEX_LEN 64u

/* A tag is at most 32 characters; a nonce is a decimal millisecond time.
 * This demo sends neither, but the signed bytes always have a place for both. */
#define CHIRP_TAG_MAX   32u
#define CHIRP_NONCE_MAX 23u

/* Room for the longest prefix chirp_sign_prefix() writes, NUL included. */
#define CHIRP_SIGN_PREFIX_MAX (CHIRP_NONCE_MAX + CHIRP_TAG_MAX + sizeof("\ntag=\n"))

/* Write the two lines every signature covers in front of the payload:
 *     <nonce> "\n" "tag=" <tag> "\n"
 * Both lines are always written. NULL or "" means the message has no such
 * field, and it is signed as empty, so a message with neither starts with
 * "\ntag=\n". Keeping both lines means a nonce or tag cannot be moved into the
 * payload and still match the same signature.
 * Returns the length without the NUL, or -1 if it does not fit in outsz. */
int chirp_sign_prefix(char *out, unsigned outsz,
                      const char *nonce, const char *tag);

/* Write a NUL-terminated hex HMAC-SHA256; out needs CHIRP_SIGN_HEX_LEN+1 bytes.
 * msg is the prefix above followed by the payload.
 * key_n bounds the key string. Return 0 on success, -1 for invalid input or digest failure. */
int chirp_sign_hex(char *out, unsigned outsz,
                   const char *key_b64, unsigned key_n,
                   const unsigned char *msg, unsigned msg_n);

/* The first four bytes of SHA-256(s) as 8 hex characters plus a NUL: tells two
 * stored secrets apart on a console without printing any of either.
 * Return 0 on success, -1 if the digest failed. */
int chirp_sign_fingerprint(char out[9], const char *s, unsigned n);

#endif /* CHIRP_SIGN_H */
