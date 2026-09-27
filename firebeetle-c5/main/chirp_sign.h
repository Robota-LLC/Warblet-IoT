/* HMAC-SHA256 output for TCP payload prefixes.
 * The transport calls this when a signing key is stored; no nonce is included. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Size of the HMAC-SHA256 prefix in a signed message. */
#define CHIRP_SIG_SIZE 32

/* Return the 32-byte HMAC-SHA256 of msg. */
esp_err_t chirp_sign_raw(const uint8_t *key, size_t key_len,
                         const uint8_t *msg, size_t msg_len,
                         uint8_t out[CHIRP_SIG_SIZE]);

#ifdef __cplusplus
}
#endif
