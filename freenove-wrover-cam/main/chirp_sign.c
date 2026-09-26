#include <inttypes.h>
#include <stdio.h>
#include <sys/time.h>

#include "chirp_sign.h"

#include "mbedtls/md.h"

esp_err_t chirp_sign_hex2(const uint8_t *key, size_t key_len,
                          const uint8_t *a, size_t a_len,
                          const uint8_t *b, size_t b_len,
                          char out[CHIRP_SIG_HEX_SIZE])
{
    const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_context_t     ctx;
    uint8_t                  mac[32];
    int                      rc;

    if (sha256 == NULL) {
        return ESP_FAIL;
    }
    /* Hash a then b without allocating a combined buffer. */
    mbedtls_md_init(&ctx);
    rc = mbedtls_md_setup(&ctx, sha256, 1 /* HMAC */);
    if (rc == 0) {
        rc = mbedtls_md_hmac_starts(&ctx, key, key_len);
    }
    if (rc == 0 && a_len > 0) {
        rc = mbedtls_md_hmac_update(&ctx, a, a_len);
    }
    if (rc == 0 && b_len > 0) {
        rc = mbedtls_md_hmac_update(&ctx, b, b_len);
    }
    if (rc == 0) {
        rc = mbedtls_md_hmac_finish(&ctx, mac);
    }
    mbedtls_md_free(&ctx);
    if (rc != 0) {
        return ESP_FAIL;
    }

    for (size_t i = 0; i < sizeof(mac); i++) {
        snprintf(out + i * 2, 3, "%02x", mac[i]);
    }
    return ESP_OK;
}

esp_err_t chirp_sign_hex(const uint8_t *key, size_t key_len,
                         const uint8_t *msg, size_t msg_len,
                         char out[CHIRP_SIG_HEX_SIZE])
{
    return chirp_sign_hex2(key, key_len, msg, msg_len, NULL, 0, out);
}

/* Treat times below the threshold as an unset clock. */
#define CLOCK_SANE_SECS 1735689600

bool chirp_sign_nonce(char out[CHIRP_NONCE_SIZE])
{
    static int64_t last;

    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    if (tv.tv_sec < CLOCK_SANE_SECS) {
        return false;
    }
    int64_t ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    /* The core requires strictly increasing nonces, and an uplink and its
     * downlink poll can share a millisecond. */
    if (ms <= last) {
        ms = last + 1;
    }
    last = ms;
    snprintf(out, CHIRP_NONCE_SIZE, "%" PRId64, ms);
    return true;
}
