#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "chirp_mqtt.h"
#include "chirp_sign.h"
#include "wifi_ladder.h"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "mqtt_client.h"

static const char *TAG = "chirp_mqtt";

#define REPORT_PERIOD_MS 300000
#define CONNECT_WAIT_MS 20000

/* Reconnect backoff, bounded and exponential. */
#define BACKOFF_MIN_MS 2000
#define BACKOFF_MAX_MS 60000

/* Keepalive detects broken connections between scheduled reports. */
#define KEEPALIVE_S 60

/* A large output buffer keeps camera frames from fragmenting; QoS 0 never uses the outbox. */
#define MQTT_OUT_BUFFER   32768
#define MQTT_IN_BUFFER     1024
#define MQTT_OUTBOX_LIMIT (128 * 1024)

/* Maximum published message size, including a signature prefix if present. */
#define FRAME_MAX_BYTES (256 * 1024)

#define URI_MAX     96
#define TOPIC_MAX   (16 + CHIRP_HWID_SIZE)
#define COMMAND_MAX 64

/* Tags are at most 32 chars (contract §Message tags): one extra topic level,
 * and one line inside the signature. */
#define TAG_MAX        32
#define TOPIC_TAG_MAX  (TOPIC_MAX + 1 + TAG_MAX)
#define TAG_PREFIX_MAX (sizeof("tag=\n") + TAG_MAX)

/* BIT_LOST ends the heartbeat wait, so a dropped session does not cost five minutes. */
#define BIT_CONNECTED BIT0
#define BIT_SNAP      BIT1
#define BIT_LOST      BIT2

static chirp_config_t           s_config;
static chirp_report_fn          s_report;
static chirp_command_fn         s_on_command;
static esp_mqtt_client_handle_t s_client;
static EventGroupHandle_t       s_bits;
static char                     s_topic_up[TOPIC_MAX];
static char                     s_topic_down[TOPIC_MAX];

/* Pass the callback's constant tag pointer to the send task through BIT_SNAP. */
static const char *s_requested_tag;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* ---------------------------------------------------------------------------
 * downlink
 * ------------------------------------------------------------------------ */

static void handle_down(const char *data, int len)
{
    /* Reserve CHIRPKEY1 for key messages; this demo accepts keys through USB. */
    if (len >= 10 && memcmp(data, "CHIRPKEY1 ", 10) == 0) {
        ESP_LOGW(TAG, "downlink carries a signing key; this demo collects keys "
                      "over CHIRP-PROV, not over the air");
        return;
    }

    /* A downlink arrives unterminated; anything that does not fit is not a
     * command this board has. */
    char command[COMMAND_MAX];
    if (len <= 0 || (size_t)len >= sizeof(command)) {
        ESP_LOGI(TAG, "downlink ignored (%d bytes)", len);
        return;
    }
    memcpy(command, data, (size_t)len);
    command[len] = '\0';

    if (s_on_command == NULL) {
        return;
    }

    /* The callback only picks a tag; the capture runs on the send task, not
     * the MQTT client's. */
    const char *tag = s_on_command(command);
    if (tag != NULL) {
        s_requested_tag = tag;
        xEventGroupSetBits(s_bits, BIT_SNAP);
    }
}

/* ---------------------------------------------------------------------------
 * the session
 * ------------------------------------------------------------------------ */

/* Events are logged with their numeric id, which is what a pasted log shows. */
static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)data;

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGD(TAG, "event %d BEFORE_CONNECT", (int)id);
        break;

    case MQTT_EVENT_CONNECTED:
        xEventGroupClearBits(s_bits, BIT_LOST);
        xEventGroupSetBits(s_bits, BIT_CONNECTED);
        /* Subscribe after every connection, including resumed sessions. */
        ESP_LOGI(TAG, "event %d CONNECTED, session_present=%d", (int)id,
                 e->session_present);
        if (esp_mqtt_client_subscribe(s_client, s_topic_down, 0) < 0) {
            ESP_LOGE(TAG, "subscribe to %s failed -- no commands will arrive",
                     s_topic_down);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        xEventGroupClearBits(s_bits, BIT_CONNECTED);
        xEventGroupSetBits(s_bits, BIT_LOST);
        ESP_LOGW(TAG, "event %d DISCONNECTED", (int)id);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "event %d SUBSCRIBED %s", (int)id, s_topic_down);
        break;

    case MQTT_EVENT_DATA:
        /* A fragmented command would need reassembly; this board's commands are short. */
        if (e->total_data_len != e->data_len) {
            ESP_LOGW(TAG, "event %d DATA fragmented (%d of %d), ignored",
                     (int)id, e->data_len, e->total_data_len);
            break;
        }
        handle_down(e->data, e->data_len);
        break;

    case MQTT_EVENT_ERROR:
        /* A refused CONNECT and a refused certificate both land here. An
         * esp_err_t is positive; an mbedtls error is negative and quoted as -0x.... */
        ESP_LOGE(TAG, "event %d ERROR type=%d connack=%d esp_tls=0x%x "
                      "tls_stack=-0x%04x sock_errno=%d",
                 (int)id, e->error_handle->error_type,
                 e->error_handle->connect_return_code,
                 e->error_handle->esp_tls_last_esp_err,
                 -e->error_handle->esp_tls_stack_err,
                 e->error_handle->esp_transport_sock_errno);
        break;

    default:
        ESP_LOGD(TAG, "event %d", (int)id);
        break;
    }
}

