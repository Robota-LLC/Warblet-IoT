/* Host stand-in (test only): an HTTP client the test answers, keeping the
 * headers a request was given. */
#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct esp_http_client *esp_http_client_handle_t;
typedef enum { HTTP_METHOD_GET = 0, HTTP_METHOD_POST } esp_http_client_method_t;
typedef struct {
    const char               *url;
    esp_http_client_method_t  method;
    int                       timeout_ms;
    esp_err_t               (*crt_bundle_attach)(void *conf);
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value);
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_len);
int       esp_http_client_write(esp_http_client_handle_t client, const char *buffer, int len);
int64_t   esp_http_client_fetch_headers(esp_http_client_handle_t client);
int       esp_http_client_read_response(esp_http_client_handle_t client, char *buffer, int len);
int       esp_http_client_get_status_code(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
