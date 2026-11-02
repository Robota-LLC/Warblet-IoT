/* Host checks for main/warblet.c, the helper every ESP-IDF demo carries, run
 * with this demo's own main.c and transport and the board stood in (test/stub,
 * and fakes here: NVS in memory, and a console wire with its own clock):
 *   - startup: app_main() as the board runs it. Never set up, it starts the
 *     setup reader and nothing else, and the reader answers a probe with v1.8's
 *     announce, made from the DEMO main.c hands warblet_start(), as demo.json
 *     declares it;
 *   - the reader, prov_task() itself: what comes in is taken in short reads
 *     that never wait; a line whose bytes stop for 1.5 s is given up, and a
 *     probe right after it is answered; pieces a second apart land; a flood is
 *     taken at most 1 KB a turn;
 *   - every push of the shared CHIRP-PROV v1.8 vectors' token profile, sent as
 *     the page sends it (64-byte pieces, 30 ms apart): the answer, whether it
 *     is stored, and the settings the next start reads;
 *   - a credential at its limit, 63 characters with quotation marks and
 *     backslashes: stored, read back by the next start, and sent as it is by
 *     the transport (the TCP hello, the HTTPS token header or the MQTT
 *     CONNECT's password, as each transport hands it to its library);
 *   - the store: one record; a save NVS fails cleanly, or fails after writing;
 *     a power cut at every step of a save; values another image left;
 *   - the line: one over the limit, stray bytes in front, a raw NUL.
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
 * given. What this cannot show: real NVS on flash, the console's driver and
 * its buffers, the network libraries past the calls the transport makes, and
 * the radio. Those are the bench's. */

#include <math.h>
#include <setjmp.h>
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

/* libm is not linked: the one call a main.c makes into it, done in place. */
#define lroundf(x) ((long)((x) < 0 ? (x) - 0.5f : (x) + 0.5f))

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

/* This demo's own parts: its transport and its main.c, as the board builds
 * them. The names they share with warblet.c are kept apart here. */
#define TAG         transport_tag
#define s_config    transport_config
#define handle_line transport_handle_line
#if __has_include("chirp_mqtt.h")
#include "chirp_mqtt.c"
#else
#include "chirp_send.c"
#endif
#undef handle_line
#undef s_config
#undef TAG
#if __has_include("camera_health.h")
#include "camera_health.c"
#endif
#define TAG main_tag
#include "main.c"
#undef TAG

/* Every console and network call any of the four demos makes is answered
 * below, so this test is the same file in each. */
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_http_client.h"
#include "lwip/netdb.h"
#include "mqtt_client.h"

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
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { return ESP_OK; }

/* A failed ESP_ERROR_CHECK panics the board: a start that would panic ends
 * here, named. */
static jmp_buf boot_stop;
static bool    in_boot;
void esp_error_check_failed(esp_err_t err, const char *expr)
{
    char detail[160];
    snprintf(detail, sizeof(detail), "ESP_ERROR_CHECK(%s) got 0x%x: the board panics and restarts", expr, err);
    check(false, "a start runs with no failed ESP_ERROR_CHECK", detail);
    if (in_boot) {
        longjmp(boot_stop, 1);
    }
    abort();
}

/* The tasks a start created, by name, in order. None of them runs. */
static char started[256];
static BaseType_t record_task(const char *name)
{
    size_t n = strlen(started);
    snprintf(started + n, sizeof(started) - n, "%s%s", n ? " " : "", name);
    return pdPASS;
}
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, int stack, void *arg, int prio, void *handle)
{
    return record_task(name);
}
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, int stack, void *arg, int prio,
                                   void *handle, int core)
{
    return record_task(name);
}

#if __has_include("chirp_led.h")
esp_err_t chirp_led_start(void) { return ESP_OK; }
bool chirp_led_command(const char *text) { return false; }
#endif
#if __has_include("camera.h")
static const uint8_t jpeg[] = {0xff, 0xd8, 0xff, 0xd9};
esp_err_t camera_init(void) { return ESP_OK; }
esp_err_t camera_capture(const uint8_t **out, size_t *out_len)
{
    *out = jpeg;
    *out_len = sizeof(jpeg);
    return ESP_OK;
}
void camera_release(void) {}
#endif

