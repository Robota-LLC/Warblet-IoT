#include <string.h>
#include <stdio.h>

#include "chirp_store.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "chirp_store";

#define NVS_NAMESPACE "chirp"

#define KEY_APS       "aps"
#define KEY_AP_COUNT  "ap_count"
#define KEY_HOST      "host"
#define KEY_HWID      "hwid"
#define KEY_CRED      "cred"
#define KEY_CRED_KIND "cred_kind"
#define KEY_SIGNKEY   "signkey"
#define KEY_LEVEL     "level"

esp_err_t chirp_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

const char *chirp_store_default_hwid(void)
{
    static char hwid[CHIRP_HWID_SIZE];

    if (hwid[0] == '\0') {
        uint8_t mac[6] = {0};
        esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_read_mac: %s", esp_err_to_name(err));
        }
        snprintf(hwid, sizeof(hwid), "cam-%02x%02x%02x", mac[3], mac[4], mac[5]);
    }
    return hwid;
}

const char *chirp_store_hwid(const chirp_config_t *config)
{
    return (config != NULL && config->hwid[0] != '\0') ? config->hwid : chirp_store_default_hwid();
}

/* nvs_get_str for an optional key: a missing key leaves *dst untouched. */
static esp_err_t load_str(nvs_handle_t h, const char *key, char *dst, size_t size)
{
    size_t len = size;
    esp_err_t err = nvs_get_str(h, key, dst, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    return err;
}

esp_err_t chirp_store_load(chirp_config_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    strlcpy(out->host, CHIRP_DEFAULT_HOST, sizeof(out->host));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;  /* never provisioned */
    }
    if (err != ESP_OK) {
        return err;
    }

    uint8_t count = 0;
    err = nvs_get_u8(h, KEY_AP_COUNT, &count);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        count = 0;
        err = ESP_OK;
    }
    if (err == ESP_OK && count > 0) {
        if (count > CHIRP_MAX_APS) {
            count = CHIRP_MAX_APS;
        }
        size_t blob_len = (size_t)count * sizeof(chirp_ap_t);
        err = nvs_get_blob(h, KEY_APS, out->aps, &blob_len);
        if (err == ESP_OK && blob_len == (size_t)count * sizeof(chirp_ap_t)) {
            out->ap_count = count;
        } else if (err == ESP_OK) {
            ESP_LOGW(TAG, "stored ap list truncated, ignoring");
        }
    }

    if (err == ESP_OK) {
        err = load_str(h, KEY_HOST, out->host, sizeof(out->host));
    }
    if (err == ESP_OK) {
        err = load_str(h, KEY_HWID, out->hwid, sizeof(out->hwid));
    }
    if (err == ESP_OK) {
        err = load_str(h, KEY_CRED, out->cred, sizeof(out->cred));
    }
    if (err == ESP_OK) {
        uint8_t kind = CHIRP_CRED_NONE;
        esp_err_t kerr = nvs_get_u8(h, KEY_CRED_KIND, &kind);
        if (kerr == ESP_OK) {
            out->cred_kind = (chirp_cred_kind_t)kind;
        } else if (kerr != ESP_ERR_NVS_NOT_FOUND) {
            err = kerr;
        }
    }
    if (err == ESP_OK) {
        size_t key_len = sizeof(out->key);
        esp_err_t kerr = nvs_get_blob(h, KEY_SIGNKEY, out->key, &key_len);
        if (kerr == ESP_OK && key_len == sizeof(out->key)) {
            out->has_key = true;
        } else if (kerr != ESP_ERR_NVS_NOT_FOUND && kerr != ESP_ERR_NVS_INVALID_LENGTH) {
            err = kerr;
        }
    }
    if (err == ESP_OK) {
        int32_t level = 0;
        esp_err_t lerr = nvs_get_i32(h, KEY_LEVEL, &level);
        if (lerr == ESP_OK) {
            out->level = level;
            out->has_level = true;
        } else if (lerr != ESP_ERR_NVS_NOT_FOUND) {
            err = lerr;
        }
    }

    nvs_close(h);
    return err;
}

/* Writes a string key, erasing it when the value is empty. */
static esp_err_t save_str(nvs_handle_t h, const char *key, const char *value)
{
    if (value == NULL || value[0] == '\0') {
        esp_err_t err = nvs_erase_key(h, key);
        return (err == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : err;
    }
    return nvs_set_str(h, key, value);
}

esp_err_t chirp_store_save(const chirp_config_t *config)
{
    if (config == NULL || config->ap_count > CHIRP_MAX_APS) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(h, KEY_AP_COUNT, config->ap_count);
    if (err == ESP_OK) {
        /* The push carries the whole network list, so the blob is rewritten whole. */
        err = nvs_set_blob(h, KEY_APS, config->aps, (size_t)config->ap_count * sizeof(chirp_ap_t));
    }
    if (err == ESP_OK) {
        err = save_str(h, KEY_HOST, config->host);
    }
    if (err == ESP_OK) {
        err = save_str(h, KEY_HWID, config->hwid);
    }
    if (err == ESP_OK) {
        err = save_str(h, KEY_CRED, config->cred);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(h, KEY_CRED_KIND, (uint8_t)config->cred_kind);
    }
    if (err == ESP_OK) {
        if (config->has_key) {
            err = nvs_set_blob(h, KEY_SIGNKEY, config->key, sizeof(config->key));
        } else {
            esp_err_t eerr = nvs_erase_key(h, KEY_SIGNKEY);
            err = (eerr == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : eerr;
        }
    }
    if (err == ESP_OK) {
        if (config->has_level) {
            err = nvs_set_i32(h, KEY_LEVEL, config->level);
        } else {
            esp_err_t eerr = nvs_erase_key(h, KEY_LEVEL);
            err = (eerr == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : eerr;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }

    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    }
    return err;
}
