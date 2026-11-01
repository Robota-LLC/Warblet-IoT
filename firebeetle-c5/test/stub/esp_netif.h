/* Host stand-in (test only): enough for warblet.c to compile; never called. */
#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { esp_ip4_addr_t ip; } esp_netif_ip_info_t;
typedef struct { esp_netif_ip_info_t ip_info; } ip_event_got_ip_t;
#define IP_EVENT_STA_GOT_IP 0
#define IPSTR "%d.%d.%d.%d"
#define IP2STR(a) 0, 0, 0, (int)(a)->addr
static inline esp_err_t esp_netif_init(void) { return ESP_OK; }
static inline void *esp_netif_create_default_wifi_sta(void) { return 0; }