/* ---- the network, as far as the transport's own calls ------------------------- */

static int some_client;  /* what a client handle points at */

/* TCP: the bytes written to the socket. */
static char   sent[512];
static size_t sent_len;
int lwip_socket(int domain, int type, int protocol) { return 3; }
int lwip_connect(int s, const struct sockaddr *name, unsigned namelen) { return 0; }
int lwip_setsockopt(int s, int level, int name, const void *value, unsigned len) { return 0; }
int lwip_send(int s, const void *data, size_t size, int flags)
{
    if (size > sizeof(sent) - sent_len) {
        size = sizeof(sent) - sent_len;
    }
    memcpy(sent + sent_len, data, size);
    sent_len += size;
    return (int)size;
}
int lwip_recv(int s, void *mem, size_t len, int flags) { return 0; }
int lwip_select(int n, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout) { return 0; }
int lwip_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
    return -1;
}
void lwip_freeaddrinfo(struct addrinfo *ai) {}

/* HTTPS: the headers the last request was given. */
static char header_kept[8][2][128];
static int  header_count;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    header_count = 0;
    return (esp_http_client_handle_t)&some_client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value)
{
    if (header_count < 8) {
        strlcpy(header_kept[header_count][0], key, sizeof(header_kept[0][0]));
        strlcpy(header_kept[header_count][1], value, sizeof(header_kept[0][1]));
        header_count++;
    }
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_len) { return ESP_OK; }
int esp_http_client_write(esp_http_client_handle_t client, const char *buffer, int len) { return len; }
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client) { return 0; }
int esp_http_client_read_response(esp_http_client_handle_t client, char *buffer, int len) { return 0; }
int esp_http_client_get_status_code(esp_http_client_handle_t client) { return 202; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) { return ESP_OK; }

/* The value a header was given, or NULL (inline: only the HTTPS demo asks). */
static inline const char *header(const char *name)
{
    for (int i = 0; i < header_count; i++) {
        if (strcmp(header_kept[i][0], name) == 0) {
            return header_kept[i][1];
        }
    }
    return NULL;
}

/* MQTT: the credentials the client was set up with (esp-mqtt keeps copies). */
static char mqtt_user[64], mqtt_id[64], mqtt_password[128];
static int  mqtt_inits;
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config)
{
    mqtt_inits++;
    strlcpy(mqtt_user, config->credentials.username ? config->credentials.username : "", sizeof(mqtt_user));
    strlcpy(mqtt_id, config->credentials.client_id ? config->credentials.client_id : "", sizeof(mqtt_id));
    strlcpy(mqtt_password, config->credentials.authentication.password ? config->credentials.authentication.password : "",
            sizeof(mqtt_password));
    return (esp_mqtt_client_handle_t)&some_client;
}

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

/* ---- the console: a wire, a clock, and the reader on it ------------------------- */

static int64_t clock_ms;  /* the board's time */
int64_t esp_timer_get_time(void) { return clock_ms * 1000; }

/* What a host sends: each byte, with the time it arrives. */
#define WIRE_MAX 65536
static char    wire[WIRE_MAX];
static int64_t wire_at[WIRE_MAX];
static size_t  wire_len, wire_pos;
static int64_t wire_next;  /* when the next piece sent arrives */

/* Sends n bytes in pieces of `piece`, gap_ms apart, after what was sent
 * before. */
static void wire_send(const char *bytes, size_t n, size_t piece, int gap_ms)
{
    if (wire_pos == wire_len) {
        wire_pos = wire_len = 0;
    }
    int64_t at = wire_next > clock_ms ? wire_next : clock_ms;
    for (size_t i = 0; i < n && wire_len < WIRE_MAX; i++) {
        if (i > 0 && i % piece == 0) {
            at += gap_ms;
        }
        wire[wire_len] = bytes[i];
        wire_at[wire_len++] = at;
    }
    wire_next = at + gap_ms;
}

