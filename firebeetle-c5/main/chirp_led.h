/* Onboard LED control. Check GPIO and module wiring when porting. */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Claims the LED pin and starts the blink task. */
esp_err_t chirp_led_start(void);

/* Acts on one downlink frame. Returns false for anything that is not one of
 * this board's commands. */
bool chirp_led_command(const char *text);

#ifdef __cplusplus
}
#endif
