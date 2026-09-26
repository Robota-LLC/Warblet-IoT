#include "chirp_sign.h"

#include "mbedtls/md.h"

esp_err_t chirp_sign_raw(const uint8_t *key, size_t key_len,
                         const uint8_t *msg, size_t msg_len,
                         uint8_t out[CHIRP_SIG_SIZE])
{
    const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    if (sha256 == NULL || mbedtls_md_hmac(sha256, key, key_len, msg, msg_len, out) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
