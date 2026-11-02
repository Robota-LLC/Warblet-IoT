/* The HAL's UART as echotest models it (test only): the 1-byte interrupt
 * receive ST's uart.c arms, the DMA receive board_serial.c starts, a transmit
 * that finishes at once, and the line between them. In its own file so that
 * echotest.c's calls to HAL_UART_Receive_IT() are calls from outside, which
 * the linker's --wrap sends to board_serial.c, as it does ST's. */
#pragma once
#include "stm32wbaxx_hal.h"

/* Bytes arriving on the line, one interrupt each: to the 1-byte receive if it
 * is armed (which calls the callback ST registered), else into the DMA
 * receive if it runs (an idle event at the end), else nowhere. */
void wire_in(UART_HandleTypeDef *h, const char *bytes, unsigned n);

/* The HAL's 1-byte receive armed directly, as ST's call would arm it with no
 * wrap in the way (the control). */
void hal_arm_it(UART_HandleTypeDef *h, uint8_t *buf);
int  hal_it_armed(const UART_HandleTypeDef *h);
int  hal_dma_running(const UART_HandleTypeDef *h);

extern char     wire_out[8192];   /* what the board sent to the console */
extern unsigned wire_out_len;
extern unsigned it_bytes;         /* bytes the 1-byte receive handed to a callback */
extern unsigned lost_bytes;       /* bytes no receive was armed for */
