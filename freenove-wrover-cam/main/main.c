/* Capture scheduled and requested JPEG frames, then send them over HTTPS.
 * The camera module handles sensor setup and buffer ownership. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "camera.h"
#include "warblet.h"
#include "chirp_send.h"

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "warblet";

/* What this demo is: its catalogue id (sent in the USB announce), its
 * default id, where it reports, and its route. */
static const warblet_demo_t DEMO = {
    .slug         = "demo-wrover-cam",
    .hwid_prefix  = "cam-",                 /* the default id: this + the MAC's last three bytes */
    .default_host = "https.warbletiot.com", /* a host the setup page stored wins */
    .transports   = "\"https\"",
};

/* ---------------------------------------------------------------------------
 * the camera
 * ------------------------------------------------------------------------ */

/* Capture, send, and release a frame. Skip the report if capture fails. */
static chirp_send_result_t send_camera_frame(const char *tag)
{
    const uint8_t *frame        = NULL;
    size_t         frame_length = 0;

    esp_err_t err = camera_capture(&frame, &frame_length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "capture: %s", esp_err_to_name(err));
        return CHIRP_SKIPPED;
    }

    chirp_send_result_t result = chirp_send_bytes(frame, frame_length, tag);
    camera_release();  /* the frame buffer goes back to the driver either way */
    return result;
}

/* Returns the tag to report with at once, or NULL. The frame that answers a
 * command is tagged with that command. */
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

    /* stdin does not block, so the setup reader can tell a line that
     * stopped coming; stdout does, so an answer is never cut short. */
    fcntl(fileno(stdin), F_SETFL, O_NONBLOCK);
    fcntl(fileno(stdout), F_SETFL, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
}

void app_main(void)
{
    ESP_ERROR_CHECK(warblet_start(&DEMO));
    console_init();

    chirp_config_t config;
    ESP_ERROR_CHECK(chirp_store_load(&config));
    ESP_LOGI(TAG, "hwid=%s host=%s credential=%s signing-key=%s",
             chirp_store_hwid(&config), chirp_store_host(&config),
             config.cred_kind == CHIRP_CRED_CLAIM ? "claim" :
             config.cred_kind == CHIRP_CRED_TOKEN ? "token" : "none",
             config.has_key ? "yes" : "no");

    /* Always listening, set up or not. */
    ESP_ERROR_CHECK(chirp_prov_start(&config));

    /* Camera before network. With no camera there is nothing to send, but a
     * restart loop would also stop the board being set up over USB: so stay
     * up, send nothing, and say why once a minute. */
    esp_err_t camera = camera_init();
    if (camera != ESP_OK) {
        for (;;) {
            ESP_LOGE(TAG, "no camera (%s): power the board off, check the "
                          "camera ribbon sits fully in its socket, then power "
                          "it on", esp_err_to_name(camera));
            vTaskDelay(pdMS_TO_TICKS(60000));
        }
    }

    /* A network and a credential, or nothing is sent: this demo has no Open mode. */
    if (config.ssid[0] == '\0' || config.cred_kind == CHIRP_CRED_NONE) {
        ESP_LOGI(TAG, "not set up yet, waiting for the setup page");
        return;
    }
    ESP_ERROR_CHECK(wifi_ladder_start(&config));

    ESP_ERROR_CHECK(chirp_send_start(&config, send_camera_frame, handle_command));
}
