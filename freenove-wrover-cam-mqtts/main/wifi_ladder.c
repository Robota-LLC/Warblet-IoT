#include <stdlib.h>
#include <string.h>

#include "wifi_ladder.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "wifi_ladder";

#define BIT_CONNECTED    BIT0  /* station has an IP address */
#define BIT_DISCONNECTED BIT1  /* attempt failed, or the link dropped */

#define CONNECT_TIMEOUT_MS 12000
#define LADDER_REST_MS     15000

static chirp_config_t     s_config;
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

/* Orders the stored networks: the ones this scan actually saw first, strongest
 * RSSI first, then the remainder in the order they were provisioned. */
static size_t order_candidates(uint8_t *order)
{
    bool   seen[CHIRP_MAX_APS] = {0};
    int8_t rssi[CHIRP_MAX_APS] = {0};

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
                for (uint8_t i = 0; i < s_config.ap_count; i++) {
                    if (strncmp(s_config.aps[i].ssid, (const char *)records[r].ssid,
                                sizeof(records[r].ssid)) != 0) {
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

    size_t n = 0;
    for (uint8_t pass = 0; pass < 2; pass++) {
        bool want_seen = (pass == 0);
        for (;;) {
            int best = -1;
            for (uint8_t i = 0; i < s_config.ap_count; i++) {
                bool used = false;
                for (size_t k = 0; k < n; k++) {
                    used = used || (order[k] == i);
                }
                if (used || seen[i] != want_seen) {
                    continue;
                }
                if (best < 0 || (want_seen && rssi[i] > rssi[best])) {
                    best = i;
                }
                if (!want_seen) {
                    break;  /* unseen networks keep their provisioned order */
                }
            }
            if (best < 0) {
                break;
            }
            order[n++] = (uint8_t)best;
        }
    }
    return n;
}

static bool try_connect(const chirp_ap_t *ap)
{
    wifi_config_t wc = {0};
    /* sta.ssid/password are fixed-size fields, not necessarily NUL terminated. */
    memcpy(wc.sta.ssid, ap->ssid, strnlen(ap->ssid, sizeof(wc.sta.ssid)));
    memcpy(wc.sta.password, ap->pass, strnlen(ap->pass, sizeof(wc.sta.password)));
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
        ESP_LOGW(TAG, "connect to \"%s\": %s", ap->ssid, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "trying \"%s\"", ap->ssid);
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
    uint8_t order[CHIRP_MAX_APS];

    for (;;) {
        size_t n = order_candidates(order);
        bool connected = false;
        for (size_t i = 0; i < n && !connected; i++) {
            connected = try_connect(&s_config.aps[order[i]]);
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
    if (config == NULL || config->ap_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;

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
    /* No band mode to set: a classic ESP32 is 2.4 GHz only. */
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
