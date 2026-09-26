/* HMAC-SHA256 for MQTT payload prefixes, with no timestamp nonce. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Size of the HMAC-SHA256 prefix in a signed message. */
#define CHIRP_SIG_SIZE 32

/* Return a 32-byte HMAC-SHA256 over a followed by b.
 * a may be NULL when a_len is zero. */
esp_err_t chirp_sign_raw(const uint8_t *key, size_t key_len,
                         const uint8_t *a, size_t a_len,
                         const uint8_t *b, size_t b_len,
                         uint8_t out[CHIRP_SIG_SIZE]);

#ifdef __cplusplus
}
#endif
