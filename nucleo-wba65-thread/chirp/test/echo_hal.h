/* The HAL's UART as echotest models it (test only), from the HAL's own source
 * (stm32wbaxx_hal_uart.c and _ex.c, as make fetch places them): the 1-byte
 * interrupt receive ST's uart.c arms, the DMA receive to idle board_serial.c
 * starts, the abort, the error interrupt, a transmit that finishes at once,
 * and under them the UART itself, its FIFO off as the firmware runs it: one
 * data register, sticky error flags and a byte being shifted in. In its own
 * file so that echotest.c's calls to HAL_UART_Receive_IT() are calls from
 * outside, which the linker's --wrap sends to board_serial.c, as it does
 * ST's. */
#pragma once
#include "stm32wbaxx_hal.h"

/* Bytes arriving back to back, then the line idle. Each lands in the data
 * register when it has been shifted in, after any byte already being shifted
 * in; if the register still holds a byte nobody read, the new one is lost
 * and the overrun flag is set. What is armed takes it: the DMA, or the 1-byte
 * receive (which calls the callback ST registered). An error flag with the
 * error interrupt on runs the HAL's error path; the line going idle runs its
 * idle path. wire_in_bad() lands byte `bad` (counted from 0) with a framing
 * error, as noise on the line would. */
void wire_in(UART_HandleTypeDef *h, const char *bytes, unsigned n);
void wire_in_bad(UART_HandleTypeDef *h, const char *bytes, unsigned n, unsigned bad);

/* A byte starts to arrive: it is being shifted in, not in the data register
 * yet, so an abort's flush does not reach it. wire_lands() lands it (as
 * above) and the line goes idle. */
void wire_starts(UART_HandleTypeDef *h, char byte);
void wire_lands(UART_HandleTypeDef *h);

/* The HAL's 1-byte receive armed directly, as ST's call would arm it with no
 * wrap in the way (the control). */
void hal_arm_it(UART_HandleTypeDef *h, uint8_t *buf);
int  hal_it_armed(const UART_HandleTypeDef *h);
int  hal_dma_running(const UART_HandleTypeDef *h);   /* the UART asks the DMA for each byte, and it runs */

/* HAL_DMA_Init() on the UART's receive channel disables the channel first. */
void hal_dma_channel_init(UART_HandleTypeDef *h);

/* The UART holds nothing: no byte in the data register, none being shifted
 * in, no error flag up. */
int  uart_holds_nothing(const UART_HandleTypeDef *h);

/* The UART as after a reset: no receive, nothing held, no flag, the HAL's
 * callbacks back to its own (which do nothing). */
void hal_uart_reset(UART_HandleTypeDef *h);

/* HAL_UART_AbortReceive() call number abort_fails from now (1: the next)
 * fails as the HAL's does when its DMA abort times out: HAL_TIMEOUT, with the
 * error flags, the data register and RxState left as they were. The HAL can
 * fail only while the DMA receive runs; the test forces it where it wants. */
extern unsigned abort_fails;
extern unsigned hal_dma_starts;   /* calls to HAL_UARTEx_ReceiveToIdle_DMA() */

extern char     wire_out[8192];   /* what the board sent to the console */
extern unsigned wire_out_len;
extern unsigned it_bytes;         /* bytes the 1-byte receive handed to a callback */
