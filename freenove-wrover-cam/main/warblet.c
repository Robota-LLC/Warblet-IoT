/* Warblet's helper for ESP-IDF boards (warblet.h says what it is for). The
 * same file in every ESP-IDF demo. Four parts, in order: the stored settings,
 * USB setup, Wi-Fi, and signing. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "warblet.h"

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "warblet";

static warblet_demo_t s_demo;

esp_err_t warblet_start(const warblet_demo_t *demo)
{
    if (demo == NULL || demo->slug == NULL || demo->hwid_prefix == NULL ||
        demo->default_host == NULL || demo->transports == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_demo = *demo;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

/* ============================================================================
 * the stored settings
 * ========================================================================= */

#define NVS_NAMESPACE "chirp"

/* Everything, as ONE value: a save is one NVS write, so the board holds the
 * old settings or the new ones, never one device's id with another's key, and
 * never one network list with another device's server. */
#define KEY_SETTINGS "settings"
#define RECORD_VERSION 1
typedef struct {
    uint8_t    version;        /* RECORD_VERSION */
    uint8_t    ap_count;
    uint8_t    cred_kind;
    uint8_t    has_key;
    char       ssid[CHIRP_SSID_SIZE];
    char       pass[CHIRP_PASS_SIZE];
    chirp_ap_t aps[CHIRP_MAX_APS];
    char       hwid[CHIRP_HWID_SIZE];
    char       cred[CHIRP_CRED_SIZE];
    uint8_t    key[CHIRP_KEY_SIZE];
    char       host[CHIRP_HOST_SIZE];
} record_t;

const char *chirp_store_default_hwid(void)
{
    static char hwid[CHIRP_HWID_SIZE];

    if (hwid[0] == '\0') {
        uint8_t mac[6] = {0};
        esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_read_mac: %s", esp_err_to_name(err));
        }
        snprintf(hwid, sizeof(hwid), "%s%02x%02x%02x", s_demo.hwid_prefix, mac[3], mac[4], mac[5]);
    }
    return hwid;
}

const char *chirp_store_hwid(const chirp_config_t *config)
{
    return (config != NULL && config->hwid[0] != '\0') ? config->hwid : chirp_store_default_hwid();
}

const char *chirp_store_host(const chirp_config_t *config)
{
    return (config != NULL && config->host[0] != '\0') ? config->host : s_demo.default_host;
}

bool chirp_config_network(const chirp_config_t *config, size_t i, const char **ssid, const char **pass)
{
    size_t seen = 0;
    if (config->ssid[0] != '\0') {
        if (i == 0) {
            *ssid = config->ssid;
            *pass = config->pass;
            return true;
        }
        seen = 1;
    }
    /* Each (name, password) pair once: one name with two passwords is two
     * networks to try (CHIRP-PROV v1.8). */
    for (size_t a = 0; a < config->ap_count; a++) {
        bool repeat = config->ssid[0] != '\0' && strcmp(config->aps[a].ssid, config->ssid) == 0 &&
                      strcmp(config->aps[a].pass, config->pass) == 0;
        for (size_t b = 0; b < a && !repeat; b++) {
            repeat = strcmp(config->aps[b].ssid, config->aps[a].ssid) == 0 &&
                     strcmp(config->aps[b].pass, config->aps[a].pass) == 0;
        }
        if (repeat) {
            continue;
        }
        if (seen == i) {
            *ssid = config->aps[a].ssid;
            *pass = config->aps[a].pass;
            return true;
        }
        seen++;
    }
    return false;
}

