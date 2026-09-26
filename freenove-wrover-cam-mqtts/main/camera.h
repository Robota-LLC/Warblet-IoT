/* Camera setup, capture, and release interface.
 * Check the pin map, sensor support, and PSRAM settings when porting. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Brings up the sensor: QVGA, JPEG, one frame buffer in PSRAM. Falls back to
 * encoding the JPEG in software on a sensor that has no encoder of its own. */
esp_err_t camera_init(void);

/* Return a JPEG pointer and length. Call camera_release() after using the bytes. */
esp_err_t camera_capture(const uint8_t **out, size_t *out_len);

/* Returns the frame buffer to the driver. Safe to call with nothing held. */
void camera_release(void);

#ifdef __cplusplus
}
#endif
