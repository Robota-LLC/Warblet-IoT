/* Host stand-in (test only): printf() is the test's already. */
#pragma once
#include "esp_vfs_common.h"
static inline void usb_serial_jtag_vfs_set_rx_line_endings(esp_line_endings_t mode) { (void)mode; }
static inline void usb_serial_jtag_vfs_set_tx_line_endings(esp_line_endings_t mode) { (void)mode; }
static inline void usb_serial_jtag_vfs_use_driver(void) {}
