/* Photo doorbell: a button press, a `snap` command or the heartbeat timer
 * captures a JPEG and publishes it over MQTTS, tagged with what caused it.
 * The camera module handles sensor setup and buffer ownership. */
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "camera.h"
#include "chirp_mqtt.h"
#include "chirp_prov.h"
#include "chirp_store.h"
#include "wifi_ladder.h"

#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "warblet";

/* ---------------------------------------------------------------------------
 * the button
 * ------------------------------------------------------------------------ */

/* Two inputs, and either one takes the photo: the board's own BOOT button, or
 * a switch wired from header pin D0 to GND (a doorbell push button, or a door
 * contact). Both read low while pressed. GPIO0 is a strapping pin, which only matters at reset:
 * holding BOOT while the board starts puts it in download mode. GPIO1 is not
 * a strapping pin. */
#define BUTTON_BOOT_GPIO GPIO_NUM_0
#define BUTTON_D0_GPIO   GPIO_NUM_1

/* Read every 10 ms; a change counts once it has held for 50 ms (debounce). */
#define BUTTON_POLL_MS   10
#define BUTTON_STEADY_MS 50

/* At most one press photo every 30 s, so a rattling contact cannot fill the
 * device's daily storage. A press inside the gap is logged and ignored. */
#define PRESS_GAP_MS 30000

/* One input: its pin, its debounced state, and how long the pin has read
 * differently from that state. */
typedef struct {
    gpio_num_t gpio;
    bool       down;
    int        differing_ms;
} button_t;

/* Each input is debounced on its own, so a door contact held closed on D0
 * does not hide a press of BOOT. */
static button_t s_buttons[] = {
    { .gpio = BUTTON_BOOT_GPIO },
    { .gpio = BUTTON_D0_GPIO },
};
#define BUTTON_COUNT (sizeof(s_buttons) / sizeof(s_buttons[0]))

/* True at the moment this input goes down, once the change has held. */
static bool button_went_down(button_t *b)
{
    bool down = gpio_get_level(b->gpio) == 0;
    if (down == b->down) {
        b->differing_ms = 0;
        return false;
    }
    b->differing_ms += BUTTON_POLL_MS;
    if (b->differing_ms < BUTTON_STEADY_MS) {
        return false;
    }
    b->down         = down;
    b->differing_ms = 0;
    return down;
}

/* One press = one photo: only the moment an input goes down counts, so
 * holding it does nothing more, and neither does letting go. */
static void button_task(void *arg)
{
    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        /* a contact already closed at start-up is not a press */
        s_buttons[i].down = gpio_get_level(s_buttons[i].gpio) == 0;
    }
    int64_t last_press_ms = -PRESS_GAP_MS;  /* so the first press always counts */

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
        for (size_t i = 0; i < BUTTON_COUNT; i++) {
            if (!button_went_down(&s_buttons[i])) {
                continue;
            }
            int64_t now_ms = esp_timer_get_time() / 1000;
            if (now_ms - last_press_ms < PRESS_GAP_MS) {
                ESP_LOGI(TAG, "press on GPIO%d ignored: %" PRId64 " ms after the "
                              "last photo, less than %d ms", s_buttons[i].gpio,
                         now_ms - last_press_ms, PRESS_GAP_MS);
                continue;
            }
            last_press_ms = now_ms;
            ESP_LOGI(TAG, "button pressed (GPIO%d) -- capturing now", s_buttons[i].gpio);
            chirp_mqtt_request(CHIRP_TAG_PRESS);
        }
    }
}

static esp_err_t button_start(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BUTTON_BOOT_GPIO) | (1ULL << BUTTON_D0_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,  /* an unwired D0 reads high */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreate(button_task, "button", 3072, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

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

/* The S3's own USB port is the console: there is no bridge chip. Line-based
 * stdin needs the USB-Serial-JTAG driver behind the VFS; the default polling
 * console never blocks. Line endings stay raw: the parser strips CR itself. */
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

    chirp_config_t config;
    ESP_ERROR_CHECK(chirp_store_load(&config));
    ESP_LOGI(TAG, "hwid=%s host=%s networks=%u credential=%s signing-key=%s",
             chirp_store_hwid(&config), config.host, config.ap_count,
             config.cred_kind == CHIRP_CRED_CLAIM ? "claim" :
             config.cred_kind == CHIRP_CRED_TOKEN ? "token" : "none",
             config.has_key ? "yes" : "no");

    /* Always listening, provisioned or not. */
    ESP_ERROR_CHECK(chirp_prov_start(&config));

    /* Camera before network. With no camera there is nothing to send, but a
     * restart loop would also stop the board being set up over USB: so stay
     * up, send nothing, and say why once a minute. */
    if (camera_init() != ESP_OK) {
        for (;;) {
            ESP_LOGE(TAG, "no camera: unplug the board, check the camera board "
                          "sits fully on its connector, then plug it back in");
            vTaskDelay(pdMS_TO_TICKS(60000));
        }
    }

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

    /* After the session exists, so a press always has somewhere to go. */
    ESP_ERROR_CHECK(button_start());
}
