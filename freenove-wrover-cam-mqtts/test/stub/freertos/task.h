/* Host stand-in (test only): no task runs; a delay is nothing. */
#pragma once
#include "FreeRTOS.h"
static inline void vTaskDelay(TickType_t t) { (void)t; }
static inline void vTaskDelete(void *t) { (void)t; }
static inline BaseType_t xTaskCreate(void (*fn)(void *), const char *name, int stack, void *arg, int prio,
                                     void *handle)
{ (void)fn; (void)name; (void)stack; (void)arg; (void)prio; (void)handle; return pdPASS; }
