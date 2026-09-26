#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chirp_prov.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"

static const char *TAG = "chirp_prov";

/* Protocol caps the line at 4096 bytes; +1 for the NUL fgets() appends. */
#define CHIRP_LINE_MAX 4097

#define PROBE_PREFIX  "CHIRP?"
#define PUSH_PREFIX   "CHIRP+ "

static chirp_config_t s_config;

static void emit(const char *line)
{
    printf("%s\n", line);
    fflush(stdout);
}

/* A present key must be a string that fits; a wrong type is bad-json by contract. */
static bool string_ok(const cJSON *obj, const char *key, size_t size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return item == NULL ||
           (cJSON_IsString(item) && item->valuestring != NULL &&
            strlen(item->valuestring) < size);
}

/* Every field rule, asked of the push alone before anything is merged, so a
 * push is applied whole or refused whole. */
static const char *validate_push(const cJSON *root)
{
    if (cJSON_GetObjectItemCaseSensitive(root, "claim") != NULL &&
        cJSON_GetObjectItemCaseSensitive(root, "token") != NULL) {
        return "bad-json";  /* one credential slot, and no silent choice */
    }
    if (!string_ok(root, "ssid", CHIRP_SSID_SIZE) || !string_ok(root, "pass", CHIRP_PASS_SIZE) ||
        !string_ok(root, "host", CHIRP_HOST_SIZE) || !string_ok(root, "hwid", CHIRP_HWID_SIZE) ||
        !string_ok(root, "claim", CHIRP_CRED_SIZE) || !string_ok(root, "token", CHIRP_CRED_SIZE)) {
        return "bad-json";
    }

    const cJSON *key = cJSON_GetObjectItemCaseSensitive(root, "key");
    if (key != NULL) {
        uint8_t raw[CHIRP_KEY_SIZE];
        size_t  len = 0;
        if (!cJSON_IsString(key) || key->valuestring == NULL ||
            (key->valuestring[0] != '\0' &&
             (mbedtls_base64_decode(raw, sizeof(raw), &len,
                                    (const unsigned char *)key->valuestring,
                                    strlen(key->valuestring)) != 0 ||
              len != sizeof(raw)))) {
            return "bad-json";  /* a key that could not sign must not report ok */
        }
    }

    const cJSON *level = cJSON_GetObjectItemCaseSensitive(root, "level");
    if (level != NULL &&
        (!cJSON_IsNumber(level) || level->valuedouble < 0 || level->valuedouble > 2)) {
        return "bad-json";
    }

    const cJSON *aps = cJSON_GetObjectItemCaseSensitive(root, "aps");
    if (aps == NULL) {
        return NULL;
    }
    if (!cJSON_IsArray(aps) || cJSON_GetArraySize(aps) > CHIRP_MAX_APS) {
        return "bad-json";
    }
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, aps) {
        if (!cJSON_IsObject(entry)) {
            return "bad-json";
        }
        const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(entry, "ssid");
        if (!cJSON_IsString(ssid) || ssid->valuestring == NULL || ssid->valuestring[0] == '\0' ||
            !string_ok(entry, "ssid", CHIRP_SSID_SIZE) || !string_ok(entry, "pass", CHIRP_PASS_SIZE)) {
            return "bad-json";
        }
    }
    return NULL;
}

/* Copies a validated string; false when the key is absent or empty. */
static bool take_string(const cJSON *obj, const char *key, char *dst, size_t size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL || item->valuestring[0] == '\0') {
        return false;
    }
    strlcpy(dst, item->valuestring, size);
    return true;
}

static void append_ap(chirp_config_t *config, const cJSON *entry)
{
    chirp_ap_t ap = {0};
    if (take_string(entry, "ssid", ap.ssid, sizeof(ap.ssid))) {
        take_string(entry, "pass", ap.pass, sizeof(ap.pass));  /* an open network has none */
        config->aps[config->ap_count++] = ap;
    }
}

