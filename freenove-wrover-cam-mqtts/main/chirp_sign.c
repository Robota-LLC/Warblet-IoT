#include "chirp_sign.h"

#include "mbedtls/md.h"

esp_err_t chirp_sign_raw(const uint8_t *key, size_t key_len,
                         const uint8_t *a, size_t a_len,
                         const uint8_t *b, size_t b_len,
                         uint8_t out[CHIRP_SIG_SIZE])
{
    const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_context_t     ctx;
    int                      rc;

    if (sha256 == NULL) {
        return ESP_FAIL;
    }
    /* Hash the tag prefix and frame in sequence without joining them in memory. */
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
        rc = mbedtls_md_hmac_finish(&ctx, out);
    }
    mbedtls_md_free(&ctx);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}
