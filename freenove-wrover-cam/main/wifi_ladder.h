/* Scan-ordered connection ladder over the stored networks. */
#pragma once

#include <stdbool.h>

#include "chirp_store.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start Wi-Fi and its reconnect task with a copy of config. */
esp_err_t wifi_ladder_start(const chirp_config_t *config);

/* Blocks until the station has an IP address (or the timeout expires). */
bool wifi_ladder_wait_connected(TickType_t ticks_to_wait);

#ifdef __cplusplus
}
#endif