static esp_err_t load_record(nvs_handle_t h, chirp_config_t *out)
{
    record_t rec;
    size_t   len = sizeof(rec);
    esp_err_t err = nvs_get_blob(h, KEY_SETTINGS, &rec, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;  /* never set up: the record is the only place settings live */
    }
    if (err != ESP_OK) {
        return err;
    }
    /* Anything but this version and size is unreadable, and unreadable
     * settings count as never set up. */
    if (len != sizeof(rec) || rec.version != RECORD_VERSION || rec.ap_count > CHIRP_MAX_APS) {
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    strlcpy(out->ssid, rec.ssid, sizeof(out->ssid));
    strlcpy(out->pass, rec.pass, sizeof(out->pass));
    out->ap_count = rec.ap_count;
    for (size_t i = 0; i < rec.ap_count; i++) {
        strlcpy(out->aps[i].ssid, rec.aps[i].ssid, sizeof(out->aps[i].ssid));
        strlcpy(out->aps[i].pass, rec.aps[i].pass, sizeof(out->aps[i].pass));
    }
    strlcpy(out->hwid, rec.hwid, sizeof(out->hwid));
    strlcpy(out->cred, rec.cred, sizeof(out->cred));
    strlcpy(out->host, rec.host, sizeof(out->host));
    out->cred_kind = (chirp_cred_kind_t)rec.cred_kind;
    out->has_key   = rec.has_key != 0;
    if (out->has_key) {
        memcpy(out->key, rec.key, sizeof(out->key));
    }
    memset(&rec, 0, sizeof(rec));
    return ESP_OK;
}

esp_err_t chirp_store_load(chirp_config_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;  /* never set up */
    }
    if (err == ESP_OK) {
        err = load_record(h, out);
        nvs_close(h);
    }
    /* Settings that cannot be read count as never set up, so the board
     * still starts and USB setup can write them again. */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "stored settings unreadable (%s), starting as never set up", esp_err_to_name(err));
        memset(out, 0, sizeof(*out));
    }
    return ESP_OK;
}

esp_err_t chirp_store_save(const chirp_config_t *config)
{
    if (config == NULL || config->ap_count > CHIRP_MAX_APS) {
        return ESP_ERR_INVALID_ARG;
    }
    record_t rec = { .version = RECORD_VERSION, .ap_count = config->ap_count,
                     .cred_kind = (uint8_t)config->cred_kind, .has_key = config->has_key ? 1 : 0 };
    strlcpy(rec.ssid, config->ssid, sizeof(rec.ssid));
    strlcpy(rec.pass, config->pass, sizeof(rec.pass));
    for (size_t i = 0; i < config->ap_count; i++) {
        strlcpy(rec.aps[i].ssid, config->aps[i].ssid, sizeof(rec.aps[i].ssid));
        strlcpy(rec.aps[i].pass, config->aps[i].pass, sizeof(rec.aps[i].pass));
    }
    strlcpy(rec.hwid, config->hwid, sizeof(rec.hwid));
    strlcpy(rec.cred, config->cred, sizeof(rec.cred));
    strlcpy(rec.host, config->host, sizeof(rec.host));
    if (config->has_key) {
        memcpy(rec.key, config->key, sizeof(rec.key));
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, KEY_SETTINGS, &rec, sizeof(rec));
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    memset(&rec, 0, sizeof(rec));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    }
    return err;
}

/* ============================================================================
 * USB setup (CHIRP-PROV v1.8)
 * ========================================================================= */

/* A line whose bytes stop for this long is given up: longer than the 1 s a
 * browser tab out of view waits between the pieces of a push. */
#define LINE_IDLE_MS 1500
/* Before "CHIRP" nothing counts, so only enough to find it is kept. */
#define JUNK_KEPT 64
/* A turn of the reader takes at most this much off the console, then lets
 * every other task run for 10 ms, so a flood of bytes cannot hold the
 * processor. A host keeping the contract's pace (64 bytes, then 30 ms) sends
 * far less than this in a turn. */
#define READ_BATCH 1024

static chirp_config_t        s_config;  /* the stored settings a push merges onto */
static chirp_console_read_fn s_read;    /* main.c's: what has come in, never a wait */

static void emit(const char *line)
{
    printf("%s\n", line);
    fflush(stdout);
}

/* ---- the push --------------------------------------------------------------- */

static const char *const ACCEPTED[] = {"ssid", "pass", "aps", "claim", "token", "hwid", "host", "key"};

