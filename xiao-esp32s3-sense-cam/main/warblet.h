/* Warblet's helper for ESP-IDF boards: USB setup (CHIRP-PROV v1.8), the stored
 * settings, joining Wi-Fi and signing. The same file in every ESP-IDF demo;
 * what makes a demo itself (its id prefix, its host, its routes) comes from
 * main.c through warblet_start(). The transport (chirp_send.c or chirp_mqtt.c)
 * stays the demo's own. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- what a demo tells the helper ------------------------------------------ */

typedef struct {
    const char *slug;          /* the catalogue id, t= in the announce */
    const char *hwid_prefix;   /* the default id: this + the MAC's last three bytes */
    const char *default_host;  /* where it reports unless a host is stored */
    const char *transports;    /* its routes, as the capability object lists them: "\"https\"" */
} warblet_demo_t;

/* Records the demo and brings up NVS (formatting it if the partition is stale).
 * Call it first. */
esp_err_t warblet_start(const warblet_demo_t *demo);

/* ---- the stored settings ------------------------------------------------------ */

#define CHIRP_MAX_APS      8
#define CHIRP_SSID_SIZE    33   /* 32 byte SSID + NUL */
#define CHIRP_PASS_SIZE    65   /* 64 byte PSK + NUL  */
#define CHIRP_HOST_SIZE    64
#define CHIRP_HWID_SIZE    32   /* the contract asks every board to hold 31 */
#define CHIRP_CRED_SIZE    64
#define CHIRP_KEY_SIZE     32   /* HMAC-SHA256 key; 44 characters of base64 on the wire */

/* Both kinds go where the token goes; the transport says which it is. */
typedef enum {
    CHIRP_CRED_NONE = 0,
    CHIRP_CRED_CLAIM,
    CHIRP_CRED_TOKEN,
} chirp_cred_kind_t;

typedef struct {
    char ssid[CHIRP_SSID_SIZE];
    char pass[CHIRP_PASS_SIZE];
} chirp_ap_t;

typedef struct {
    /* The network tried first. After a push of aps it is the list's first. */
    char              ssid[CHIRP_SSID_SIZE];
    char              pass[CHIRP_PASS_SIZE];
    chirp_ap_t        aps[CHIRP_MAX_APS];  /* the list pushed as aps, in order */
    uint8_t           ap_count;            /* 0: no list, the network above alone */
    char              host[CHIRP_HOST_SIZE];  /* empty: the demo's default host */
    char              hwid[CHIRP_HWID_SIZE];  /* empty: the default id */
    char              cred[CHIRP_CRED_SIZE];
    chirp_cred_kind_t cred_kind;
    uint8_t           key[CHIRP_KEY_SIZE];  /* a stored signing key enables signatures */
    bool              has_key;
} chirp_config_t;

/* Always fills *out: nothing set up (all empty) when nothing is stored, or
 * when what is stored cannot be read (it says so in the log), so USB setup
 * always starts. ESP_OK unless out is NULL. */
esp_err_t chirp_store_load(chirp_config_t *out);

/* Replaces the stored settings with *config in one NVS write. ESP_OK only once
 * that write and its commit are done; any error from either is a store the
 * board cannot confirm, answered store-failed. */
esp_err_t chirp_store_save(const chirp_config_t *config);

/* The demo's id prefix + the last three bytes of the station MAC, lowercase hex. */
const char *chirp_store_default_hwid(void);

/* The stored id if there is one, otherwise the default id. */
const char *chirp_store_hwid(const chirp_config_t *config);

/* The stored host if there is one, otherwise the demo's default host. */
const char *chirp_store_host(const chirp_config_t *config);

/* The i-th network to try, from 0: the first network, then the rest of the
 * list, each name once. False past the last. */
bool chirp_config_network(const chirp_config_t *config, size_t i, const char **ssid, const char **pass);

/* ---- USB setup ---------------------------------------------------------------- */

/* Takes up to size bytes that have already come in on the console and never
 * waits for more. Returns how many it took: 0 when none has come, below 0 on
 * a driver error. main.c's console_read() is one. */
typedef int (*chirp_console_read_fn)(uint8_t *buf, size_t size);

/* Starts the console reader with a copy of config, which a push merges onto,
 * and the function it reads the console with. It answers CHIRP? with the
 * announce and CHIRP+ with a stored push (then a restart) or a refusal. */
esp_err_t chirp_prov_start(const chirp_config_t *config, chirp_console_read_fn read_console);

/* ---- Wi-Fi ---------------------------------------------------------------------- */

/* Starts Wi-Fi and the task that keeps it joined, with a copy of config. */
esp_err_t wifi_ladder_start(const chirp_config_t *config);

/* Blocks until the station has an IP address (or the timeout expires). */
bool wifi_ladder_wait_connected(TickType_t ticks_to_wait);

/* ---- signing -------------------------------------------------------------------- */

#define CHIRP_SIG_SIZE     32   /* the raw HMAC-SHA256 in front of an MQTT or TCP payload */
#define CHIRP_SIG_HEX_SIZE 65   /* the same as lowercase hex, + NUL, for an HTTP header */
#define CHIRP_NONCE_SIZE   24   /* a millisecond Unix time in decimal, + NUL */
#define CHIRP_TAG_MAX      32   /* a tag is at most 32 characters */

/* Room for the longest prefix chirp_sign_prefix() writes, NUL included. */
#define CHIRP_SIGN_PREFIX_MAX (CHIRP_NONCE_SIZE + CHIRP_TAG_MAX + sizeof("\ntag=\n"))

/* Writes the two lines every signature covers in front of the body:
 *     <nonce> "\n" "tag=" <tag> "\n"
 * Both lines are always written; NULL or "" signs that field as empty.
 * Returns the length, or -1 if it does not fit in outsz. */
int chirp_sign_prefix(char *out, size_t outsz, const char *nonce, const char *tag);

/* out = HMAC-SHA256(key, a followed by b), the 32 raw bytes. a may be NULL
 * when a_len is zero, b when b_len is. */
esp_err_t chirp_sign_raw(const uint8_t *key, size_t key_len,
                         const uint8_t *a, size_t a_len,
                         const uint8_t *b, size_t b_len,
                         uint8_t out[CHIRP_SIG_SIZE]);

/* The same as lowercase hex, NUL terminated. */
esp_err_t chirp_sign_hex2(const uint8_t *key, size_t key_len,
                          const uint8_t *a, size_t a_len,
                          const uint8_t *b, size_t b_len,
                          char out[CHIRP_SIG_HEX_SIZE]);

/* An increasing Unix-millisecond nonce, or false while the clock is unset. */
bool chirp_sign_nonce(char out[CHIRP_NONCE_SIZE]);

#ifdef __cplusplus
}
#endif
