/* Host stand-in (test only): enough for a main.c to compile and start; no task
 * that uses a queue runs. */
#pragma once
#include <stddef.h>
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
static inline QueueHandle_t xQueueCreate(int length, size_t size) { (void)length; (void)size; return (QueueHandle_t)1; }
static inline BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t t)
{ (void)q; (void)item; (void)t; return pdTRUE; }
static inline BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t t)
{ (void)q; (void)item; (void)t; return pdFALSE; }
