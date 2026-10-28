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
#define KEY_IDENT     "ident"
/* The separate entries an older image keeps its settings in. This store reads
 * them only while it has no record of its own, and deletes them once its
 * record is saved, the credential first. */
#define KEY_CRED      "cred"
#define KEY_SIGNKEY   "signkey"
#define KEY_CRED_KIND "cred_kind"
#define KEY_HWID      "hwid"
#define KEY_HOST      "host"
#define KEY_AP_COUNT  "ap_count"

/* Who the board is, how it signs and where it reports, saved as ONE value:
 * a power cut leaves the old set or the new one, never one device's id with
 * another's key or server. */
#define IDENT_VERSION 1
typedef struct {
    uint8_t version;              /* IDENT_VERSION */
    uint8_t cred_kind;
    uint8_t has_key;
    char    hwid[CHIRP_HWID_SIZE];
    char    cred[CHIRP_CRED_SIZE];
    uint8_t key[CHIRP_KEY_SIZE];
    char    host[CHIRP_HOST_SIZE];
} ident_t;

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
        snprintf(hwid, sizeof(hwid), CHIRP_HWID_PREFIX "%02x%02x%02x", mac[3], mac[4], mac[5]);
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

static void set_defaults(chirp_config_t *out)
{
    memset(out, 0, sizeof(*out));
    strlcpy(out->host, CHIRP_DEFAULT_HOST, sizeof(out->host));
}

static esp_err_t load_all(nvs_handle_t h, chirp_config_t *out)
{
    /* The network list is one value, and its length says how many networks
     * it holds, so no power cut can leave a count that does not match it. */
    size_t blob_len = sizeof(out->aps);
    esp_err_t err = nvs_get_blob(h, KEY_APS, out->aps, &blob_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        blob_len = 0;
        err = ESP_OK;
    }
    if (err == ESP_OK && blob_len % sizeof(chirp_ap_t) != 0) {
        err = ESP_ERR_NVS_INVALID_LENGTH;
    }
    if (err == ESP_OK) {
        out->ap_count = (uint8_t)(blob_len / sizeof(chirp_ap_t));
    }
    if (err != ESP_OK) {
        return err;
    }

    /* The record, when there is one. Anything but this version and size is
     * unreadable, and unreadable settings count as never set up. */
    ident_t id;
    size_t id_len = sizeof(id);
    err = nvs_get_blob(h, KEY_IDENT, &id, &id_len);
    if (err == ESP_OK) {
        if (id_len != sizeof(id) || id.version != IDENT_VERSION) {
            return ESP_ERR_NVS_INVALID_LENGTH;
        }
        strlcpy(out->hwid, id.hwid, sizeof(out->hwid));
        strlcpy(out->cred, id.cred, sizeof(out->cred));
        out->cred_kind = (chirp_cred_kind_t)id.cred_kind;
        out->has_key   = id.has_key != 0;
        if (out->has_key) {
            memcpy(out->key, id.key, sizeof(out->key));
        }
        if (id.host[0] != '\0') {
            strlcpy(out->host, id.host, sizeof(out->host));
        }
        return ESP_OK;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    /* None yet: the separate entries an older image saved. */
    err = load_str(h, KEY_HOST, out->host, sizeof(out->host));
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
    return err;
}

esp_err_t chirp_store_load(chirp_config_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    set_defaults(out);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;  /* never provisioned */
    }
    if (err == ESP_OK) {
        err = load_all(h, out);
        nvs_close(h);
    }

    /* Settings that cannot be read count as never set up, so the board
     * still starts and USB setup can write them again. */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "stored settings unreadable (%s), starting as never set up",
                 esp_err_to_name(err));
        set_defaults(out);
    }
    return ESP_OK;
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

    /* The id, the credential, the key and the server go in one write, so
     * they change together or not at all. */
    ident_t id = { .version = IDENT_VERSION, .cred_kind = (uint8_t)config->cred_kind,
                   .has_key = config->has_key ? 1 : 0 };
    strlcpy(id.hwid, config->hwid, sizeof(id.hwid));
    strlcpy(id.cred, config->cred, sizeof(id.cred));
    if (config->has_key) {
        memcpy(id.key, config->key, sizeof(id.key));
    }
    strlcpy(id.host, config->host, sizeof(id.host));
    err = nvs_set_blob(h, KEY_IDENT, &id, sizeof(id));

    /* Only once that is saved: an older image's entries go, the credential
     * first, so that image finds its old settings whole or no credential.
     * Its network count goes before the list below changes, so it never
     * disagrees with the list. */
    static const char *const older[] = {
        KEY_CRED, KEY_SIGNKEY, KEY_CRED_KIND, KEY_HWID, KEY_HOST, KEY_AP_COUNT,
    };
    for (size_t i = 0; err == ESP_OK && i < sizeof(older) / sizeof(older[0]); i++) {
        esp_err_t eerr = nvs_erase_key(h, older[i]);
        err = (eerr == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : eerr;
    }

    /* The push carries the whole network list, so the blob is rewritten whole. */
    if (err == ESP_OK) {
        err = nvs_set_blob(h, KEY_APS, config->aps, (size_t)config->ap_count * sizeof(chirp_ap_t));
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
