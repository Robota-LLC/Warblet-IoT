/* Host stand-in (test only): an MQTT client the test answers, keeping the
 * credentials it was set up with. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_event.h"
typedef struct esp_mqtt_client *esp_mqtt_client_handle_t;
typedef enum {
    MQTT_EVENT_ANY = -1,
    MQTT_EVENT_ERROR = 0,
    MQTT_EVENT_CONNECTED,
    MQTT_EVENT_DISCONNECTED,
    MQTT_EVENT_SUBSCRIBED,
    MQTT_EVENT_UNSUBSCRIBED,
    MQTT_EVENT_PUBLISHED,
    MQTT_EVENT_DATA,
    MQTT_EVENT_BEFORE_CONNECT,
    MQTT_EVENT_DELETED,
} esp_mqtt_event_id_t;
typedef struct {
    esp_err_t esp_tls_last_esp_err;
    int       esp_tls_stack_err;
    int       error_type;
    int       connect_return_code;
    int       esp_transport_sock_errno;
} esp_mqtt_error_codes_t;
typedef struct {
    esp_mqtt_event_id_t      event_id;
    esp_mqtt_client_handle_t client;
    char                    *data;
    int                      data_len;
    int                      total_data_len;
    bool                     session_present;
    esp_mqtt_error_codes_t  *error_handle;
} esp_mqtt_event_t;
typedef esp_mqtt_event_t *esp_mqtt_event_handle_t;
typedef struct {
    struct {
        struct { const char *uri; } address;
        struct { esp_err_t (*crt_bundle_attach)(void *conf); } verification;
    } broker;
    struct {
        const char *username;
        const char *client_id;
        struct { const char *password; } authentication;
    } credentials;
    struct { int keepalive; bool disable_clean_session; } session;
    struct { bool disable_auto_reconnect; int timeout_ms; } network;
    struct { int size; int out_size; } buffer;
    struct { uint64_t limit; } outbox;
} esp_mqtt_client_config_t;
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config);
static inline esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client, esp_mqtt_event_id_t event,
                                                       esp_event_handler_t handler, void *arg)
{ (void)client; (void)event; (void)handler; (void)arg; return ESP_OK; }
static inline esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client) { (void)client; return ESP_OK; }
static inline esp_err_t esp_mqtt_client_reconnect(esp_mqtt_client_handle_t client) { (void)client; return ESP_OK; }
static inline int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char *topic, const char *data,
                                          int len, int qos, int retain)
{ (void)client; (void)topic; (void)data; (void)len; (void)qos; (void)retain; return 0; }
static inline int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client, const char *topic, int qos)
{ (void)client; (void)topic; (void)qos; return 0; }
