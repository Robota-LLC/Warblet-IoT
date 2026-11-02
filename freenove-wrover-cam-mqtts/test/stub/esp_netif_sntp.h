/* Host stand-in (test only). */
#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
typedef struct {
    const char *server;
} esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(srv) { .server = (srv) }
static inline esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *c) { (void)c; return ESP_OK; }
static inline esp_err_t esp_netif_sntp_sync_wait(TickType_t t) { (void)t; return ESP_OK; }
