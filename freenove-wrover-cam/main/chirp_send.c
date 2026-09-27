#include <stdio.h>
#include <string.h>

#include "chirp_send.h"
#include "chirp_sign.h"
#include "wifi_ladder.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "chirp_send";

#define REPORT_PERIOD_MS 60000
#define HTTP_TIMEOUT_MS  15000
#define SNTP_WAIT_MS     15000
#define URL_MAX          160
#define BODY_MAX         256
#define SIGNED_MAX       128  /* "chirp-down\n<hwid>\n<nonce>" is the long one */

/* Tags are at most 32 chars (contract §Message tags). The signed prefix is
 * "<nonce>\n" then "tag=<tag>\n", inside the digest and never in the body. */
#define TAG_MAX          32
#define PREFIX_MAX       (CHIRP_NONCE_SIZE + 1 + sizeof("tag=\n") + TAG_MAX)

/* Maximum HTTP body size, including the JPEG bytes. */
#define FRAME_MAX_BYTES  (256 * 1024)

static chirp_config_t   s_config;
static chirp_report_fn  s_report;
static chirp_command_fn s_on_command;

/* Add credential and signature headers from the stored config.
 * Uplink signatures include the tag; downlink polls use a separate challenge. */
static esp_err_t set_auth_headers(esp_http_client_handle_t client,
                                  esp_http_client_method_t method,
                                  const char *tag,
                                  const uint8_t *body, size_t body_len)
{
    if (s_config.cred_kind == CHIRP_CRED_NONE) {
        return ESP_OK;  /* an empty token header would read as "token present
                           and wrong", not "open device" */
    }
    esp_http_client_set_header(client, "X-Chirp-Token", s_config.cred);
    if (s_config.cred_kind == CHIRP_CRED_CLAIM) {
        /* A spent claim code becomes the token, so sending this header forever stays correct. */
        esp_http_client_set_header(client, "X-Chirp-Claim", s_config.cred);
    }
    if (!s_config.has_key) {
        return ESP_OK;
    }

    char    nonce[CHIRP_NONCE_SIZE];
    char    sig[CHIRP_SIG_HEX_SIZE];
    uint8_t challenge[SIGNED_MAX];
    esp_err_t err;

    /* Omit the nonce when the clock is below the usable-time threshold. */
    bool have_nonce = chirp_sign_nonce(nonce);

    if (method == HTTP_METHOD_POST) {
        /* Hash optional nonce and tag lines, followed by the body.
         * Stream the prefix and JPEG separately to avoid copying the frame. */
        char prefix[PREFIX_MAX];
        int  plen = 0;
        if (have_nonce) {
            plen = snprintf(prefix, sizeof(prefix), "%s\n", nonce);
            if (plen < 0 || (size_t)plen >= sizeof(prefix)) {
                return ESP_ERR_INVALID_SIZE;
            }
        }
        if (tag != NULL) {
            int n = snprintf(prefix + plen, sizeof(prefix) - (size_t)plen,
                             "tag=%s\n", tag);
            if (n < 0 || (size_t)n >= sizeof(prefix) - (size_t)plen) {
                return ESP_ERR_INVALID_SIZE;
            }
            plen += n;
        }
        err = chirp_sign_hex2(s_config.key, sizeof(s_config.key),
                              (const uint8_t *)prefix, (size_t)plen,
                              body, body_len, sig);
    } else {
        /* Sign the downlink challenge: chirp-down, hardware ID, and nonce.
         * Without a usable clock, leave the poll token-only. */
        if (!have_nonce) {
            static bool warned;
            if (!warned) {
                warned = true;
                ESP_LOGW(TAG, "no clock yet — the downlink poll cannot be signed, "
                              "so queued commands wait for SNTP");
            }
            return ESP_OK;
        }
        int n = snprintf((char *)challenge, sizeof(challenge), "chirp-down\n%s\n%s",
                         chirp_store_hwid(&s_config), nonce);
        if (n < 0 || (size_t)n >= sizeof(challenge)) {
            return ESP_ERR_INVALID_SIZE;
        }
        err = chirp_sign_hex(s_config.key, sizeof(s_config.key), challenge,
                             (size_t)n, sig);
    }

    if (err != ESP_OK) {
        return err;
    }
    if (have_nonce) {
        esp_http_client_set_header(client, "X-Chirp-Nonce", nonce);
    }
    esp_http_client_set_header(client, "X-Chirp-Signature", sig);
    return ESP_OK;
}

static esp_err_t http_exchange(const char *url, esp_http_client_method_t method,
                               const char *tag,
                               const uint8_t *body, size_t body_len,
                               int *out_status, char *out_body, size_t out_body_size)
{
    esp_http_client_config_t http_config = {
        .url               = url,
        .method            = method,
        .timeout_ms        = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = set_auth_headers(client, method, tag, body, body_len);
    if (err == ESP_OK && method == HTTP_METHOD_POST) {
        /* Send the JPEG as the HTTP body, with its tag in a separate header. */
        esp_http_client_set_header(client, "Content-Type", "image/jpeg");
        if (tag != NULL) {
            esp_http_client_set_header(client, "X-Chirp-Tag", tag);
        }
    }

    if (err == ESP_OK) {
        err = esp_http_client_open(client, (int)body_len);
    }
    if (err == ESP_OK && body_len > 0) {
        int written = esp_http_client_write(client, (const char *)body, body_len);
        if (written != (int)body_len) {
            err = ESP_FAIL;
        }
    }
    if (err == ESP_OK && esp_http_client_fetch_headers(client) < 0) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        int read = esp_http_client_read_response(client, out_body, (int)out_body_size - 1);
        if (read < 0) {
            err = ESP_FAIL;
        } else {
            out_body[read] = '\0';
            *out_status = esp_http_client_get_status_code(client);
        }
    }

    esp_http_client_cleanup(client);
    return err;
}

