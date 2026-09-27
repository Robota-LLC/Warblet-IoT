/* Capture scheduled and requested JPEG frames, then publish over MQTTS.
 * The camera module handles sensor setup and buffer ownership. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "camera.h"
#include "chirp_mqtt.h"
#include "chirp_prov.h"
#include "chirp_store.h"
#include "wifi_ladder.h"

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"

static const char *TAG = "warblet";

/* ---------------------------------------------------------------------------
 * the camera
 * ------------------------------------------------------------------------ */

/* Capture, publish, and release a frame. Skip the report if capture fails. */
static chirp_send_result_t send_camera_frame(const char *tag)
{
    const uint8_t *frame        = NULL;
    size_t         frame_length = 0;

    esp_err_t err = camera_capture(&frame, &frame_length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "capture: %s", esp_err_to_name(err));
        return CHIRP_SKIPPED;
    }

    chirp_send_result_t result = chirp_mqtt_send_bytes(frame, frame_length, tag);
    camera_release();  /* the frame buffer goes back to the driver either way */
    return result;
}

/* Return the response tag for a recognized command, or NULL.
 * The send task performs the capture. */
static const char *handle_command(const char *command)
{
    if (strcmp(command, CHIRP_CMD_SNAP) == 0) {
        ESP_LOGI(TAG, "down %s -- capturing now", CHIRP_CMD_SNAP);
        return CHIRP_TAG_SNAP;
    }
    ESP_LOGI(TAG, "downlink ignored: %s", command);
    return NULL;
}

/* ---------------------------------------------------------------------------
 * start-up
 * ------------------------------------------------------------------------ */

/* Use blocking UART0 input through the CH340 bridge for serial setup.
 * Keep LF line endings; the parser strips a trailing CR. */
static void console_init(void)
{
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 1024, 0, 0, NULL, 0));

    uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_LF);
    uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_LF);
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

    /* Blocking, unbuffered stdin, so the reader task sleeps in fgets(). */
    fcntl(fileno(stdin), F_SETFL, 0);
    fcntl(fileno(stdout), F_SETFL, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
}

void app_main(void)
{
    ESP_ERROR_CHECK(chirp_store_init());
    console_init();

    chirp_config_t config;
    ESP_ERROR_CHECK(chirp_store_load(&config));
    ESP_LOGI(TAG, "hwid=%s host=%s networks=%u credential=%s signing-key=%s",
             chirp_store_hwid(&config), config.host, config.ap_count,
             config.cred_kind == CHIRP_CRED_CLAIM ? "claim" :
             config.cred_kind == CHIRP_CRED_TOKEN ? "token" : "none",
             config.has_key ? "yes" : "no");

    /* Always listening, provisioned or not. */
    ESP_ERROR_CHECK(chirp_prov_start(&config));

    /* Camera before network, and fatal on purpose: a board with no ribbon
     * seated should say so at the console. */
    ESP_ERROR_CHECK(camera_init());

    if (config.ap_count == 0) {
        ESP_LOGI(TAG, "no networks stored, waiting to be provisioned");
        return;
    }
    ESP_ERROR_CHECK(wifi_ladder_start(&config));

    /* With no credential, connect without a password. */
    if (config.cred_kind == CHIRP_CRED_NONE) {
        ESP_LOGI(TAG, "no claim code or token stored, reporting as open (L0)");
    }
    ESP_ERROR_CHECK(chirp_mqtt_start(&config, send_camera_frame, handle_command));
}