static bool accepted(const char *name)
{
    for (size_t i = 0; i < sizeof(ACCEPTED) / sizeof(ACCEPTED[0]); i++) {
        if (strcmp(name, ACCEPTED[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* A field name a refusal can carry: 1 to 32 of A-Z a-z 0-9 _ . - */
static bool nameable(const char *name)
{
    size_t n = strlen(name);
    if (n == 0 || n > 32) {
        return false;
    }
    for (const char *c = name; *c != '\0'; c++) {
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
              *c == '_' || *c == '.' || *c == '-')) {
            return false;
        }
    }
    return true;
}

/* A text value into a fixed field: a string that fits, with no character
 * below U+0020 and no U+007F. That holds for a network's name and password
 * too (CHIRP-PROV v1.8): a WPA2 passphrase is printable ASCII, and these
 * values reach request headers, the console and the record. */
static bool take_text(const cJSON *item, char *dst, size_t size)
{
    if (!cJSON_IsString(item) || item->valuestring == NULL || strlen(item->valuestring) >= size) {
        return false;
    }
    for (const unsigned char *c = (const unsigned char *)item->valuestring; *c != '\0'; c++) {
        if (*c < 0x20 || *c == 0x7f) {
            return false;
        }
    }
    strlcpy(dst, item->valuestring, size);
    return true;
}

/* True when the JSON text holds a NUL as an escape (\u0000). A backslash
 * escapes the one character after it, so "\\u0000", an escaped backslash
 * and then u0000, is text, not a NUL. */
static bool escaped_nul(const char *s)
{
    for (; *s != '\0'; s++) {
        if (*s != '\\') {
            continue;
        }
        if (*++s == '\0') {
            return false;
        }
        if (*s == 'u' && strncmp(s + 1, "0000", 4) == 0) {
            return true;
        }
    }
    return false;
}

/* The key format, as written: exactly 44 characters, 43 of the standard
 * base64 alphabet and one '='. Checked before decoding, since the decoder
 * skips spaces and line breaks. */
static bool key_text(const char *s)
{
    if (strlen(s) != 44 || s[43] != '=') {
        return false;
    }
    for (size_t i = 0; i < 43; i++) {
        char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' ||
              c == '/')) {
            return false;
        }
    }
    return true;
}

/* A setup code or token: printable ASCII, so as many bytes as characters. */
static bool printable_ascii(const char *s)
{
    for (; *s != '\0'; s++) {
        if (*s < 0x21 || *s > 0x7e) {  /* no space either (CHIRP-PROV v1.8) */
            return false;
        }
    }
    return true;
}

/* The platform's id grammar: letters, digits and : - _ . only, so an id
 * cannot put a space or a line break into the announce, a topic or a URL. */
static bool hwid_chars(const char *s)
{
    for (; *s != '\0'; s++) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
              *s == ':' || *s == '-' || *s == '_' || *s == '.')) {
            return false;
        }
    }
    return true;
}

/* A host: letters, digits, dots and dashes, so it comes back out of the URL
 * or topic the transport builds as the same host. */
static bool host_chars(const char *s)
{
    for (; *s != '\0'; s++) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
              *s == '.' || *s == '-')) {
            return false;
        }
    }
    return true;
}

/* aps: the whole list, 1 to 8 entries, each exactly an ssid (not empty) and a pass. */
static bool take_networks(const cJSON *list, chirp_config_t *out)
{
    int n = cJSON_IsArray(list) ? cJSON_GetArraySize(list) : 0;
    if (n < 1 || n > CHIRP_MAX_APS) {
        return false;
    }
    out->ap_count = 0;
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, list) {
        if (!cJSON_IsObject(entry) || cJSON_GetArraySize(entry) != 2) {
            return false;
        }
        chirp_ap_t *ap = &out->aps[out->ap_count];
        if (!take_text(cJSON_GetObjectItemCaseSensitive(entry, "ssid"), ap->ssid, sizeof(ap->ssid)) ||
            ap->ssid[0] == '\0' ||
            !take_text(cJSON_GetObjectItemCaseSensitive(entry, "pass"), ap->pass, sizeof(ap->pass))) {
            return false;
        }
        out->ap_count++;
    }
    return true;
}

/* Checks a push and merges it onto *stored into *out. NULL when the merged
 * settings are complete; otherwise the refusal's word, and for "unsupported"
 * the field's name in field[]. A push is applied whole or refused whole. */
