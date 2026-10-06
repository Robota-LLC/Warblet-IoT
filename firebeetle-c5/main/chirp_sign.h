/* HMAC-SHA256 for MQTT and TCP payload prefixes, with no timestamp nonce. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Size of the HMAC-SHA256 prefix in a signed message. */
#define CHIRP_SIG_SIZE 32

/* A tag is at most 32 characters; a nonce is a decimal millisecond time. */
#define CHIRP_TAG_MAX   32
#define CHIRP_NONCE_MAX 23

/* Room for the longest prefix chirp_sign_prefix() writes, NUL included. */
#define CHIRP_SIGN_PREFIX_MAX (CHIRP_NONCE_MAX + CHIRP_TAG_MAX + sizeof("\ntag=\n"))

/* Write the two lines every signature covers in front of the body:
 *     <nonce> "\n" "tag=" <tag> "\n"
 * Both lines are always written. NULL or "" means the message has no such
 * field, and it is signed as empty. Returns the length, or -1 if it does not
 * fit in outsz. */
int chirp_sign_prefix(char *out, size_t outsz, const char *nonce, const char *tag);

/* Return a 32-byte HMAC-SHA256 over a followed by b.
 * a may be NULL when a_len is zero. */
esp_err_t chirp_sign_raw(const uint8_t *key, size_t key_len,
                         const uint8_t *a, size_t a_len,
                         const uint8_t *b, size_t b_len,
                         uint8_t out[CHIRP_SIG_SIZE]);

#ifdef __cplusplus
}
#endif
