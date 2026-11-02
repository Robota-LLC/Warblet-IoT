/* Host stand-in (test only): two inputs that read high (not pressed). */
#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef enum { GPIO_NUM_0 = 0, GPIO_NUM_1 = 1 } gpio_num_t;
typedef enum { GPIO_MODE_INPUT = 1 } gpio_mode_t;
typedef enum { GPIO_PULLUP_DISABLE, GPIO_PULLUP_ENABLE } gpio_pullup_t;
typedef enum { GPIO_PULLDOWN_DISABLE, GPIO_PULLDOWN_ENABLE } gpio_pulldown_t;
typedef enum { GPIO_INTR_DISABLE } gpio_int_type_t;
typedef struct {
    uint64_t        pin_bit_mask;
    gpio_mode_t     mode;
    gpio_pullup_t   pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;
static inline esp_err_t gpio_config(const gpio_config_t *c) { (void)c; return ESP_OK; }
static inline int gpio_get_level(gpio_num_t n) { (void)n; return 1; }