static const char *merge_push(const char *json, const chirp_config_t *stored, chirp_config_t *out,
                              char field[33])
{
    field[0] = '\0';
    /* A NUL anywhere, here sent as \u0000, is bad-json: the checks below read
     * C strings, which would end there (a raw one never reaches this far). */
    if (escaped_nul(json)) {
        return "bad-json";
    }
    cJSON *root = cJSON_ParseWithOpts(json, NULL, true);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return "bad-json";
    }

    /* A field this board does not act on is refused by name, the first by
     * byte order; one that cannot be named is bad-json. */
    const char *first = NULL;
    bool unnameable = false, claim = false, token = false;
    for (const cJSON *it = root->child; it != NULL; it = it->next) {
        claim = claim || strcmp(it->string, "claim") == 0;
        token = token || strcmp(it->string, "token") == 0;
        if (accepted(it->string)) {
            continue;
        }
        if (!nameable(it->string)) {
            unnameable = true;
        } else if (first == NULL || strcmp(it->string, first) < 0) {
            first = it->string;
        }
    }
    if (unnameable || (first == NULL && claim && token)) {  /* one credential slot */
        cJSON_Delete(root);
        return "bad-json";
    }
    if (first != NULL) {
        strlcpy(field, first, 33);
        cJSON_Delete(root);
        return "unsupported";
    }

    *out = *stored;
    bool pushed_ssid = false, pushed_pass = false, pushed_aps = false, pushed_key = false, good = true;
    for (const cJSON *it = root->child; it != NULL && good; it = it->next) {
        const char *k = it->string;
        if (strcmp(k, "ssid") == 0) {
            good = take_text(it, out->ssid, sizeof(out->ssid)) && out->ssid[0] != '\0';
            pushed_ssid = true;
        } else if (strcmp(k, "pass") == 0) {
            good = take_text(it, out->pass, sizeof(out->pass));
            pushed_pass = true;
        } else if (strcmp(k, "aps") == 0) {
            good = take_networks(it, out);
            pushed_aps = true;
        } else if (strcmp(k, "claim") == 0 || strcmp(k, "token") == 0) {
            /* A setup code is kept where the token goes: once redeemed it IS the token. */
            good = take_text(it, out->cred, sizeof(out->cred)) && out->cred[0] != '\0' &&
                   printable_ascii(out->cred);
            out->cred_kind = k[0] == 'c' ? CHIRP_CRED_CLAIM : CHIRP_CRED_TOKEN;
        } else if (strcmp(k, "hwid") == 0) {
            good = take_text(it, out->hwid, sizeof(out->hwid)) && hwid_chars(out->hwid);
        } else if (strcmp(k, "host") == 0) {
            good = take_text(it, out->host, sizeof(out->host)) && host_chars(out->host);
        } else if (strcmp(k, "key") == 0) {
            /* A key that will sign, never "". */
            size_t len = 0;
            good = cJSON_IsString(it) && it->valuestring != NULL && key_text(it->valuestring) &&
                   mbedtls_base64_decode(out->key, sizeof(out->key), &len,
                                         (const unsigned char *)it->valuestring,
                                         strlen(it->valuestring)) == 0 &&
                   len == sizeof(out->key);
            out->has_key = good;
            pushed_key = true;
        }
    }
    cJSON_Delete(root);
    if (!good) {
        return "bad-json";
    }

    if (pushed_aps) {
        /* aps is the whole list, its first entry tried first. A top-level ssid
         * or pass sent with it must be that entry's. */
        if ((pushed_ssid && strcmp(out->ssid, out->aps[0].ssid) != 0) ||
            (pushed_pass && strcmp(out->pass, out->aps[0].pass) != 0)) {
            return "bad-json";
        }
        strlcpy(out->ssid, out->aps[0].ssid, sizeof(out->ssid));
        strlcpy(out->pass, out->aps[0].pass, sizeof(out->pass));
    } else if (pushed_ssid) {
        out->ap_count = 0;  /* a top-level ssid without aps names exactly one network */
    } else if (pushed_pass && out->ap_count > 0) {
        /* A pass alone is the password of the network tried first, the list's
         * first entry: it changes there too, and the list keeps the rest. */
        strlcpy(out->aps[0].pass, out->pass, sizeof(out->aps[0].pass));
    }

    /* A setup code, or an id other than the stored one, starts a new device:
     * the old device's signing key goes, unless this push brings one. Ids are
     * compared byte for byte, as Warblet tells devices apart. */
    if ((claim || strcmp(out->hwid, stored->hwid) != 0) && !pushed_key) {
        memset(out->key, 0, sizeof(out->key));
        out->has_key = false;
    }

    /* The merged result has to be complete: a network, a credential and an id. */
    if (out->ssid[0] == '\0' || out->cred[0] == '\0' || out->hwid[0] == '\0') {
        return "missing-field";
    }
    return NULL;
}