/* The host waits ms more before its next piece. */
static void wire_pause(int ms)
{
    wire_next = (wire_next > clock_ms ? wire_next : clock_ms) + ms;
}

/* One driver read hands back what has come, at most this much each time (a
 * ring buffer gives what it holds in one piece), so reads come back short and
 * uneven. */
static const size_t READ_PIECES[] = {64, 7, 33, 1, 64, 20, 5};
static size_t read_count;
static int    waits;  /* reads that were allowed to wait */
static size_t since_yield, most_between_yields;  /* bytes taken without another task running */
static const char *console_driver;  /* the driver main.c installed */
static jmp_buf reader_stop;
static bool    in_reader;

static int console_take(void *buf, uint32_t length, TickType_t ticks)
{
    if (ticks != 0) {
        /* A read allowed to wait sleeps until a byte comes; with none left to
         * come it would sleep for ever, and the run ends there. */
        waits++;
        if (wire_pos == wire_len) {
            if (in_reader) {
                longjmp(reader_stop, 1);
            }
            return 0;
        }
        if (wire_at[wire_pos] > clock_ms) {
            if (ticks != portMAX_DELAY && clock_ms + ticks < wire_at[wire_pos]) {
                clock_ms += ticks;
                since_yield = 0;
                return 0;
            }
            clock_ms = wire_at[wire_pos];
            since_yield = 0;
        }
    }
    size_t most = READ_PIECES[read_count++ % (sizeof(READ_PIECES) / sizeof(READ_PIECES[0]))];
    size_t n = 0;
    while (n < length && n < most && wire_pos < wire_len && wire_at[wire_pos] <= clock_ms) {
        ((char *)buf)[n++] = wire[wire_pos++];
    }
    since_yield += n;
    if (since_yield > most_between_yields) {
        most_between_yields = since_yield;
    }
    return (int)n;
}

esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *config)
{
    console_driver = "USB-Serial-JTAG";
    return ESP_OK;
}
int usb_serial_jtag_read_bytes(void *buf, uint32_t length, TickType_t ticks_to_wait)
{
    return console_take(buf, length, ticks_to_wait);
}
esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size, int tx_buffer_size, int queue_size,
                              void *uart_queue, int intr_alloc_flags)
{
    console_driver = "UART";
    return ESP_OK;
}
int uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length, TickType_t ticks_to_wait)
{
    return console_take(buf, length, ticks_to_wait);
}

/* A delay lets every other task run, and moves the clock on. In the reader,
 * once the host has sent everything and the line has been quiet for 3 s,
 * the run ends. */
void vTaskDelay(TickType_t ticks)
{
    clock_ms += ticks;
    since_yield = 0;
    if (in_reader && wire_pos == wire_len && clock_ms >= (wire_len ? wire_at[wire_len - 1] : 0) + 3000) {
        longjmp(reader_stop, 1);
    }
}

/* Runs the setup reader's task, prov_task() itself, on the wire until the host
 * has sent everything and the line has been quiet for 3 s. A start that did
 * not create the task leaves nothing to read the console. */
static void run_reader(void)
{
    if (strstr(started, "warblet_setup") == NULL) {
        wire_pos = wire_len;
    } else {
        in_reader = true;
        if (setjmp(reader_stop) == 0) {
            prov_task(NULL);
        }
        in_reader = false;
    }
    int64_t quiet = (wire_len ? wire_at[wire_len - 1] : clock_ms) + 3000;
    if (clock_ms < quiet) {
        clock_ms = quiet;
    }
}

/* The line the board answered a console line with first ("" for none), the
 * line sent as the page sends it: 64-byte pieces, 30 ms apart. */
