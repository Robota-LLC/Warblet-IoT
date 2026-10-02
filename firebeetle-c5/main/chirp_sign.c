#include <stdio.h>

#include "chirp_sign.h"

#include "mbedtls/md.h"

int chirp_sign_prefix(char *out, size_t outsz, const char *nonce, const char *tag)
{
    /* signed input = <nonce> "\n" "tag=" <tag> "\n" <body>; a missing field is empty. */
    int n = snprintf(out, outsz, "%s\ntag=%s\n",
                     nonce != NULL ? nonce : "",
                     tag != NULL ? tag : "");

    if (n < 0 || (size_t)n >= outsz) {
        return -1;
    }
    return n;
}

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
    /* Hash the prefix and the payload in turn without joining them in memory. */
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