/* ---------------------------------------------------------------------------
 * uplink
 * ------------------------------------------------------------------------ */

/* Publish on the uplink topic with the tag as the last level. With a key, the
 * tag line and payload are signed and the raw digest prefixes the payload. */
chirp_send_result_t chirp_mqtt_send_bytes(const uint8_t *bytes, size_t length,
                                          const char *tag)
{
    if (bytes == NULL || length == 0 || tag == NULL) {
        return CHIRP_SKIPPED;
    }
    if (length > FRAME_MAX_BYTES) {
        ESP_LOGW(TAG, "message is %u bytes, over the %d byte cap -- dropped",
                 (unsigned)length, FRAME_MAX_BYTES);
        return CHIRP_SKIPPED;
    }

    char topic[TOPIC_TAG_MAX];
    snprintf(topic, sizeof(topic), "%s/%s", s_topic_up, tag);

    const uint8_t *body     = bytes;
    size_t         body_len = length;
    uint8_t       *envelope = NULL;

    if (s_config.has_key) {
        envelope = heap_caps_malloc(CHIRP_SIG_SIZE + length, MALLOC_CAP_SPIRAM);
        if (envelope == NULL) {
            ESP_LOGW(TAG, "no room for a %u byte envelope -- message dropped",
                     (unsigned)(CHIRP_SIG_SIZE + length));
            return CHIRP_SKIPPED;
        }
        /* Sign the tag line followed by the payload. */
        char prefix[TAG_PREFIX_MAX];
        int  plen = snprintf(prefix, sizeof(prefix), "tag=%s\n", tag);
        if (plen < 0 || (size_t)plen >= sizeof(prefix)) {
            ESP_LOGW(TAG, "tag %s does not fit the signed prefix -- message "
                          "dropped", tag);
            free(envelope);
            return CHIRP_SKIPPED;
        }
        if (chirp_sign_raw(s_config.key, sizeof(s_config.key),
                           (const uint8_t *)prefix, (size_t)plen,
                           bytes, length, envelope) != ESP_OK) {
            free(envelope);
            return CHIRP_SKIPPED;
        }
        memcpy(envelope + CHIRP_SIG_SIZE, bytes, length);
        body     = envelope;
        body_len = CHIRP_SIG_SIZE + length;
    }

    int64_t t0 = now_ms();
    /* Publish at QoS 0, without a broker delivery acknowledgement. */
    int msg = esp_mqtt_client_publish(s_client, topic, (const char *)body,
                                      (int)body_len, 0, 0);
    int64_t took = now_ms() - t0;

    free(envelope);  /* NULL when no signed copy was needed */

    if (msg < 0) {
        ESP_LOGW(TAG, "publish failed (%d)", msg);
        return CHIRP_CONNECTION_FAILED;
    }
    ESP_LOGI(TAG, "%u bytes %s -> PUBLISH %s %u bytes in %" PRId64 " ms "
                  "(tag=%s)",
             (unsigned)length, s_config.has_key ? "signed" : "plain", topic,
             (unsigned)body_len, took, tag);
    return CHIRP_SENT;
}

/* ---------------------------------------------------------------------------
 * the loop
 * ------------------------------------------------------------------------ */