static void handle_push(const char *json)
{
    chirp_config_t merged;
    char field[33];
    const char *refused = merge_push(json, &s_config, &merged, field);
    if (refused != NULL) {
        char line[64];
        snprintf(line, sizeof(line), field[0] ? "CHIRP= err %s %s" : "CHIRP= err %s", refused, field);
        ESP_LOGW(TAG, "push refused: %s", line + 7);
        emit(line);
        return;
    }
    if (chirp_store_save(&merged) != ESP_OK) {
        emit("CHIRP= err store-failed");
        return;
    }
    ESP_LOGI(TAG, "set up: hwid=%s host=%s signing=%s", chirp_store_hwid(&merged),
             chirp_store_host(&merged), merged.has_key ? "yes" : "no");
    emit("CHIRP= ok");
    vTaskDelay(pdMS_TO_TICKS(200));  /* let the answer reach the host */
    esp_restart();
}

/* ---- the announce ------------------------------------------------------------ */

static void announce(void)
{
    /* At most 512 bytes (CHIRP-PROV v1.8). Static: it is too big for a task's stack. */
    static char line[513];
    int n = snprintf(line, sizeof(line),
                     "CHIRP! v=1 hw=%s fw=%s key_set=%d radios=wifi t=%s caps={\"v\":1,\"auth\":[1,2],"
                     "\"keyProvisioning\":[\"usb\"],\"otaKeyRotation\":false,\"otaFirmwareUpdate\":false,"
                     "\"transports\":[%s]}",
                     chirp_store_hwid(&s_config), esp_app_get_description()->version,
                     s_config.has_key ? 1 : 0, s_demo.slug, s_demo.transports);
    if (n < 0 || (size_t)n >= sizeof(line)) {
        ESP_LOGE(TAG, "the announce does not fit 512 bytes; not cut short, not sent");
        return;
    }
    emit(line);
}

/* ---- the line ----------------------------------------------------------------- */

static char    s_line[CHIRP_LINE_MAX + 1];
static size_t  s_len;
static bool    s_marked;   /* "CHIRP" has come: s_line starts there */
static bool    s_skip;     /* the rest of a line that was too long: dropped to its end */
static bool    s_nul;      /* a raw NUL came in this line: it is refused at its end */
static int64_t s_quiet_ms = -1;  /* since when every look at the console found nothing; -1 once one found a byte */

static void handle_line(char *line)
{
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t')) {
        line[--n] = '\0';
    }
    if (strcmp(line, "CHIRP?") == 0) {
        announce();
    } else if (strncmp(line, "CHIRP+", 6) == 0) {
        const char *body = line + 6;
        while (*body == ' ' || *body == '\t') {
            body++;
        }
        handle_push(body);
    }
    /* Other lines that start CHIRP (an echo of our own) are not ours to answer. */
}

/* One byte off the console. A line counts from its first "CHIRP": stray bytes
 * in front are dropped and do not spoil it. A line longer than CHIRP_LINE_MAX
 * from there is thrown away whole, through its line ending, so no part of it
 * is read as a command. A line that stops coming is given up by quiet(). */
static void feed(char c)
{
    if (c == '\r' || c == '\n') {  /* the portal sends CRLF; a bare LF is fine too */
        if (s_marked && !s_skip) {
            s_line[s_len] = '\0';
            if (!s_nul) {
                handle_line(s_line);
            } else if (strncmp(s_line, "CHIRP+", 6) == 0) {
                emit("CHIRP= err bad-json");  /* a raw NUL: no part of it is read */
            }
        }
        s_len = 0;
        s_marked = s_skip = s_nul = false;
        return;
    }
    if (c == '\0') {
        s_nul = s_nul || s_marked;  /* a C string would end here: the line goes whole */
        return;
    }
    if (s_skip) {
        return;
    }
    if (s_len >= CHIRP_LINE_MAX) {
        s_len = 0;
        s_marked = false;
        s_skip = true;
        return;
    }
    if (!s_marked && s_len >= JUNK_KEPT) {
        memmove(s_line, s_line + s_len - 4, 4);  /* keep enough to find a split "CHIRP" */
        s_len = 4;
    }
    s_line[s_len++] = c;
    if (!s_marked && c == 'P' && s_len >= 5 && memcmp(s_line + s_len - 5, "CHIRP", 5) == 0) {
        memcpy(s_line, "CHIRP", 5);  /* the line starts here */
        s_len = 5;
        s_marked = true;
    }
}

/* A look at the console that found nothing, at now_ms. A setup line is given
 * up, over the limit or not (CHIRP-PROV v1.8), once the console has been seen
 * quiet for LINE_IDLE_MS: looks that found nothing, that far apart, so a probe
 * sent after a stalled line is answered. Bytes that came while other tasks
 * kept the reader off would still be waiting, so quiet seen at both ends means
 * none came between. Bytes found waiting join the line that was arriving:
 * nothing shows they paused, so a good push is never thrown away. */
