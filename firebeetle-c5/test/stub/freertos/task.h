#pragma once
void vTaskDelay(int ticks);
void vTaskDelete(void *task);
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, int stack, void *arg, int prio, void *handle);
