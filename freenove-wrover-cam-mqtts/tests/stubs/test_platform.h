#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 258
#define ESP_ERR_TIMEOUT 263
#define ESP_ERR_NO_MEM 257
#define ESP_ERROR_CHECK(e) assert((e) == ESP_OK)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define CONFIG_ESP_CONSOLE_UART_NUM 0
#define ESP_LINE_ENDINGS_LF 0
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define tskIDLE_PRIORITY 0
typedef uint32_t TickType_t;
typedef struct fake_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned count, unsigned size);
int xQueueReceive(QueueHandle_t queue, void *item, unsigned ticks);
int xQueueSend(QueueHandle_t queue, const void *item, unsigned ticks);
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle, int core);
int64_t esp_timer_get_time(void);
int uart_driver_install(int port, int rx, int tx, int count, void *q, int flags);
void uart_vfs_dev_port_set_rx_line_endings(int port, int endings);
void uart_vfs_dev_port_set_tx_line_endings(int port, int endings);
void uart_vfs_dev_use_driver(int port);
