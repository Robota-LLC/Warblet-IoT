/* FireBeetle C5 temperature reports and LED commands.
 * Edit sensor setup, reading, and packing here; chirp_send manages the TCP connection. */
#include <fcntl.h>
#include <math.h>
#include <stdio.h>

#include "chirp_led.h"
#include "chirp_prov.h"
#include "chirp_send.h"
#include "chirp_store.h"
#include "wifi_ladder.h"

#include "driver/temperature_sensor.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"

static const char *TAG = "warblet";

/* Two bytes: round(temp_c * 10) as a signed big-endian int16. Keep
 * ../decoder.star consistent with this format. */
#define TEMPERATURE_PAYLOAD_SIZE 2

static temperature_sensor_handle_t temperature_sensor;

/* ---------------------------------------------------------------------------
 * the sensor
 * ------------------------------------------------------------------------ */

static esp_err_t temperature_sensor_start(void)
{
    temperature_sensor_config_t sensor_config =
        TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);

    esp_err_t err = temperature_sensor_install(&sensor_config, &temperature_sensor);
    if (err != ESP_OK) {
        return err;
    }
    return temperature_sensor_enable(temperature_sensor);
}

static esp_err_t read_temperature_c(float *celsius)
{
    return temperature_sensor_get_celsius(temperature_sensor, celsius);
}

static void encode_temperature(float celsius, uint8_t payload[TEMPERATURE_PAYLOAD_SIZE])
{
    int16_t temperature_tenths_c = (int16_t)lroundf(celsius * 10.0f);

    payload[0] = (uint8_t)(temperature_tenths_c >> 8);
    payload[1] = (uint8_t)(temperature_tenths_c & 0xff);
}

/* Read and send a temperature report from the transport task.
 * Return CHIRP_SKIPPED on sensor failure so the connection stays open. */
static chirp_send_result_t report_temperature(void)
{
    float celsius = 0.0f;

    esp_err_t err = read_temperature_c(&celsius);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "temperature read: %s", esp_err_to_name(err));
        return CHIRP_SKIPPED;
    }

    uint8_t payload[TEMPERATURE_PAYLOAD_SIZE];
    encode_temperature(celsius, payload);

    ESP_LOGI(TAG, "%.1f C", celsius);
    return chirp_send_bytes(payload, sizeof(payload));
}

/* Called by chirp_send with one downlink frame. `led:on`, `led:off` and
 * `led:blink` move the board's LED; anything else is logged and ignored. */
static void handle_command(const char *command)
{
    if (!chirp_led_command(command)) {
        ESP_LOGI(TAG, "downlink ignored: %s", command);
    }
}

/* ---------------------------------------------------------------------------
 * start-up
 * ------------------------------------------------------------------------ */

/* Line-based stdin needs the USB-Serial-JTAG driver behind the VFS; the default
 * polling console never blocks. Line endings stay raw: the parser strips CR itself. */
static void console_init(void)
{
    usb_serial_jtag_driver_config_t console_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    console_config.rx_buffer_size = 1024;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&console_config));

    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_LF);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
    usb_serial_jtag_vfs_use_driver();

    /* Blocking, unbuffered stdin, so the reader task sleeps in fgets(). */
    fcntl(fileno(stdin), F_SETFL, 0);
    fcntl(fileno(stdout), F_SETFL, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
}

void app_main(void)
{
    ESP_ERROR_CHECK(chirp_store_init());
    console_init();
    ESP_ERROR_CHECK(chirp_led_start());

    chirp_config_t config;
    ESP_ERROR_CHECK(chirp_store_load(&config));
    ESP_LOGI(TAG, "hwid=%s host=%s networks=%u credential=%s signing-key=%s",
             chirp_store_hwid(&config), config.host, config.ap_count,
             config.cred_kind == CHIRP_CRED_CLAIM ? "claim" :
             config.cred_kind == CHIRP_CRED_TOKEN ? "token" : "none",
             config.has_key ? "yes" : "no");

    /* Always listening, provisioned or not. */
    ESP_ERROR_CHECK(chirp_prov_start(&config));

    if (config.ap_count == 0) {
        ESP_LOGI(TAG, "no networks stored, waiting to be provisioned");
        return;
    }
    ESP_ERROR_CHECK(wifi_ladder_start(&config));

    /* Without a credential, the transport uses the open-device marker. */
    if (config.cred_kind == CHIRP_CRED_NONE) {
        ESP_LOGI(TAG, "no claim code or token stored, reporting as open (L0)");
    }

    ESP_ERROR_CHECK(temperature_sensor_start());
    ESP_ERROR_CHECK(chirp_send_start(&config, report_temperature, handle_command));
}
