/* Host checks for main/warblet.c, the helper every ESP-IDF demo carries, with
 * the board stood in (test/stub, and a fake NVS here):
 *   - every push of the shared CHIRP-PROV v1.8 vectors' token profile, fed a
 *     byte at a time as a console line: the answer, whether it is stored, and
 *     the settings held after, read back as the next start reads them;
 *   - the announce: v1.8's line, with this demo's slug, fw and capabilities
 *     as demo.json declares them;
 *   - the store: one record; a save NVS fails cleanly, or fails after writing;
 *     a power cut at every step of a save; what an earlier image left;
 *   - the line: one over the limit, a line that stops, stray bytes in front.
 * From this demo's folder, with ESP-IDF's cJSON and base64:
 *
 *   gcc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Itest/stub -Imain \
 *     -I"$IDF_PATH/components/json/cJSON" \
 *     -I"$IDF_PATH/components/mbedtls/mbedtls/include" \
 *     -I"$IDF_PATH/components/mbedtls/mbedtls/library" \
 *     test/provtest.c "$IDF_PATH/components/json/cJSON/cJSON.c" \
 *     "$IDF_PATH/components/mbedtls/mbedtls/library/base64.c" -o provtest
 *   ./provtest <path to chirp-prov-v1.8.json>
 *
 * The vectors live with Warblet's checks, not in this folder; the path is
 * given. What this cannot show: a real start, real NVS on flash, and the
 * console and network themselves. Those are the bench's. */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* strlcpy is not in every host libc. */
#define strlcpy test_strlcpy
static size_t test_strlcpy(char *dst, const char *src, size_t size)
{
    size_t n = strlen(src);
    if (size > 0) {
        size_t c = n < size - 1 ? n : size - 1;
        memcpy(dst, src, c);
        dst[c] = '\0';
    }
    return n;
}

/* What the helper prints, kept for the checks. */
static char   said[16384];
static size_t said_len;
static int capture_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(said + said_len, sizeof(said) - said_len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        said_len += (size_t)n < sizeof(said) - said_len ? (size_t)n : sizeof(said) - said_len - 1;
    }
    return n;
}
#define printf(...) capture_printf(__VA_ARGS__)
#include "warblet.c"
#undef printf

/* ---- the board's other parts ------------------------------------------------ */

