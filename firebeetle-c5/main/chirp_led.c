#include <string.h>

#include "chirp_led.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "chirp_led";

/* GPIO15 (D13) drives the active-high LED through a 4.7 kOhm resistor.
 * It also drives the GDI backlight and may be reserved for PSRAM on other modules.
 * Check the board schematic when porting: https://wiki.dfrobot.com/dfr1222/ */
#define CHIRP_LED_GPIO 15

_Static_assert(GPIO_IS_VALID_OUTPUT_GPIO(CHIRP_LED_GPIO),
               "CHIRP_LED_GPIO is not an output-capable pin on this target");

#define BLINK_HALF_PERIOD_MS 400

typedef enum { LED_OFF = 0, LED_ON, LED_BLINK } led_mode_t;

static volatile led_mode_t s_mode = LED_OFF;
static TaskHandle_t        s_task;

/* Steady states park on a notification and cost nothing; blinking wakes on the
 * timeout instead. One task, no polling loop. */
static void led_task(void *arg)
{
    bool phase = false;
    for (;;) {
        led_mode_t mode = s_mode;
        TickType_t wait;
        if (mode == LED_BLINK) {
            phase = !phase;
            wait = pdMS_TO_TICKS(BLINK_HALF_PERIOD_MS);
        } else {
            phase = (mode == LED_ON);
            wait = portMAX_DELAY;
        }
        gpio_set_level(CHIRP_LED_GPIO, phase ? 1 : 0);
        ulTaskNotifyTake(pdTRUE, wait);
    }
}

esp_err_t chirp_led_start(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CHIRP_LED_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreate(led_task, "chirp_led", 2048, NULL, 4, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool chirp_led_command(const char *text)
{
    led_mode_t mode;

    if (text == NULL) {
        return false;
    }
    if (strcmp(text, "led:on") == 0) {
        mode = LED_ON;
    } else if (strcmp(text, "led:off") == 0) {
        mode = LED_OFF;
    } else if (strcmp(text, "led:blink") == 0) {
        mode = LED_BLINK;
    } else {
        return false;
    }

    s_mode = mode;
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
    ESP_LOGI(TAG, "%s", text);
    return true;
}
