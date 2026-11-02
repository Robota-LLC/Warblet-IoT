/* Host stand-in (test only): a sensor that reads 21.5 C. */
#pragma once
#include "esp_err.h"
typedef struct temperature_sensor_obj_t *temperature_sensor_handle_t;
typedef struct {
    int range_min;
    int range_max;
} temperature_sensor_config_t;
#define TEMPERATURE_SENSOR_CONFIG_DEFAULT(min, max) { .range_min = (min), .range_max = (max) }
static inline esp_err_t temperature_sensor_install(const temperature_sensor_config_t *c,
                                                   temperature_sensor_handle_t *h)
{ (void)c; *h = 0; return ESP_OK; }
static inline esp_err_t temperature_sensor_enable(temperature_sensor_handle_t h) { (void)h; return ESP_OK; }
static inline esp_err_t temperature_sensor_get_celsius(temperature_sensor_handle_t h, float *c)
{ (void)h; *c = 21.5f; return ESP_OK; }