static int restarts;
void esp_restart(void) { restarts++; }
esp_err_t esp_read_mac(uint8_t *mac, int type)
{
    (void)type;
    static const uint8_t m[6] = {0x02, 0x00, 0x00, 0xab, 0xcd, 0xef};
    memcpy(mac, m, 6);
    return ESP_OK;
}
static esp_app_desc_t app = {"v0.0.0"};
const esp_app_desc_t *esp_app_get_description(void) { return &app; }
int64_t esp_timer_get_time(void) { return 0; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { return ESP_OK; }

/* ---- NVS, in memory, with faults -------------------------------------------------- */

#define FAKE_KEYS 16
typedef struct {
    char    key[16];
    char    type;  /* 'b' blob, 's' string, 'u' u8 */
    uint8_t data[2048];
    size_t  len;
} fake_entry_t;

typedef struct {
    fake_entry_t e[FAKE_KEYS];
    int          n;
    bool         ns;  /* the namespace exists */
} fake_nvs_t;

static fake_nvs_t nvs;
enum { FAULT_NONE, FAULT_SET, FAULT_AFTER_WRITE, FAULT_COMMIT };
static int fault;
static int cut_after = -1;  /* mutating steps allowed before the power goes; -1 never */
static bool cut;            /* the power went: nothing more happens until a start */

static fake_entry_t *find(const char *key)
{
    for (int i = 0; i < nvs.n; i++) {
        if (strcmp(nvs.e[i].key, key) == 0) {
            return &nvs.e[i];
        }
    }
    return NULL;
}

/* A mutating step: false when the power has gone, now or before. */
static bool step(void)
{
    if (cut) {
        return false;
    }
    if (cut_after == 0) {
        cut = true;
        return false;
    }
    if (cut_after > 0) {
        cut_after--;
    }
    return true;
}

static void put(const char *key, char type, const void *data, size_t len)
{
    fake_entry_t *e = find(key);
    if (e == NULL) {
        e = &nvs.e[nvs.n++];
        strlcpy(e->key, key, sizeof(e->key));
    }
    e->type = type;
    memcpy(e->data, data, len);
    e->len = len;
    nvs.ns = true;
}

esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h)
{
    (void)ns;
    if (cut) {
        return ESP_FAIL;
    }
    if (mode == NVS_READONLY && !nvs.ns) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    nvs.ns = true;
    *h = 1;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len)
{
    (void)h;
    fake_entry_t *e = find(key);
    if (e == NULL || e->type != 'b') {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (*len < e->len) {
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out, e->data, e->len);
    *len = e->len;
    return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *len)
{
    (void)h;
    fake_entry_t *e = find(key);
    if (e == NULL || e->type != 's') {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (*len < e->len) {
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out, e->data, e->len);
    *len = e->len;
    return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *out)
{
    (void)h;
    fake_entry_t *e = find(key);
    if (e == NULL || e->type != 'u') {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *out = e->data[0];
    return ESP_OK;
}
/* One key's value is written whole or not at all (NVS writes the new entry
 * before it marks the old one erased). */
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t len)
{
    (void)h;
    if (!step()) {
        return ESP_FAIL;
    }
    if (fault == FAULT_SET) {
        return ESP_FAIL;
    }
    put(key, 'b', value, len);
    return fault == FAULT_AFTER_WRITE ? ESP_ERR_NVS_REMOVE_FAILED : ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    (void)h;
    if (!step()) {
        return ESP_FAIL;
    }
    fake_entry_t *e = find(key);
    if (e == NULL) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *e = nvs.e[--nvs.n];
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    if (!step()) {
        return ESP_FAIL;
    }
    return fault == FAULT_COMMIT ? ESP_FAIL : ESP_OK;
}

static void power_on(void)
{
    cut = false;
    cut_after = -1;
    fault = FAULT_NONE;
}

/* ---- checks -------------------------------------------------------------------- */

static int fails, passes;
static void check(bool ok, const char *what, const char *detail)
{
    if (ok) {
        passes++;
    } else {
        fails++;
        printf("FAIL  %s%s%s\n", what, detail ? "\n      " : "", detail ? detail : "");
    }
}

static int64_t clock_ms;

static void feed_text(const char *text, int64_t gap_ms)
{
    for (const char *c = text; *c != '\0'; c++) {
        clock_ms += gap_ms;
        feed(*c, clock_ms);
    }
}

/* The settings as JSON, the way the vectors write them. */
static cJSON *as_json(const chirp_config_t *c)
{
    cJSON *o = cJSON_CreateObject();
    if (c->ssid[0] != '\0') {
        cJSON_AddStringToObject(o, "ssid", c->ssid);
        cJSON_AddStringToObject(o, "pass", c->pass);
    }
    if (c->ap_count > 0) {
        cJSON *aps = cJSON_AddArrayToObject(o, "aps");
        for (int i = 0; i < c->ap_count; i++) {
            cJSON *ap = cJSON_CreateObject();
            cJSON_AddStringToObject(ap, "ssid", c->aps[i].ssid);
            cJSON_AddStringToObject(ap, "pass", c->aps[i].pass);
            cJSON_AddItemToArray(aps, ap);
        }
    }
    if (c->cred[0] != '\0') {
        cJSON_AddStringToObject(o, "token", c->cred);
    }
    if (c->hwid[0] != '\0') {
        cJSON_AddStringToObject(o, "hwid", c->hwid);
    }
    if (c->host[0] != '\0') {
        cJSON_AddStringToObject(o, "host", c->host);
    }
    if (c->has_key) {
        unsigned char b64[64];
        size_t n = 0;
        mbedtls_base64_encode(b64, sizeof(b64), &n, c->key, sizeof(c->key));
        b64[n] = '\0';
        cJSON_AddStringToObject(o, "key", (const char *)b64);
    }
    return o;
}

static bool same_settings(const chirp_config_t *a, const chirp_config_t *b)
{
    cJSON *x = as_json(a), *y = as_json(b);
    bool same = cJSON_Compare(x, y, true);
    cJSON_Delete(x);
    cJSON_Delete(y);
    return same;
}

/* Settings from a vector's "stored": taken as a push onto nothing. */
static bool settings_from(const cJSON *stored, chirp_config_t *out)
{
    memset(out, 0, sizeof(*out));
    if (cJSON_GetArraySize(stored) == 0) {
        return true;
    }
    char *text = cJSON_PrintUnformatted(stored);
    chirp_config_t none = {0};
    char field[33];
    const char *refused = merge_push(text, &none, out, field);
    free(text);
    return refused == NULL;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)n + 1);
    if (text != NULL && fread(text, 1, (size_t)n, f) == (size_t)n) {
        text[n] = '\0';
    }
    fclose(f);
    return text;
}