static const char *answer_to(const char *line)
{
    said_len = 0;
    said[0] = '\0';
    wire_send(line, strlen(line), 64, 30);
    run_reader();
    char *nl = strchr(said, '\n');
    if (nl != NULL) {
        *nl = '\0';
    }
    return said;
}

/* A start, as after a reset: the reader's line starts empty, nothing has been
 * created, and app_main() runs as the board runs it. */
static void boot(void)
{
    s_len = 0;
    s_marked = s_skip = s_nul = false;
    s_last_ms = 0;
    started[0] = '\0';
    mqtt_inits = 0;
    in_boot = true;
    if (setjmp(boot_stop) == 0) {
        app_main();
    }
    in_boot = false;
}

/* A board holding `stored` (nothing, for NULL), just started. */
static void start_with(const chirp_config_t *stored)
{
    memset(&nvs, 0, sizeof(nvs));
    power_on();
    if (stored != NULL) {
        check(chirp_store_save(stored) == ESP_OK, "the settings before save", NULL);
    }
    boot();
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

/* Settings from a vector's "stored" or "after", field by field, straight
 * from the vector: never through the merge under test, so a merge bug cannot
 * agree with itself. */
static bool settings_from(const cJSON *v, chirp_config_t *out)
{
    memset(out, 0, sizeof(*out));
    for (const cJSON *it = v->child; it != NULL; it = it->next) {
        const char *k = it->string;
        if (strcmp(k, "aps") == 0) {
            const cJSON *ap = NULL;
            cJSON_ArrayForEach(ap, it) {
                const cJSON *s = cJSON_GetObjectItemCaseSensitive(ap, "ssid");
                const cJSON *w = cJSON_GetObjectItemCaseSensitive(ap, "pass");
                if (out->ap_count >= CHIRP_MAX_APS || !cJSON_IsString(s) || !cJSON_IsString(w)) {
                    return false;
                }
                strlcpy(out->aps[out->ap_count].ssid, s->valuestring, sizeof(out->aps[0].ssid));
                strlcpy(out->aps[out->ap_count].pass, w->valuestring, sizeof(out->aps[0].pass));
                out->ap_count++;
            }
            continue;
        }
        if (!cJSON_IsString(it)) {
            return false;
        }
        const char *s = it->valuestring;
        size_t n = 0;
        if (strcmp(k, "ssid") == 0) {
            strlcpy(out->ssid, s, sizeof(out->ssid));
        } else if (strcmp(k, "pass") == 0) {
            strlcpy(out->pass, s, sizeof(out->pass));
        } else if (strcmp(k, "token") == 0) {
            strlcpy(out->cred, s, sizeof(out->cred));
            out->cred_kind = CHIRP_CRED_TOKEN;
        } else if (strcmp(k, "hwid") == 0) {
            strlcpy(out->hwid, s, sizeof(out->hwid));
        } else if (strcmp(k, "host") == 0) {
            strlcpy(out->host, s, sizeof(out->host));
        } else if (strcmp(k, "key") == 0 &&
                   mbedtls_base64_decode(out->key, sizeof(out->key), &n, (const unsigned char *)s, strlen(s)) == 0 &&
                   n == sizeof(out->key)) {
            out->has_key = true;
        } else {
            return false;
        }
    }
    return true;
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

static void a_full_setup(chirp_config_t *c, const char *id, const char *token)
{
    memset(c, 0, sizeof(*c));
    strlcpy(c->ssid, "home", sizeof(c->ssid));
    strlcpy(c->pass, "pw", sizeof(c->pass));
    strlcpy(c->hwid, id, sizeof(c->hwid));
    strlcpy(c->cred, token, sizeof(c->cred));
    c->cred_kind = CHIRP_CRED_TOKEN;
}

/* Startup, as the board runs it. Never set up, it starts the setup reader and
 * nothing else, and the reader answers a probe with this demo's announce,
 * made from the DEMO main.c hands warblet_start(): its slug, its routes and
 * its default id, checked against demo.json. */
static void test_startup(void)
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
    strlcpy(app.version, fw->valuestring, sizeof(app.version));  /* the version the build stamps */

    start_with(NULL);
    check(strcmp(started, "warblet_setup") == 0 && mqtt_inits == 0,
          "never set up: startup starts the setup reader, and no network and no transport", started);
    check(console_driver != NULL, "startup installs the console's driver", NULL);

    const char *line = answer_to("CHIRP?\r\n");
    char *caps = strstr(line, " caps=");
    char want_head[160];
    snprintf(want_head, sizeof(want_head), "CHIRP! v=1 hw=%sabcdef fw=%s key_set=0 radios=wifi t=%s",
             DEMO.hwid_prefix, fw->valuestring, slug->valuestring);
    check(caps != NULL && strncmp(line, want_head, strlen(want_head)) == 0 && caps == line + strlen(want_head),
          "the announce is v1.8's line: the default id, fw, key_set, radios, t, caps last", line);
    cJSON *announced = caps ? cJSON_Parse(caps + 6) : NULL;
    check(announced != NULL && cJSON_Compare(announced, cap, true),
          "the announce's caps are demo.json's capabilities", caps);
    cJSON_Delete(announced);
    check(strlen(line) <= 512, "the announce fits 512 bytes", NULL);

    /* Set up with a key: the next start joins Wi-Fi and starts the transport,
     * and the announce names the id and says a key is held. */
    check(strcmp(answer_to("CHIRP+ {\"ssid\":\"home\",\"pass\":\"pw\",\"token\":\"tok-1\",\"hwid\":\"board-1\","
                           "\"key\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\"}\r\n"),
                 "CHIRP= ok") == 0,
          "a whole setup with a key is stored", said);
    boot();
    check(strstr(started, "wifi_ladder") != NULL &&
          (strstr(started, "chirp_send") != NULL || strstr(started, "chirp_mqtt") != NULL),
          "set up: the next start joins Wi-Fi and starts the transport", started);
    line = answer_to("CHIRP?\r\n");
    check(strstr(line, " hw=board-1 ") != NULL && strstr(line, " key_set=1 ") != NULL,
          "set up with a key: the announce carries the id and key_set=1", line);
    cJSON_Delete(doc);
    free(text);
}

/* The reader on the console: what comes in is taken without waiting, in
 * reads that come back short, and a line is timed by its own bytes. */
static void test_reader(void)
{
    chirp_config_t base, held;
    a_full_setup(&base, "board-1", "tok-1");
    const char *head = "CHIRP+ {\"hwid\":";

    /* Small pieces a second apart (a browser tab out of view): it lands. */
    static const char paced[] = "CHIRP+ {\"ssid\":\"paced-network\",\"pass\":\"paced-password\","
                                "\"token\":\"tok-paced-0123456789\",\"hwid\":\"board-paced\","
                                "\"host\":\"paced.example.com\"}\r\n";
    start_with(&base);
    said_len = 0;
    said[0] = '\0';
    wire_send(paced, strlen(paced), 20, 1000);
    run_reader();
    check(strncmp(said, "CHIRP= ok", 9) == 0, "a push in 20-byte pieces a second apart lands", said);

    /* A pause of 1.4 s inside a push is under the limit: it lands. */
    start_with(&base);
    said_len = 0;
    said[0] = '\0';
    wire_send(head, strlen(head), 64, 30);
    wire_pause(1400);
    wire_send("\"board-slow\"}\r\n", 16, 64, 30);
    run_reader();
    check(strncmp(said, "CHIRP= ok", 9) == 0, "a push that pauses 1.4 s lands", said);

    /* A push whose bytes stop for 1.6 s, then the rest: given up. */
    start_with(&base);
    said_len = 0;
    said[0] = '\0';
    wire_send(head, strlen(head), 64, 30);
    wire_pause(1600);
    wire_send("\"board-late\"}\r\n", 16, 64, 30);
    run_reader();
    chirp_store_load(&held);
    check(said_len == 0 && same_settings(&held, &base),
          "a push whose bytes stop for 1.6 s is given up: no answer, nothing stored", said);

    /* A probe right after a push that stopped: the announce, and no refusal. */
    start_with(&base);
    said_len = 0;
    said[0] = '\0';
    wire_send(head, strlen(head), 64, 30);
    wire_pause(1600);
    wire_send("CHIRP?\r\n", 8, 64, 30);
    run_reader();
    check(strncmp(said, "CHIRP! ", 7) == 0 && strstr(said, "CHIRP= ") == NULL,
          "a probe right after a push that stopped for 1.6 s gets the announce and no bad-json", said);

    /* A flood, all there at once: taken at most READ_BATCH bytes before the
     * other tasks run, and the probe behind it is answered. */
    static char flood[4096];
    memset(flood, 'z', sizeof(flood));
    start_with(&base);
    said_len = 0;
    said[0] = '\0';
    most_between_yields = 0;
    wire_send(flood, sizeof(flood), sizeof(flood), 0);
    wire_send("CHIRP?\r\n", 8, 64, 30);
    run_reader();
    char detail[96];
    snprintf(detail, sizeof(detail), "%u bytes taken with no other task run between", (unsigned)most_between_yields);
    check(most_between_yields <= READ_BATCH, "a flood is taken at most 1 KB a turn, other tasks running between",
          detail);
    check(strncmp(said, "CHIRP! ", 7) == 0, "...and the probe behind it is answered", said);
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
        check(settings_from(cJSON_GetObjectItemCaseSensitive(v, "after"), &after), "the settings after load", name);

        start_with(&stored);
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

/* A credential at its limit: 63 characters of printable ASCII, a third of them
 * quotation marks and backslashes, with an id at its limit too. Stored as
 * sent, read back by the next start, and sent as it is by the transport. */
static void test_credential_at_limit(void)
{
    static const char mix[] = "\"\\a\\\"b{\"}\\:,'~";
    char cred[CHIRP_CRED_SIZE];
    for (int i = 0; i < 63; i++) {
        cred[i] = mix[i % (sizeof(mix) - 1)];
    }
    cred[63] = '\0';
    const char *id = "board-0123456789-0123456789-abc";  /* 31, what every board holds */

    for (int kind = CHIRP_CRED_CLAIM; kind <= CHIRP_CRED_TOKEN; kind++) {
        const char *field = kind == CHIRP_CRED_CLAIM ? "claim" : "token";
        char what[160];

        start_with(NULL);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", "home");
        cJSON_AddStringToObject(o, "pass", "pw");
        cJSON_AddStringToObject(o, field, cred);
        cJSON_AddStringToObject(o, "hwid", id);
        char *json = cJSON_PrintUnformatted(o);
        char line[512];
        snprintf(line, sizeof(line), "CHIRP+ %s\r\n", json);
        cJSON_free(json);
        cJSON_Delete(o);
        int restarted = restarts;
        snprintf(what, sizeof(what), "a %s of 63 characters with quotation marks and backslashes: ok", field);
        check(strcmp(answer_to(line), "CHIRP= ok") == 0 && restarts > restarted, what, said);

        chirp_config_t held;
        chirp_store_load(&held);
        snprintf(what, sizeof(what), "...the %s is stored as sent, byte for byte", field);
        check(strcmp(held.cred, cred) == 0 && held.cred_kind == (chirp_cred_kind_t)kind &&
              strcmp(held.hwid, id) == 0, what, held.cred);

        boot();  /* the next start reads it and starts the transport */
        bool claim = kind == CHIRP_CRED_CLAIM;
#if defined(CHIRP_PORT)
        /* TCP: the hello, "CHIRP1 <id> <token>" or "... claim:<code>", one line. */
        char want[160];
        snprintf(want, sizeof(want), "CHIRP1 %s %s%s\n", id, claim ? "claim:" : "", cred);
        sent_len = 0;
        s_socket_fd = 3;
        esp_err_t err = send_hello();
        s_socket_fd = -1;
        snprintf(what, sizeof(what), "...and the TCP hello carries it exactly (%s)", field);
        check(err == ESP_OK && sent_len == strlen(want) && memcmp(sent, want, sent_len) == 0, what, want);
#elif defined(HTTP_TIMEOUT_MS)
        /* HTTPS: the token header, and with a setup code the claim header too. */
        static const uint8_t frame[] = {0xff, 0xd8, 0xff, 0xd9};
        chirp_send_result_t result = chirp_send_bytes(frame, sizeof(frame), CHIRP_TAG_HEARTBEAT);
        const char *token = header("X-Chirp-Token"), *code = header("X-Chirp-Claim");
        snprintf(what, sizeof(what), "...and the HTTPS request's headers carry it exactly (%s)", field);
        check(result == CHIRP_SENT && token != NULL && strcmp(token, cred) == 0 &&
              (claim ? code != NULL && strcmp(code, cred) == 0 : code == NULL), what, token);
#elif defined(CHIRP_MQTT_PORT)
        /* MQTT: the CONNECT's password, with the id as user and client. Both
         * kinds go in the password. */
        (void)claim;
        snprintf(what, sizeof(what), "...and the MQTT CONNECT carries it exactly as its password (%s)", field);
        check(mqtt_inits == 1 && strcmp(mqtt_password, cred) == 0 && strcmp(mqtt_user, id) == 0 &&
              strcmp(mqtt_id, id) == 0, what, mqtt_password);
#else
        (void)claim;
        check(false, "the transport is one this test knows how to read", NULL);
#endif
    }
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

    /* A power cut at every step of a save: the next start reads the old or
     * the new settings, whole, and the new ones whenever the save said ok. */
    for (int k = 0; k < 8; k++) {
        memset(&nvs, 0, sizeof(nvs));
        power_on();
        chirp_store_save(&old);
        chirp_config_t before;
        chirp_store_load(&before);
        cut_after = k;
        esp_err_t err = chirp_store_save(&new);
        power_on();
        chirp_store_load(&got);
        bool whole = same_settings(&got, &before) || same_settings(&got, &new);
        char what[64];
        snprintf(what, sizeof(what), "a power cut after %d step(s) of a save", k);
        check(whole && (err != ESP_OK || same_settings(&got, &new)), what, NULL);
    }


    /* The store is one record (the owner's ruling: v1.6.0 never shipped, so
     * there is nothing earlier to keep). A board flashed over values another
     * image left finds no settings, and the page sets it up. */
    memset(&nvs, 0, sizeof(nvs));
    power_on();
    chirp_ap_t other_aps[2] = {{"cafe", "p1"}, {"office", "p2"}};
    put("aps", 'b', other_aps, sizeof(other_aps));          /* v1.6's network list */
    put("hwid", 's', "board-other", sizeof("board-other"));  /* and the entries before it */
    put("cred", 's', "tok-other", sizeof("tok-other"));
    chirp_store_load(&got);
    check(got.hwid[0] == '\0' && got.cred[0] == '\0' && got.ap_count == 0 && got.ssid[0] == '\0',
          "values another image left are not read: the board starts as never set up", NULL);
}

static void test_line(void)
{
    chirp_config_t base, held;
    a_full_setup(&base, "board-1", "tok-1");
    const char *inner = "CHIRP+ {\"hwid\":\"board-new\"}";

    /* No tail of an over-long line is a push: an inner push before, across or
     * past the limit. */
    start_with(&base);
    bool none = true;
    for (size_t cut_at = 0; cut_at < strlen(inner); cut_at++) {
        static char line[CHIRP_LINE_MAX + 128];
        size_t pad = CHIRP_LINE_MAX - 7 - cut_at;
        memcpy(line, "CHIRP+ ", 7);
        memset(line + 7, 'j', pad);
        sprintf(line + 7 + pad, "%s\r\n", inner);
        none = none && answer_to(line)[0] == '\0';
    }
    chirp_store_load(&held);
    check(none && same_settings(&held, &base), "no tail of a line over the limit is read as a push", NULL);
    check(strcmp(answer_to("CHIRP+ {\"hwid\":\"board-2\"}\r\n"), "CHIRP= ok") == 0,
          "the line after it is read", said);

    /* The limit counts from "CHIRP": exactly 4096 bytes after stray bytes. */
    {
        static char line[CHIRP_LINE_MAX + 128];
        start_with(&base);
        memset(line, 'z', 50);
        char *p = line + 50;
        strcpy(p, "CHIRP+ {\"hwid\":\"board-3\"}");
        size_t n = strlen(p);
        memset(p + n, ' ', CHIRP_LINE_MAX - n);
        strcpy(p + CHIRP_LINE_MAX, "\r\n");
        check(strcmp(answer_to(line), "CHIRP= ok") == 0, "a line of exactly 4096 from CHIRP, after stray bytes, is read",
              said);
    }

    /* Stray bytes in front of a command are dropped, and not kept. */
    start_with(&base);
    answer_to("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz");
    check(s_len <= JUNK_KEPT, "stray bytes before CHIRP are not kept", NULL);
    check(strncmp(answer_to("\x5a\xa6" "CHIRP?\r\n"), "CHIRP! ", 7) == 0, "a probe after stray bytes is answered",
          said);

    /* A raw NUL in a push: the line is refused whole and nothing is stored,
     * even where the text before it is a whole push of its own. The vectors'
     * raw NUL reaches this test cut short (cJSON's strings end at it), so it
     * is sent here, byte by byte. */
    static const char raw[] = "CHIRP+ {\"token\":\"tok-2\"}\0 and the rest\r\n";
    start_with(&base);
    said_len = 0;
    said[0] = '\0';
    wire_send(raw, sizeof(raw) - 1, 64, 30);
    run_reader();
    chirp_store_load(&held);
    check(strncmp(said, "CHIRP= err bad-json", 19) == 0 && same_settings(&held, &base),
          "a raw NUL in a push: bad-json, and nothing stored", said);
}

static void test_networks(void)
{
    /* One name with two passwords is two networks to try, in order; only an
     * identical pair is dropped. */
    chirp_config_t c = {0};
    strlcpy(c.ssid, "lab", sizeof(c.ssid));
    strlcpy(c.pass, "old", sizeof(c.pass));
    c.ap_count = 3;
    strlcpy(c.aps[0].ssid, "lab", sizeof(c.aps[0].ssid));
    strlcpy(c.aps[0].pass, "old", sizeof(c.aps[0].pass));
    strlcpy(c.aps[1].ssid, "lab", sizeof(c.aps[1].ssid));
    strlcpy(c.aps[1].pass, "new", sizeof(c.aps[1].pass));
    strlcpy(c.aps[2].ssid, "lab", sizeof(c.aps[2].ssid));
    strlcpy(c.aps[2].pass, "old", sizeof(c.aps[2].pass));
    const char *s0 = NULL, *p0 = NULL, *s1 = NULL, *p1 = NULL, *s2 = NULL, *p2 = NULL;
    bool first = chirp_config_network(&c, 0, &s0, &p0);
    bool second = chirp_config_network(&c, 1, &s1, &p1);
    bool third = chirp_config_network(&c, 2, &s2, &p2);
    check(first && second && !third && strcmp(p0, "old") == 0 && strcmp(s1, "lab") == 0 && strcmp(p1, "new") == 0,
          "one name with two passwords: both tried, the first password first, the repeat once", NULL);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: provtest <path to chirp-prov-v1.8.json>\n");
        return 2;
    }
    test_startup();  /* first: the first start is the one that has never been set up */
    test_reader();
    test_vectors(argv[1]);
    test_credential_at_limit();
    test_store();
    test_line();
    test_networks();
    char detail[64];
    snprintf(detail, sizeof(detail), "%d read(s) were allowed to wait", waits);
    check(waits == 0, "the reader never let a console read wait, in any run", detail);
    printf("provtest: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
