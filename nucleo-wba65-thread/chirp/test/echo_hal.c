/* The HAL's UART for echotest: see echo_hal.h. */
#include <string.h>

#include "echo_hal.h"

char     wire_out[8192];
unsigned wire_out_len;
unsigned it_bytes;
unsigned lost_bytes;

/* One receive of each kind at a time is enough for the test. */
static UART_HandleTypeDef *it_uart;
static uint8_t            *it_buf;
static UART_HandleTypeDef *dma_uart;
static uint8_t            *dma_buf;
static uint16_t            dma_size, dma_pos;

unsigned        flush_leaves;
static char     waiting[256];
static unsigned waiting_n;

void uart_holds(const char *bytes, unsigned n)
{
  for (unsigned i = 0u; i < n && waiting_n < sizeof waiting; i++) { waiting[waiting_n++] = bytes[i]; }
}

unsigned uart_waiting_bytes(void) { return waiting_n; }

void hal_uart_reset(UART_HandleTypeDef *h)
{
  it_uart = NULL;
  if (dma_uart == h) { dma_uart = NULL; }
  h->RxState = HAL_UART_STATE_READY;
  waiting_n = 0u;
}

void hal_dma_takes_waiting(UART_HandleTypeDef *h)
{
  if (dma_uart != h || waiting_n == 0u) { return; }
  if (waiting_n >= 2u && h->ErrorCallback != NULL) {
    h->ErrorCallback(h);                              /* an overrun: only the last byte is kept */
    waiting[0] = waiting[waiting_n - 1u];
    waiting_n = 1u;
  }
  for (unsigned i = 0u; i < waiting_n && dma_pos < dma_size; i++) { dma_buf[dma_pos++] = (uint8_t)waiting[i]; }
  waiting_n = 0u;
  if (h->RxEventCallback != NULL) { h->RxEventCallback(h, dma_pos); }
}

void hal_arm_it(UART_HandleTypeDef *h, uint8_t *buf)
{
  it_uart    = h;
  it_buf     = buf;
  h->RxState = HAL_UART_STATE_BUSY_RX;
}

int hal_it_armed(const UART_HandleTypeDef *h) { return it_uart == h; }
int hal_dma_running(const UART_HandleTypeDef *h) { return dma_uart == h; }

HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *h, uint8_t *buf, uint16_t size)
{
  (void)size;
  if (h->RxState != HAL_UART_STATE_READY) { return HAL_BUSY; }
  hal_arm_it(h, buf);
  return HAL_OK;
}

HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *h, uint8_t *buf, uint16_t size)
{
  if (h->RxState != HAL_UART_STATE_READY) { return HAL_BUSY; }
  dma_uart   = h;
  dma_buf    = buf;
  dma_size   = size;
  dma_pos    = 0u;
  h->RxState = HAL_UART_STATE_BUSY_RX;
  return HAL_OK;
}

/* Both kinds stop, the error flags are cleared, and what waited in the UART
 * is flushed (RXFRQ), all but a byte still being shifted in. */
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *h)
{
  if (it_uart == h)  { it_uart = NULL; }
  if (dma_uart == h) { dma_uart = NULL; }
  h->RxState = HAL_UART_STATE_READY;
  if (waiting_n > flush_leaves) {
    memmove(waiting, waiting + waiting_n - flush_leaves, flush_leaves);
    waiting_n = flush_leaves;
  }
  return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *h, const uint8_t *buf, uint16_t size)
{
  if (wire_out_len + size < sizeof wire_out) {
    memcpy(wire_out + wire_out_len, buf, size);
    wire_out_len += size;
    wire_out[wire_out_len] = '\0';
  }
  h->gState = HAL_UART_STATE_READY;
  if (h->TxCpltCallback != NULL) { h->TxCpltCallback(h); }
  return HAL_OK;
}

void wire_in(UART_HandleTypeDef *h, const char *bytes, unsigned n)
{
  int dma_got = 0;

  for (unsigned i = 0u; i < n; i++) {
    if (it_uart == h) {
      /* The HAL's receive-complete path: the byte in place, the receive
       * over, then the callback (which may arm the next one). */
      *it_buf    = (uint8_t)bytes[i];
      it_uart    = NULL;
      h->RxState = HAL_UART_STATE_READY;
      it_bytes++;
      if (h->RxCpltCallback != NULL) { h->RxCpltCallback(h); }
    } else if (dma_uart == h && dma_pos < dma_size) {
      dma_buf[dma_pos++] = (uint8_t)bytes[i];
      dma_got = 1;
    } else {
      lost_bytes++;
      uart_holds(&bytes[i], 1u);      /* in the UART, unread */
    }
  }
  if (dma_got && dma_uart == h && h->RxEventCallback != NULL) {
    if (dma_pos == dma_size) { dma_uart = NULL; h->RxState = HAL_UART_STATE_READY; }   /* the buffer is full */
    h->RxEventCallback(h, dma_pos);
  }
}