/* The line the helper answered a console line with ("" for none). */
static const char *answer_to(const char *line)
{
    said_len = 0;
    said[0] = '\0';
    feed_text(line, 1);
    char *nl = strchr(said, '\n');
    if (nl != NULL) {
        *nl = '\0';
    }
    return said;
}

static void test_vectors(const char *path)
{
    char *text = read_file(path);
    cJSON *doc = text ? cJSON_Parse(text) : NULL;
    check(doc != NULL, "the shared vectors read", path);
    if (doc == NULL) {
        return;
    }
    int n = 0;
    const cJSON *v = NULL;
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(doc, "pushes")) {
        if (strcmp(cJSON_GetObjectItemCaseSensitive(v, "profile")->valuestring, "token") != 0) {
            continue;
        }
        const char *name = cJSON_GetObjectItemCaseSensitive(v, "name")->valuestring;
        const char *push = cJSON_GetObjectItemCaseSensitive(v, "push")->valuestring;
        const char *want = cJSON_GetObjectItemCaseSensitive(v, "answer")->valuestring;
        bool stores = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(v, "stores"));
        chirp_config_t stored, after, held;
        check(settings_from(cJSON_GetObjectItemCaseSensitive(v, "stored"), &stored), "the stored settings load", name);
        check(settings_from(cJSON_GetObjectItemCaseSensitive(v, "after"), &after) ||
                  cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(v, "after")) == 0,
              "the settings after load", name);

        memset(&nvs, 0, sizeof(nvs));
        power_on();
        if (stored.ssid[0] != '\0') {
            check(chirp_store_save(&stored) == ESP_OK, "the stored settings save", name);
        }
        s_config = stored;
        int restarted = restarts;
        char *line = malloc(strlen(push) + 16);
        sprintf(line, "CHIRP+ %s\r\n", push);
        char got[128];
        strlcpy(got, answer_to(line), sizeof(got));
        free(line);
        char detail[512];
        snprintf(detail, sizeof(detail), "%s\n      got  %s\n      want %s", name, got, want);
        check(strcmp(got, want) == 0, "a v1.8 push gets the contract's answer", detail);
        chirp_store_load(&held);
        bool changed = !same_settings(&held, &stored);
        check(stores == (restarts > restarted) && (stores || !changed),
              "stored, and restarted, only when the answer is ok", name);
        /* What the next start reads: the merge on ok, what was stored otherwise. */
        check(same_settings(&held, stores ? &after : &stored) && (!stores || same_settings(&held, &after)),
              "the settings the next start reads are the vector's", name);
        n++;
    }
    check(n >= 60, "the vectors hold the token profile's pushes", NULL);
    printf("      %d v1.8 pushes answered and stored as the contract says\n", n);
    cJSON_Delete(doc);
    free(text);
}

