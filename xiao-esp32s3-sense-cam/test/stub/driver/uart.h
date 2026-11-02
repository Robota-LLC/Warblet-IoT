/* Host stand-in (test only): the test's console wire answers the reads. */
#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#define CONFIG_ESP_CONSOLE_UART_NUM 0  /* sdkconfig's, as the demos leave it */
typedef int uart_port_t;
esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size, int tx_buffer_size, int queue_size,
                              void *uart_queue, int intr_alloc_flags);
int uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length, TickType_t ticks_to_wait);