static void quiet(int64_t now_ms)
{
    if (s_quiet_ms < 0) {
        s_quiet_ms = now_ms;
    } else if ((s_marked || s_skip) && now_ms - s_quiet_ms >= LINE_IDLE_MS) {
        s_len = 0;  /* the line stopped coming: it will not end now */
        s_marked = s_skip = s_nul = false;
    }
}

/* One turn of the reader: the bytes that have come in, up to READ_BATCH. The
 * read never waits, so a read that returns nothing is a look that found the
 * console quiet, which is how quiet() sees a line stop. The look's time is
 * read before it: other tasks can hold the reader off after it, and a byte
 * that came then is not one the look saw come. */
static void prov_step(void)
{
    uint8_t buf[64];
    for (size_t taken = 0; taken < READ_BATCH;) {
        size_t room = READ_BATCH - taken;
        int64_t looked_ms = esp_timer_get_time() / 1000;
        int n = s_read(buf, room < sizeof(buf) ? room : sizeof(buf));
        if (n <= 0) {
            quiet(looked_ms);  /* nothing more had come when it looked */
            return;
        }
        s_quiet_ms = -1;
        for (int i = 0; i < n; i++) {
            feed((char)buf[i]);
        }
        taken += (size_t)n;
    }
}

static void prov_task(void *arg)
{
    ESP_LOGI(TAG, "listening for CHIRP-PROV v1.8 on the console");
    for (;;) {
        prov_step();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t chirp_prov_start(const chirp_config_t *config, chirp_console_read_fn read_console)
{
    if (config == NULL || read_console == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;
    s_read   = read_console;
    if (xTaskCreate(prov_task, "warblet_setup", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* ============================================================================
 * Wi-Fi: the stored networks, tried in a scan's order
 * ========================================================================= */

#define BIT_CONNECTED    BIT0  /* station has an IP address */
#define BIT_DISCONNECTED BIT1  /* attempt failed, or the link dropped */

#define CONNECT_TIMEOUT_MS 12000
#define LADDER_REST_MS     15000
#define CANDIDATES_MAX     (CHIRP_MAX_APS + 1)

static chirp_config_t     s_wifi;
static EventGroupHandle_t s_events;

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *ev = data;
        ESP_LOGW(TAG, "disconnected from %.*s (reason %d)", ev->ssid_len, (const char *)ev->ssid, ev->reason);
        xEventGroupClearBits(s_events, BIT_CONNECTED);
        xEventGroupSetBits(s_events, BIT_DISCONNECTED);
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = data;
        ESP_LOGI(TAG, "connected, ip " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupClearBits(s_events, BIT_DISCONNECTED);
        xEventGroupSetBits(s_events, BIT_CONNECTED);
    }
}

/* Orders the stored networks: the ones this scan saw first, strongest RSSI
 * first, then the rest in the order they were set up. */
static size_t order_candidates(const char **ssid, const char **pass)
{
    size_t n = 0;
    while (n < CANDIDATES_MAX && chirp_config_network(&s_wifi, n, &ssid[n], &pass[n])) {
        n++;
    }
    bool   seen[CANDIDATES_MAX] = {0};
    int8_t rssi[CANDIDATES_MAX] = {0};

    esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed (%s), using stored order", esp_err_to_name(err));
    } else {
        uint16_t found = 0;
        esp_wifi_scan_get_ap_num(&found);
        wifi_ap_record_t *records = (found > 0) ? calloc(found, sizeof(wifi_ap_record_t)) : NULL;
        if (records != NULL) {
            err = esp_wifi_scan_get_ap_records(&found, records);
            for (uint16_t r = 0; err == ESP_OK && r < found; r++) {
                for (size_t i = 0; i < n; i++) {
                    if (strncmp(ssid[i], (const char *)records[r].ssid, sizeof(records[r].ssid)) != 0) {
                        continue;
                    }
                    if (!seen[i] || records[r].rssi > rssi[i]) {
                        seen[i] = true;
                        rssi[i] = records[r].rssi;
                    }
                }
            }
            free(records);
        } else {
            esp_wifi_clear_ap_list();  /* records were never fetched */
        }
    }

    /* A stable insertion sort: a network the scan saw moves ahead of every
     * unseen one and every weaker one. Ties and unseen networks keep their
     * set-up order. */
    for (size_t i = 1; i < n; i++) {
        const char *s = ssid[i], *p = pass[i];
        bool   sn = seen[i];
        int8_t r = rssi[i];
        size_t k = i;
        while (k > 0 && sn && (!seen[k - 1] || r > rssi[k - 1])) {
            ssid[k] = ssid[k - 1];
            pass[k] = pass[k - 1];
            seen[k] = seen[k - 1];
            rssi[k] = rssi[k - 1];
            k--;
        }
        ssid[k] = s;
        pass[k] = p;
        seen[k] = sn;
        rssi[k] = r;
    }
    return n;
}

static bool try_connect(const char *ssid, const char *pass)
{
    wifi_config_t wc = {0};
    /* sta.ssid/password are fixed-size fields, not necessarily NUL terminated. */
    memcpy(wc.sta.ssid, ssid, strnlen(ssid, sizeof(wc.sta.ssid)));
    memcpy(wc.sta.password, pass, strnlen(pass, sizeof(wc.sta.password)));
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;  /* open networks must stay usable */

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_config: %s", esp_err_to_name(err));
        return false;
    }

    xEventGroupClearBits(s_events, BIT_CONNECTED | BIT_DISCONNECTED);
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "connect to \"%s\": %s", ssid, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "trying \"%s\"", ssid);
    EventBits_t bits = xEventGroupWaitBits(s_events, BIT_CONNECTED | BIT_DISCONNECTED,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));
    if (bits & BIT_CONNECTED) {
        return true;
    }

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));  /* let the trailing disconnect event land */
    return false;
}