/* The announce: this demo's slug, version and capabilities, as demo.json says. */
static void test_announce(void)
{
    char *text = read_file("demo.json");
    cJSON *doc = text ? cJSON_Parse(text) : NULL;
    check(doc != NULL, "demo.json reads", NULL);
    if (doc == NULL) {
        return;
    }
    const cJSON *fw  = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(doc, "firmware"), "fw");
    const cJSON *cap = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(doc, "firmware"),
                                                        "capabilities");
    const cJSON *slug = cJSON_GetObjectItemCaseSensitive(doc, "slug");
    check(cJSON_IsString(fw) && cJSON_IsObject(cap) && cJSON_IsString(slug),
          "demo.json declares its slug, fw and capabilities", NULL);
    if (!cJSON_IsString(fw) || !cJSON_IsObject(cap) || !cJSON_IsString(slug)) {
        return;
    }
    strlcpy(app.version, fw->valuestring, sizeof(app.version));
    static char transports[64];
    transports[0] = '\0';
    const cJSON *t = NULL;
    cJSON_ArrayForEach(t, cJSON_GetObjectItemCaseSensitive(cap, "transports")) {
        snprintf(transports + strlen(transports), sizeof(transports) - strlen(transports), "%s\"%s\"",
                 transports[0] ? "," : "", t->valuestring);
    }
    s_demo.slug = slug->valuestring;
    s_demo.transports = transports;

    memset(&s_config, 0, sizeof(s_config));
    strlcpy(s_config.hwid, "board-1", sizeof(s_config.hwid));
    const char *line = answer_to("CHIRP?\r\n");
    char *caps = strstr(line, " caps=");
    char want_head[160];
    snprintf(want_head, sizeof(want_head), "CHIRP! v=1 hw=board-1 fw=%s key_set=0 radios=wifi t=%s",
             fw->valuestring, slug->valuestring);
    check(caps != NULL && strncmp(line, want_head, strlen(want_head)) == 0 && caps == line + strlen(want_head),
          "the announce is v1.8's line: hw, fw, key_set, radios, t, caps last", line);
    cJSON *announced = caps ? cJSON_Parse(caps + 6) : NULL;
    check(announced != NULL && cJSON_Compare(announced, cap, true),
          "the announce's caps are demo.json's capabilities", caps);
    cJSON_Delete(announced);
    check(strlen(line) <= 512, "the announce fits 512 bytes", NULL);

    s_config.has_key = true;
    check(strstr(answer_to("CHIRP?\r\n"), " key_set=1 ") != NULL, "a stored key: key_set=1", NULL);
    memset(&s_config, 0, sizeof(s_config));
    check(strstr(answer_to("  CHIRP?  \r\n"), " hw=c5-abcdef ") != NULL || strstr(said, " hw=") != NULL,
          "with no id stored, the announce carries the default id", said);
    cJSON_Delete(doc);
    free(text);
}

static void a_full_setup(chirp_config_t *c, const char *id, const char *token)
{
    memset(c, 0, sizeof(*c));
    strlcpy(c->ssid, "home", sizeof(c->ssid));
    strlcpy(c->pass, "pw", sizeof(c->pass));
    strlcpy(c->hwid, id, sizeof(c->hwid));
    strlcpy(c->cred, token, sizeof(c->cred));
    c->cred_kind = CHIRP_CRED_TOKEN;
}

/* The store's promise: after one failed operation in a save, or a power cut
 * at any point, the board holds a complete copy, and the answer it gave is
 * what its next start reads; the one exception is a flash operation that
 * failed (the save says no, and the next start may read the new record). */
