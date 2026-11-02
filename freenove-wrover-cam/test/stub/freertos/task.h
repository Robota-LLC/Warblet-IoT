/* Host stand-in (test only): no task runs. The test records each task a start
 * creates, and a delay moves the test's clock on. */
#pragma once
#include "FreeRTOS.h"
#define tskIDLE_PRIORITY 0
void       vTaskDelay(TickType_t ticks);
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, int stack, void *arg, int prio, void *handle);
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, int stack, void *arg, int prio,
                                   void *handle, int core);
static inline void vTaskDelete(void *t) { (void)t; }
#define taskENTER_CRITICAL(lock) ((void)(lock))
#define taskEXIT_CRITICAL(lock)  ((void)(lock))