/* ---------------------------------------------------------------------------
 * uplink
 * ------------------------------------------------------------------------ */

chirp_send_result_t chirp_send_bytes(const uint8_t *bytes, size_t length,
                                     const char *tag)
{
    if (bytes == NULL || length == 0) {
        return CHIRP_SKIPPED;
    }
    if (length > FRAME_MAX_BYTES) {
        ESP_LOGW(TAG, "message is %u bytes, over the %d byte cap — dropped",
                 (unsigned)length, FRAME_MAX_BYTES);
        return CHIRP_SKIPPED;
    }

    char url[URL_MAX];
    char body[BODY_MAX];
    int  status = 0;

    snprintf(url, sizeof(url), "https://%s/ingest/%s", s_config.host,
             chirp_store_hwid(&s_config));
    esp_err_t err = http_exchange(url, HTTP_METHOD_POST, tag, bytes, length,
                                  &status, body, sizeof(body));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "POST failed: %s", esp_err_to_name(err));
        return CHIRP_CONNECTION_FAILED;
    }
    ESP_LOGI(TAG, "%u bytes tag=%s -> POST %d %s", (unsigned)length, tag,
             status, body);
    return CHIRP_SENT;
}

/* ---------------------------------------------------------------------------
 * downlink
 * ------------------------------------------------------------------------ */

/* Writes the queued command into `out`. Returns false when the queue was empty
 * or the poll did not complete. */
static bool poll_down(char *out, size_t out_size)
{
    char url[URL_MAX];
    int  status = 0;

    out[0] = '\0';
    snprintf(url, sizeof(url), "https://%s/ingest/%s/down", s_config.host,
             chirp_store_hwid(&s_config));
    esp_err_t err = http_exchange(url, HTTP_METHOD_GET, NULL, NULL, 0, &status,
                                  out, out_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "downlink poll failed: %s", esp_err_to_name(err));
        out[0] = '\0';
        return false;
    }
    if (status == 200) {
        if (strncmp(out, "CHIRPKEY1 ", 10) != 0) {
            ESP_LOGI(TAG, "downlink: %s", out);  /* key messages stay off the log */
        }
        return out[0] != '\0';
    }
    if (status != 204) {
        ESP_LOGW(TAG, "downlink poll returned %d %s", status, out);
    }
    out[0] = '\0';
    return false;
}

/* ---------------------------------------------------------------------------
 * the loop
 * ------------------------------------------------------------------------ */

static void report_once(void)
{
    /* The message the timer asked for, then the poll behind it. */
    if (s_report(CHIRP_TAG_HEARTBEAT) != CHIRP_SENT) {
        return;
    }

    char command[BODY_MAX];
    if (!poll_down(command, sizeof(command)) || s_on_command == NULL) {
        return;
    }

    /* Reserve CHIRPKEY1 for key messages; this demo accepts keys through USB. */
    if (strncmp(command, "CHIRPKEY1 ", 10) == 0) {
        ESP_LOGW(TAG, "downlink carries a signing key; this demo collects keys "
                      "over CHIRP-PROV, not over the air");
        return;
    }

    /* The command callback returns a tag for a requested report, or NULL to skip it. */
    const char *answer_tag = s_on_command(command);
    if (answer_tag != NULL) {
        s_report(answer_tag);
    }
}

/* The core refuses a nonce more than five minutes from its clock, so a signing
 * device needs real time. A device with no key never asks for it. */
static void sync_clock(void)
{
    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&sntp_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sntp init: %s", esp_err_to_name(err));
        return;
    }
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(SNTP_WAIT_MS)) != ESP_OK) {
        /* SNTP keeps retrying in the background; the 401s until then are not a bad key. */
        ESP_LOGW(TAG, "clock not set yet — signed messages are refused until it is");
        return;
    }
    ESP_LOGI(TAG, "clock synced, signing enabled");
}

static void send_task(void *arg)
{
    if (s_config.has_key && wifi_ladder_wait_connected(portMAX_DELAY)) {
        sync_clock();
    }
    for (;;) {
        if (!wifi_ladder_wait_connected(portMAX_DELAY)) {
            continue;
        }
        report_once();
        /* CHIRP-PROV is still answered during this gap: the responder is its own task. */
        vTaskDelay(pdMS_TO_TICKS(REPORT_PERIOD_MS));
    }
}

esp_err_t chirp_send_start(const chirp_config_t *config,
                           chirp_report_fn report,
                           chirp_command_fn on_command)
{
    if (config == NULL || report == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config     = *config;
    s_report     = report;
    s_on_command = on_command;

    /* Reserve task stack space for TLS; the JPEG buffer lives in PSRAM. */
    if (xTaskCreate(send_task, "chirp_send", 8192, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