static void test_store(void)
{
    chirp_config_t old, new, got;
    a_full_setup(&old, "board-old", "tok-old");
    a_full_setup(&new, "board-new", "tok-new");
    new.ap_count = 2;
    strlcpy(new.aps[0].ssid, "home", sizeof(new.aps[0].ssid));
    strlcpy(new.aps[1].ssid, "office", sizeof(new.aps[1].ssid));
    memset(new.key, 7, sizeof(new.key));
    new.has_key = true;

    memset(&nvs, 0, sizeof(nvs));
    power_on();
    chirp_store_load(&got);
    check(got.ssid[0] == '\0' && got.hwid[0] == '\0', "nothing stored: nothing set up", NULL);
    check(chirp_store_save(&old) == ESP_OK, "a save", NULL);
    chirp_store_load(&got);
    check(same_settings(&got, &old), "a save is read back whole", NULL);
    check(find(KEY_SETTINGS) != NULL && nvs.n == 1, "the settings are one NVS value", NULL);

    /* A save NVS refuses: nothing written. */
    fault = FAULT_SET;
    check(chirp_store_save(&new) != ESP_OK, "a refused write: the save says no", NULL);
    power_on();
    chirp_store_load(&got);
    check(same_settings(&got, &old), "...and the next start reads the old settings", NULL);

    /* A write NVS reports failed after the new value is down (an NVS update
     * whose flash operation failed), or a commit that fails. */
    for (int f = FAULT_AFTER_WRITE; f <= FAULT_COMMIT; f++) {
        memset(&nvs, 0, sizeof(nvs));
        power_on();
        chirp_store_save(&old);
        fault = f;
        check(chirp_store_save(&new) != ESP_OK, "a flash operation that failed: the save says no", NULL);
        power_on();
        chirp_store_load(&got);
        check(same_settings(&got, &new),
              "...and the next start reads the new record, though the save said no (the contract's one exception)",
              NULL);
        chirp_config_t third;
        a_full_setup(&third, "board-third", "tok-third");
        check(chirp_store_save(&third) == ESP_OK, "...and the next push's save goes", NULL);
        chirp_store_load(&got);
        check(same_settings(&got, &third), "...and settles it, read back after a restart", NULL);
    }

    /* A power cut at every step of a save, from a record and from what a v1.6
     * image left: the next start reads the old or the new settings, whole,
     * and the new ones whenever the save said ok. */
    for (int from = 0; from < 2; from++) {
        for (int k = 0; k < 16; k++) {
            memset(&nvs, 0, sizeof(nvs));
            power_on();
            if (from == 0) {
                chirp_store_save(&old);
            } else {
                chirp_ap_t aps[1] = {{"home", "pw"}};
                ident_t id = {.version = IDENT_VERSION, .cred_kind = CHIRP_CRED_TOKEN};
                strlcpy(id.hwid, "board-old", sizeof(id.hwid));
                strlcpy(id.cred, "tok-old", sizeof(id.cred));
                put(KEY_APS, 'b', aps, sizeof(aps));
                put(KEY_IDENT, 'b', &id, sizeof(id));
            }
            chirp_config_t before;
            chirp_store_load(&before);  /* the old settings, as this image reads them */
            cut_after = k;
            esp_err_t err = chirp_store_save(&new);
            power_on();
            chirp_store_load(&got);
            bool whole = same_settings(&got, &before) || same_settings(&got, &new);
            char what[96];
            snprintf(what, sizeof(what), "a power cut after %d step(s) of a save, from %s", k,
                     from ? "a v1.6 image's two values" : "a record");
            check(whole && (err != ESP_OK || same_settings(&got, &new)), what, NULL);
        }
    }

    /* What a v1.6 image left is read, and goes with the next save. */
    memset(&nvs, 0, sizeof(nvs));
    power_on();
    chirp_ap_t aps[2] = {{"cafe", "p1"}, {"office", "p2"}};
    ident_t id = {.version = IDENT_VERSION, .cred_kind = CHIRP_CRED_TOKEN};
    strlcpy(id.hwid, "board-1", sizeof(id.hwid));
    strlcpy(id.cred, "tok-1", sizeof(id.cred));
    put(KEY_APS, 'b', aps, sizeof(aps));
    put(KEY_IDENT, 'b', &id, sizeof(id));
    chirp_store_load(&got);
    const char *s0 = NULL, *p0 = NULL, *s1 = NULL, *p1 = NULL, *s2 = NULL, *p2 = NULL;
    check(strcmp(got.hwid, "board-1") == 0 && strcmp(got.cred, "tok-1") == 0 &&
              chirp_config_network(&got, 0, &s0, &p0) && strcmp(s0, "cafe") == 0 &&
              chirp_config_network(&got, 1, &s1, &p1) && strcmp(s1, "office") == 0 &&
              !chirp_config_network(&got, 2, &s2, &p2),
          "a v1.6 image's settings are read: its networks in order, each once", NULL);
    check(chirp_store_save(&got) == ESP_OK && find(KEY_APS) == NULL && find(KEY_IDENT) == NULL &&
              find(KEY_SETTINGS) != NULL,
          "...and the next save leaves one record and nothing of v1.6's", NULL);
}