static void send_task(void *arg)
{
    uint32_t    backoff = BACKOFF_MIN_MS;
    bool        started = false;
    const char *tag     = CHIRP_TAG_HEARTBEAT;

    for (;;) {
        if (!wifi_ladder_wait_connected(portMAX_DELAY)) {
            continue;
        }

        if (!(xEventGroupGetBits(s_bits) & BIT_CONNECTED)) {
            /* Log connection time and available internal RAM and PSRAM. */
            int64_t t0     = now_ms();
            size_t  dram0  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            size_t  psram0 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

            if (!started) {
                if (esp_mqtt_client_start(s_client) != ESP_OK) {
                    ESP_LOGE(TAG, "client would not start");
                    vTaskDelete(NULL);
                }
                started = true;
            } else {
                /* Auto-reconnect is off, so the backoff lives here. */
                esp_mqtt_client_reconnect(s_client);
            }

            EventBits_t b = xEventGroupWaitBits(s_bits, BIT_CONNECTED, pdFALSE,
                                                pdFALSE,
                                                pdMS_TO_TICKS(CONNECT_WAIT_MS));
            if (!(b & BIT_CONNECTED)) {
                ESP_LOGW(TAG, "no session after %d ms, retrying in %u ms",
                         CONNECT_WAIT_MS, (unsigned)backoff);
                vTaskDelay(pdMS_TO_TICKS(backoff));
                backoff = backoff >= BACKOFF_MAX_MS / 2 ? BACKOFF_MAX_MS
                                                        : backoff * 2;
                continue;
            }
            backoff = BACKOFF_MIN_MS;
            /* Tag the first report after connection as heartbeat. */
            tag     = CHIRP_TAG_HEARTBEAT;
            ESP_LOGI(TAG, "mqtts session up in %" PRId64 " ms -- internal heap "
                          "%u -> %u, psram %u -> %u",
                     now_ms() - t0, (unsigned)dram0,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)psram0,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }

        /* A failed capture costs a log line; a failed publish drops the
         * session and BIT_LOST reconnects. */
        if (s_report(tag) == CHIRP_CONNECTION_FAILED) {
            ESP_LOGW(TAG, "dropping the session");
            xEventGroupClearBits(s_bits, BIT_CONNECTED);
            xEventGroupSetBits(s_bits, BIT_LOST);
        }

        /* Wait for a command or the next scheduled report. */
        EventBits_t b = xEventGroupWaitBits(s_bits, BIT_SNAP | BIT_LOST, pdTRUE,
                                            pdFALSE,
                                            pdMS_TO_TICKS(REPORT_PERIOD_MS));
        if (b & BIT_LOST) {
            continue;  /* reconnect at the top rather than sleep out the gap */
        }
        /* Use the command's response tag, or heartbeat after a timeout. */
        tag = (b & BIT_SNAP) && s_requested_tag != NULL ? s_requested_tag
                                                        : CHIRP_TAG_HEARTBEAT;
    }
}

esp_err_t chirp_mqtt_start(const chirp_config_t *config,
                           chirp_report_fn report,
                           chirp_command_fn on_command)
{
    if (config == NULL || report == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config     = *config;
    s_report     = report;
    s_on_command = on_command;

    const char *hwid = chirp_store_hwid(&s_config);
    snprintf(s_topic_up, sizeof(s_topic_up), CHIRP_TOPIC_UP, hwid);
    snprintf(s_topic_down, sizeof(s_topic_down), CHIRP_TOPIC_DOWN, hwid);

    /* Use MQTTS on port 8883. A stored host changes only the hostname. */
    char uri[URI_MAX];
    snprintf(uri, sizeof(uri), "mqtts://%s:%u", s_config.host,
             (unsigned)CHIRP_DEFAULT_PORT);

    s_bits = xEventGroupCreate();
    if (s_bits == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_mqtt_client_config_t mqtt_config = {
        .broker = {
            .address.uri = uri,
            /* Verify the broker certificate with the ESP-IDF root bundle. */
            .verification.crt_bundle_attach = esp_crt_bundle_attach,
        },
        .credentials = {
            .client_id = hwid,
            .username  = hwid,
            /* Use a NULL password when no credential is stored.
             * Tokens and claim codes share the MQTT password field. */
            .authentication.password = s_config.cred_kind == CHIRP_CRED_NONE
                                           ? NULL
                                           : s_config.cred,
        },
        .session = {
            .keepalive             = KEEPALIVE_S,
            .disable_clean_session = true,
        },
        .network = {
            .disable_auto_reconnect = true,
            .timeout_ms             = 15000,
        },
        .buffer = {
            .size     = MQTT_IN_BUFFER,
            .out_size = MQTT_OUT_BUFFER,
        },
        .outbox = { .limit = MQTT_OUTBOX_LIMIT },
    };

    s_client = esp_mqtt_client_init(&mqtt_config);
    if (s_client == NULL) {
        return ESP_FAIL;
    }
    esp_err_t err = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                                   mqtt_event, NULL);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "%s, up=%s down=%s, %s", uri, s_topic_up, s_topic_down,
             s_config.cred_kind == CHIRP_CRED_NONE ? "no password flag (L0)"
             : s_config.has_key ? "password + 32-byte envelope (L2)"
                                : "password (L1)");

    /* The TLS handshake runs on the MQTT client's task; the frame stays in PSRAM. */
    if (xTaskCreate(send_task, "chirp_mqtt", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
