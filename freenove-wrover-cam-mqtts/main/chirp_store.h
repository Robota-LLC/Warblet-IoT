/* Persistent provisioning state (NVS) for the Warblet demo firmware. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHIRP_MAX_APS      8
#define CHIRP_SSID_SIZE    33   /* 32 byte SSID + NUL */
#define CHIRP_PASS_SIZE    65   /* 64 byte PSK + NUL  */
#define CHIRP_HOST_SIZE    64
#define CHIRP_HWID_SIZE    32
#define CHIRP_CRED_SIZE    64
#define CHIRP_KEY_SIZE     32   /* HMAC-SHA256 key; 44 chars of base64 on the wire */

/* Default MQTT hostname and TLS port. A stored host overrides the hostname. */
#define CHIRP_DEFAULT_HOST "mqtt.warbletiot.com"
#define CHIRP_DEFAULT_PORT 8883

/* Identify the stored credential as a claim or token. */
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
    chirp_ap_t        aps[CHIRP_MAX_APS];  /* ordered, as pushed */
    uint8_t           ap_count;            /* 0 => unprovisioned  */
    char              host[CHIRP_HOST_SIZE];
    char              hwid[CHIRP_HWID_SIZE];  /* empty => default identity */
    char              cred[CHIRP_CRED_SIZE];
    chirp_cred_kind_t cred_kind;
    /* A stored signing key enables message signatures. */
    uint8_t           key[CHIRP_KEY_SIZE];
    bool              has_key;
    int32_t           level;               /* informational, never acted on */
    bool              has_level;
} chirp_config_t;

/* Brings up NVS (formatting it if the partition is stale). */
esp_err_t chirp_store_init(void);

/* Always fills *out: defaults when nothing has been provisioned yet. */
esp_err_t chirp_store_load(chirp_config_t *out);

/* Replaces the stored provisioning state with *config. */
esp_err_t chirp_store_save(const chirp_config_t *config);

/* "cam-" + last three bytes of the station MAC, lowercase hex. */
const char *chirp_store_default_hwid(void);

/* Provisioned hwid if there is one, otherwise the default identity. */
const char *chirp_store_hwid(const chirp_config_t *config);

#ifdef __cplusplus
}
#endif
