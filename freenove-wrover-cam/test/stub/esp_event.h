/* Host stand-in (test only): enough for warblet.c to compile; never called. */
#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef const char *esp_event_base_t;
typedef void (*esp_event_handler_t)(void *arg, esp_event_base_t base, int32_t id, void *data);
#define WIFI_EVENT       "WIFI_EVENT"
#define IP_EVENT         "IP_EVENT"
#define ESP_EVENT_ANY_ID (-1)
static inline esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
static inline esp_err_t esp_event_handler_instance_register(esp_event_base_t b, int32_t id, esp_event_handler_t f,
                                                            void *arg, void *inst)
{ (void)b; (void)id; (void)f; (void)arg; (void)inst; return ESP_OK; }
