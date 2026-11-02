/* Host stand-in (test only): printf() is the test's already. */
#pragma once
#include "esp_vfs_common.h"
static inline int uart_vfs_dev_port_set_rx_line_endings(int uart_num, esp_line_endings_t mode)
{ (void)uart_num; (void)mode; return 0; }
static inline int uart_vfs_dev_port_set_tx_line_endings(int uart_num, esp_line_endings_t mode)
{ (void)uart_num; (void)mode; return 0; }
static inline void uart_vfs_dev_use_driver(int uart_num) { (void)uart_num; }
