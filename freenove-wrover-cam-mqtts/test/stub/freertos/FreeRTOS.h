/* Host stand-in (test only). */
#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int      BaseType_t;
#define pdPASS             1
#define pdTRUE             1
#define pdFALSE            0
#define portMAX_DELAY      0xffffffffu
#define pdMS_TO_TICKS(ms)  ((TickType_t)(ms))
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