static void ladder_task(void *arg)
{
    const char *ssid[CANDIDATES_MAX], *pass[CANDIDATES_MAX];

    for (;;) {
        size_t n = order_candidates(ssid, pass);
        bool connected = false;
        for (size_t i = 0; i < n && !connected; i++) {
            connected = try_connect(ssid[i], pass[i]);
        }
        if (!connected) {
            ESP_LOGW(TAG, "all %u network(s) failed, resting %d s", (unsigned)n, LADDER_REST_MS / 1000);
            vTaskDelay(pdMS_TO_TICKS(LADDER_REST_MS));
            continue;
        }
        /* Stay put until the link drops, then run the whole ladder again. */
        xEventGroupWaitBits(s_events, BIT_DISCONNECTED, pdFALSE, pdFALSE, portMAX_DELAY);
        ESP_LOGW(TAG, "link lost, rerunning the ladder");
    }
}

esp_err_t wifi_ladder_start(const chirp_config_t *config)
{
    if (config == NULL || config->ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    s_wifi = *config;

    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        on_ip_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    /* Band mode is left at auto: a dual-band chip such as the C5 can use 5 GHz
     * networks, and a 2.4 GHz-only one has nothing to set. */
    ESP_ERROR_CHECK(esp_wifi_start());

    if (xTaskCreate(ladder_task, "wifi_ladder", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool wifi_ladder_wait_connected(TickType_t ticks_to_wait)
{
    EventBits_t bits = xEventGroupWaitBits(s_events, BIT_CONNECTED, pdFALSE, pdFALSE, ticks_to_wait);
    return (bits & BIT_CONNECTED) != 0;
}

/* ============================================================================
 * signing
 * ========================================================================= */

int chirp_sign_prefix(char *out, size_t outsz, const char *nonce, const char *tag)
{
    /* signed input = <nonce> "\n" "tag=" <tag> "\n" <body>; a missing field is empty. */
    int n = snprintf(out, outsz, "%s\ntag=%s\n", nonce != NULL ? nonce : "", tag != NULL ? tag : "");
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
    /* Hash a then b without joining them in memory. */
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

esp_err_t chirp_sign_hex2(const uint8_t *key, size_t key_len,
                          const uint8_t *a, size_t a_len,
                          const uint8_t *b, size_t b_len,
                          char out[CHIRP_SIG_HEX_SIZE])
{
    uint8_t mac[CHIRP_SIG_SIZE];
    esp_err_t err = chirp_sign_raw(key, key_len, a, a_len, b, b_len, mac);
    if (err != ESP_OK) {
        return err;
    }
    for (size_t i = 0; i < sizeof(mac); i++) {
        snprintf(out + i * 2, 3, "%02x", mac[i]);
    }
    return ESP_OK;
}

/* Times below this are an unset clock. */
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
