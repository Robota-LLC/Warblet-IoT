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