/* Merge a validated push onto the saved config. */
static const char *parse_push(const char *json, chirp_config_t *config)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return "bad-json";
    }
    const char *reason = validate_push(root);
    if (reason != NULL) {
        cJSON_Delete(root);
        return reason;
    }

    *config = s_config;

    const cJSON *aps  = cJSON_GetObjectItemCaseSensitive(root, "aps");
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    if (aps != NULL || ssid != NULL) {
        /* Replace the network list with aps, or with the single supplied ssid. */
        memset(config->aps, 0, sizeof(config->aps));
        config->ap_count = 0;

        const cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, aps) {
            append_ap(config, entry);
        }
        if (config->ap_count == 0) {
            append_ap(config, root);  /* top-level ssid/pass = one-entry list */
        }
    }

    take_string(root, "host", config->host, sizeof(config->host));
    take_string(root, "hwid", config->hwid, sizeof(config->hwid));
    /* Keep the saved credential unless the push supplies a claim or token. */
    if (take_string(root, "claim", config->cred, sizeof(config->cred))) {
        config->cred_kind = CHIRP_CRED_CLAIM;
    } else if (take_string(root, "token", config->cred, sizeof(config->cred))) {
        config->cred_kind = CHIRP_CRED_TOKEN;
    }

    const cJSON *key = cJSON_GetObjectItemCaseSensitive(root, "key");
    if (cJSON_IsString(key) && key->valuestring[0] != '\0') {
        size_t len = 0;
        mbedtls_base64_decode(config->key, sizeof(config->key), &len,
                              (const unsigned char *)key->valuestring,
                              strlen(key->valuestring));  /* checked in validate_push */
        config->has_key = true;
    }

    const cJSON *level = cJSON_GetObjectItemCaseSensitive(root, "level");
    if (level != NULL) {
        config->level = (int32_t)level->valuedouble;  /* stored, never acted on */
        config->has_level = true;
    }

    /* Every other key in the object is ignored by design. */
    cJSON_Delete(root);

    /* The MERGED result has to be complete. */
    if (config->ap_count == 0 || config->cred_kind == CHIRP_CRED_NONE) {
        return "missing-field";
    }
    return NULL;
}

static void handle_push(const char *json)
{
    chirp_config_t config;
    const char *reason = parse_push(json, &config);
    if (reason != NULL) {
        ESP_LOGW(TAG, "push rejected: %s", reason);
        char line[48];
        snprintf(line, sizeof(line), "CHIRP= err %s", reason);
        emit(line);
        return;
    }

    if (chirp_store_save(&config) != ESP_OK) {
        emit("CHIRP= err store-failed");
        return;
    }

    ESP_LOGI(TAG, "provisioned: %u network(s), host=%s, hwid=%s, signing=%s",
             config.ap_count, config.host, chirp_store_hwid(&config), config.has_key ? "yes" : "no");
    emit("CHIRP= ok");
    vTaskDelay(pdMS_TO_TICKS(200));  /* let the reply reach the host */
    esp_restart();
}

static void handle_line(char *line)
{
    /* Non-CHIRP lines, including our own log output, are ignored. */
    if (strncmp(line, PROBE_PREFIX, strlen(PROBE_PREFIX)) == 0) {
        /* Announce Wi-Fi and signing support, plus this demo's slug. */
        char reply[128];
        snprintf(reply, sizeof(reply), "CHIRP! v=1 hw=%s radios=wifi sig=1 t=demo-firebeetle-c5",
                 chirp_store_hwid(&s_config));
        emit(reply);
    } else if (strncmp(line, PUSH_PREFIX, strlen(PUSH_PREFIX)) == 0) {
        handle_push(line + strlen(PUSH_PREFIX));
    }
}

static void prov_task(void *arg)
{
    char *line = malloc(CHIRP_LINE_MAX);
    if (line == NULL) {
        ESP_LOGE(TAG, "out of memory");
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "listening for CHIRP-PROV v1.6 on the console");
    for (;;) {
        if (fgets(line, CHIRP_LINE_MAX, stdin) == NULL) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';  /* tolerate CRLF on receive */
        }
        handle_line(line);
    }
}

esp_err_t chirp_prov_start(const chirp_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;

    if (xTaskCreate(prov_task, "chirp_prov", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
