/* Host stand-in (test only): enough for warblet.c to compile; never called. */
#pragma once
#include <stdint.h>
#include "esp_err.h"
#define WIFI_EVENT_STA_DISCONNECTED 5
typedef struct { uint8_t ssid[32]; uint8_t ssid_len; uint8_t reason; } wifi_event_sta_disconnected_t;
typedef struct { uint8_t ssid[33]; int8_t rssi; } wifi_ap_record_t;
typedef struct {
    struct {
        uint8_t ssid[32];
        uint8_t password[64];
        int     scan_method;
        int     sort_method;
        struct { int authmode; } threshold;
    } sta;
} wifi_config_t;
typedef struct { int unused; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() {0}
#define WIFI_ALL_CHANNEL_SCAN     1
#define WIFI_CONNECT_AP_BY_SIGNAL 0
#define WIFI_AUTH_OPEN            0
#define WIFI_IF_STA               0
#define WIFI_MODE_STA             1
static inline esp_err_t esp_wifi_scan_start(const void *c, int block) { (void)c; (void)block; return ESP_FAIL; }
static inline esp_err_t esp_wifi_scan_get_ap_num(uint16_t *n) { *n = 0; return ESP_OK; }
static inline esp_err_t esp_wifi_scan_get_ap_records(uint16_t *n, wifi_ap_record_t *r) { (void)r; *n = 0; return ESP_OK; }
static inline esp_err_t esp_wifi_clear_ap_list(void) { return ESP_OK; }
static inline esp_err_t esp_wifi_set_config(int i, wifi_config_t *c) { (void)i; (void)c; return ESP_OK; }
static inline esp_err_t esp_wifi_connect(void) { return ESP_OK; }
static inline esp_err_t esp_wifi_disconnect(void) { return ESP_OK; }
static inline esp_err_t esp_wifi_init(const wifi_init_config_t *c) { (void)c; return ESP_OK; }
static inline esp_err_t esp_wifi_set_mode(int m) { (void)m; return ESP_OK; }
static inline esp_err_t esp_wifi_start(void) { return ESP_OK; }
