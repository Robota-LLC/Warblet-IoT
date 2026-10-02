/* Hex HMAC-SHA256 and increasing timestamp nonces for HTTP. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 32 digest bytes as lowercase hex, + NUL. */
#define CHIRP_SIG_HEX_SIZE 65

/* A millisecond Unix timestamp in decimal, + NUL. */
#define CHIRP_NONCE_SIZE 24

/* A tag is at most 32 characters. */
#define CHIRP_TAG_MAX 32

/* Room for the longest prefix chirp_sign_prefix() writes, NUL included. */
#define CHIRP_SIGN_PREFIX_MAX (CHIRP_NONCE_SIZE + CHIRP_TAG_MAX + sizeof("\ntag=\n"))

/* Write the two lines every signature covers in front of the body:
 *     <nonce> "\n" "tag=" <tag> "\n"
 * Both lines are always written. NULL or "" means the message has no such
 * field, and it is signed as empty. Returns the length, or -1 if it does not
 * fit in outsz. */
int chirp_sign_prefix(char *out, size_t outsz, const char *nonce, const char *tag);

/* out = hex hmac-sha256(key, msg), lowercase, NUL terminated. */
esp_err_t chirp_sign_hex(const uint8_t *key, size_t key_len,
                         const uint8_t *msg, size_t msg_len,
                         char out[CHIRP_SIG_HEX_SIZE]);

/* Hash two byte ranges as one input without copying the frame.
 * b may be NULL when b_len is zero. */
esp_err_t chirp_sign_hex2(const uint8_t *key, size_t key_len,
                          const uint8_t *a, size_t a_len,
                          const uint8_t *b, size_t b_len,
                          char out[CHIRP_SIG_HEX_SIZE]);

/* Return an increasing Unix-millisecond nonce, or false if the clock is unset. */
bool chirp_sign_nonce(char out[CHIRP_NONCE_SIZE]);

#ifdef __cplusplus
}
#endif