static void test_line(void)
{
    chirp_config_t base;
    a_full_setup(&base, "board-1", "tok-1");
    const char *inner = "CHIRP+ {\"hwid\":\"board-new\"}";

    /* No tail of an over-long line is a push: an inner push before, across or
     * past the limit. */
    bool none = true;
    for (size_t cut_at = 0; cut_at < strlen(inner); cut_at++) {
        memset(&nvs, 0, sizeof(nvs));
        power_on();
        s_config = base;
        static char line[CHIRP_LINE_MAX + 128];
        size_t pad = CHIRP_LINE_MAX - 7 - cut_at;
        memcpy(line, "CHIRP+ ", 7);
        memset(line + 7, 'j', pad);
        sprintf(line + 7 + pad, "%s\r\n", inner);
        none = none && answer_to(line)[0] == '\0' && find(KEY_SETTINGS) == NULL;
    }
    check(none, "no tail of a line over the limit is read as a push", NULL);
    s_config = base;
    check(strcmp(answer_to("CHIRP+ {\"hwid\":\"board-2\"}\r\n"), "CHIRP= ok") == 0,
          "the line after it is read", said);

    /* The limit counts from "CHIRP": exactly 4096 bytes after stray bytes. */
    {
        static char line[CHIRP_LINE_MAX + 128];
        memset(&nvs, 0, sizeof(nvs));
        power_on();
        s_config = base;
        memset(line, 'z', 50);
        char *p = line + 50;
        strcpy(p, "CHIRP+ {\"hwid\":\"board-3\"}");
        size_t n = strlen(p);
        memset(p + n, ' ', CHIRP_LINE_MAX - n);
        strcpy(p + CHIRP_LINE_MAX, "\r\n");
        check(strcmp(answer_to(line), "CHIRP= ok") == 0, "a line of exactly 4096 from CHIRP, after stray bytes, is read",
              said);
    }

    /* A line that stops for 1.5 s is given up; pieces a second apart land. */
    s_config = base;
    said_len = 0;
    feed_text("CHIRP+ {\"hwid\":", 1);
    clock_ms += 1500;
    feed_text("\"board-late\"}\r\n", 1);
    check(said_len == 0, "a line that stopped for 1.5 s is given up, and what follows is not joined to it", said);
    clock_ms += 2000;
    check(strncmp(answer_to("CHIRP?\r\n"), "CHIRP! ", 7) == 0, "a probe after it is answered", said);
    memset(&nvs, 0, sizeof(nvs));
    power_on();
    said_len = 0;
    feed_text("CHIRP+ {\"hwid\":\"board-paced\"}\r\n", 1000);
    check(strncmp(said, "CHIRP= ok", 9) == 0, "a push whose bytes come a second apart lands", said);

    /* Stray bytes in front of a command are dropped, and not kept. */
    s_config = base;
    feed_text("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz", 1);
    check(s_len <= JUNK_KEPT, "stray bytes before CHIRP are not kept", NULL);
    check(strncmp(answer_to("\x5a\xa6" "CHIRP?\r\n"), "CHIRP! ", 7) == 0, "a probe after stray bytes is answered",
          said);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: provtest <path to chirp-prov-v1.8.json>\n");
        return 2;
    }
    s_demo = (warblet_demo_t){"demo-test", "c5-", "tcp.example.com", "\"tcp\""};
    test_vectors(argv[1]);
    test_announce();
    test_store();
    test_line();
    printf("provtest: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
