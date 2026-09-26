/* Receive USART1 bytes with GPDMA1 channel 7.
 * Interrupt callbacks copy bytes to a ring; the scheduler assembles complete lines.
 * Dispatch CHIRP lines to provisioning and other lines to OpenThread CLI. */

#include <stdarg.h>
#include <stdio.h>

#include "board_serial.h"

#include "stm32wbaxx.h"
#include "stm32wbaxx_hal.h"

#include "app_conf.h"       /* CFG_TASK_NBR, CFG_SEQ_PRIO_1 */
#include "stm32_seq.h"

#include "platform_wba.h"   /* arcUartProcess, LL_LOCK / LL_UNLOCK */

/* ST's uart.c globals: the CLI output sink and UART handle. Not in any ST header. */
extern int                 CliUartOutput(void *aContext, const char *aFormat, va_list aArguments);
extern UART_HandleTypeDef *oTCLIuart;

/* --wrap partner, provided by the linker (see the Makefile's WRAPS). */
void __real_otPlatUartReceived(const uint8_t *aBuf, uint16_t aBufLength);

/* CliUartOutput() passes BUFFER_SIZE, not the remaining space, to vsnprintf, so
 * a long echo would overrun ST's 1536-byte buffer. 256 matches ST's own echo. */
#define ECHO_MAX 256u

/* Nothing drains ST's 1536-byte CLI buffers inside a task, so one print is
 * capped and the buffer is handed to the UART once OUT_BUDGET is queued. */
#define OUT_LINE_MAX 256u
#define OUT_BUDGET   1024u

/* Interrupt-to-task ring; it covers one scheduler round trip, not a whole line. */
#define RX_RING 1024u

/* Scheduler task id above ST's CFG_TASK_NBR. chirp_prov.c owns 30 and 31,
 * chirp_report.c owns 28. */
#define SERIAL_TASK_RX_ID 29u
#define SERIAL_TASK_RX    (1u << SERIAL_TASK_RX_ID)

_Static_assert(SERIAL_TASK_RX_ID >= (unsigned)CFG_TASK_NBR,
               "serial task id overlaps ST's CFG_Task_Id_t");
_Static_assert(SERIAL_TASK_RX_ID < 32u, "sequencer task ids are 0..31");

/* Who gets a finished CHIRP line. Set by board_serial_begin(). */
static void (*s_on_chirp_line)(char *line, unsigned len);

/* The line under assembly: +2 to restore the CRLF for ot-cli, +1 for the NUL. */
static char        s_line[BOARD_SERIAL_LINE_MAX + 3u];
static unsigned    s_line_len;
static const char *s_line_bad;      /* NULL, or why this line is a write-off */

/* Interrupt -> task ring. head is interrupt-owned, tail is task-owned. */
static volatile uint8_t  s_ring[RX_RING];
static volatile uint32_t s_ring_head;
static volatile uint32_t s_ring_tail;
static volatile uint8_t  s_ring_lost;

/* DMA reception buffer; the callbacks copy each new region into the ring.
 * 512 bytes is ~44 ms at 115200. */
static DMA_HandleTypeDef s_rx_dma;
static uint8_t           s_dma_buf[512];
static volatile uint16_t s_dma_last;   /* end of the last region copied out */

/* Bytes we have queued into ST's output buffer since the last hand-off. */
static unsigned s_out_pending;

/* ------------------------------------------------------------------ output */

static void cli_out(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  (void)CliUartOutput(NULL, fmt, ap);
  va_end(ap);
}

/* Hand ST's filled buffer to the UART. Skipped before the UART exists, where
 * ST's wait would never end. */
static void out_handoff(void)
{
  if (oTCLIuart != NULL && oTCLIuart->gState != HAL_UART_STATE_RESET) {
    arcUartProcess();
    s_out_pending = 0u;
  }
}

void board_serial_print(const char *fmt, ...)
{
  char    text[OUT_LINE_MAX];
  va_list ap;
  int     n;

  va_start(ap, fmt);
  n = vsnprintf(text, sizeof text, fmt, ap);
  va_end(ap);
  if (n <= 0) { return; }
  if ((unsigned)n >= sizeof text) { n = (int)sizeof text - 1; }   /* truncated */

  if (s_out_pending + (unsigned)n > OUT_BUDGET) { out_handoff(); }
  cli_out("%s", text);
  s_out_pending += (unsigned)n;
}

/* The CLI's own task cannot drain its buffer while we are in a task: push by
 * hand and wait for idle. */
static void flush_and_wait(void)
{
  arcUartProcess();
  while (oTCLIuart != NULL && oTCLIuart->gState != HAL_UART_STATE_READY) { }
}

void board_serial_drain(void)
{
  arcUartProcess();

  /* Allow queued console output to drain before entering the bootloader. */
  for (volatile unsigned i = 0; i < 4000000u; i++) { __NOP(); }
}

/* ------------------------------------------------------------------ routing */

/* A line is the responder's iff it begins "CHIRP"; all else goes to the CLI. */
static int is_chirp_line(const char *s, unsigned n)
{
  return n >= 5u && s[0] == 'C' && s[1] == 'H' && s[2] == 'I' &&
         s[3] == 'R' && s[4] == 'P';
}

static int starts_with(const char *s, const char *prefix)
{
  while (*prefix) { if (*s++ != *prefix++) { return 0; } }
  return 1;
}

/* Hand a non-CHIRP line to ot-cli as ST's own path would: echoed, CRLF restored, one call. */
static void forward_to_cli(unsigned n)
{
  unsigned echo = (n > ECHO_MAX) ? ECHO_MAX : n;

  board_serial_print("%.*s\r\n", (int)echo, s_line);

  /* ST flushes before these two so the echo lands before the stack resets. */
  if (starts_with(s_line, "reset") || starts_with(s_line, "factoryreset")) {
    flush_and_wait();
  }

  s_line[n]      = '\r';
  s_line[n + 1u] = '\n';
  out_handoff();                        /* ot-cli's reply starts in an empty buffer */
  __real_otPlatUartReceived((const uint8_t *)s_line, (uint16_t)(n + 2u));
}

/* A terminator arrived. Dispatch the whole line, exactly once, and reset. */
static void dispatch_line(void)
{
  unsigned    n   = s_line_len;
  const char *bad = s_line_bad;

  s_line_len = 0u;
  s_line_bad = NULL;

  if (bad != NULL) {
    /* Report a damaged command instead of dispatching it. */
    if (is_chirp_line(s_line, n)) { board_serial_print("CHIRP= err %s\r\n", bad); }
    else { board_serial_print("CHIRP. input line dropped (%s)\r\n", bad); }
    return;
  }

  /* Empty line: a bare CR, a bare LF, or the LF half of a CRLF. */
  if (n == 0u) { return; }

  s_line[n] = '\0';

  /* Hold the recursive link-layer mutex while handing a line to OpenThread CLI. */
  LL_LOCK();
  if (is_chirp_line(s_line, n)) {
    if (s_on_chirp_line != NULL) { s_on_chirp_line(s_line, n); }
  } else {
    forward_to_cli(n);
  }
  LL_UNLOCK();
}

/* ------------------------------------------------------------------ receive */

/* Scheduler task: drain the ring, dispatching on each terminator, so half a
 * line never reaches either speaker. */
static void rx_task(void)
{
  if (s_ring_lost) {
    s_ring_lost = 0u;
    s_line_bad  = "overrun";            /* this line is a write-off; say so */
  }

  while (s_ring_tail != s_ring_head) {
    uint8_t  c    = s_ring[s_ring_tail];
    uint32_t next = s_ring_tail + 1u;

    s_ring_tail = (next == RX_RING) ? 0u : next;

    if (c == '\r' || c == '\n') { dispatch_line(); continue; }

    if (s_line_len < BOARD_SERIAL_LINE_MAX) { s_line[s_line_len++] = (char)c; }
    else if (s_line_bad == NULL)            { s_line_bad = "too-long"; }
  }
}

/* Interrupt-side ring push, shared by the two DMA callbacks below. */
static void ring_push(uint8_t c)
{
  uint32_t head = s_ring_head;
  uint32_t next = head + 1u;

  if (next == RX_RING) { next = 0u; }

  if (next != s_ring_tail) { s_ring[head] = c; s_ring_head = next; }
  else                     { s_ring_lost = 1u; }
}

/* Copy received DMA bytes to the ring, wake the task, and re-arm completed transfers. */
static void rx_event(UART_HandleTypeDef *huart, uint16_t pos)
{
  uint16_t i;

  for (i = s_dma_last; i < pos; i++) { ring_push(s_dma_buf[i]); }
  s_dma_last = pos;

  if (huart->RxState == HAL_UART_STATE_READY) {
    s_dma_last = 0u;
    (void)HAL_UARTEx_ReceiveToIdle_DMA(huart, s_dma_buf, sizeof s_dma_buf);
  }

  UTIL_SEQ_SetTask(SERIAL_TASK_RX, CFG_SEQ_PRIO_1);
}

/* Mark a UART-damaged line for rejection, then restart reception. */
static void rx_error(UART_HandleTypeDef *huart)
{
  s_ring_lost = 1u;

  if (huart->RxState == HAL_UART_STATE_READY) {
    s_dma_last = 0u;
    (void)HAL_UARTEx_ReceiveToIdle_DMA(huart, s_dma_buf, sizeof s_dma_buf);
  }

  UTIL_SEQ_SetTask(SERIAL_TASK_RX, CFG_SEQ_PRIO_1);
}

/* ST defines this vector weakly; channel 7 is ours (ST uses 0..2). */
void GPDMA1_Channel7_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&s_rx_dma);
}

/* ------------------------------------------------------------------ start */

void board_serial_begin(void (*on_chirp_line)(char *line, unsigned len))
{
  s_on_chirp_line = on_chirp_line;

  if (oTCLIuart == NULL) { return; }

  s_ring_head = 0u;
  s_ring_tail = 0u;
  s_ring_lost = 0u;
  s_line_len  = 0u;
  s_line_bad  = NULL;

  /* Replace ST's 1-byte interrupt receive with DMA, which is immune to
   * radio-interrupt latency. */
  (void)HAL_UART_AbortReceive(oTCLIuart);

  /* Mirrors ST's USART1_TX channel setup in stm32wbaxx_hal_msp.c, direction reversed. */
  s_rx_dma.Instance                   = GPDMA1_Channel7;
  s_rx_dma.Init.Request               = GPDMA1_REQUEST_USART1_RX;
  s_rx_dma.Init.BlkHWRequest          = DMA_BREQ_SINGLE_BURST;
  s_rx_dma.Init.Direction             = DMA_PERIPH_TO_MEMORY;
  s_rx_dma.Init.SrcInc                = DMA_SINC_FIXED;
  s_rx_dma.Init.DestInc               = DMA_DINC_INCREMENTED;
  s_rx_dma.Init.SrcDataWidth          = DMA_SRC_DATAWIDTH_BYTE;
  s_rx_dma.Init.DestDataWidth         = DMA_DEST_DATAWIDTH_BYTE;
  s_rx_dma.Init.Priority              = DMA_LOW_PRIORITY_HIGH_WEIGHT;
  s_rx_dma.Init.SrcBurstLength        = 1;
  s_rx_dma.Init.DestBurstLength       = 1;
  s_rx_dma.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
  s_rx_dma.Init.TransferEventMode     = DMA_TCEM_BLOCK_TRANSFER;
  s_rx_dma.Init.Mode                  = DMA_NORMAL;

  UTIL_SEQ_RegTask(SERIAL_TASK_RX, UTIL_SEQ_RFU, rx_task);

  if (HAL_DMA_Init(&s_rx_dma) != HAL_OK ||
      HAL_DMA_ConfigChannelAttributes(&s_rx_dma, DMA_CHANNEL_NPRIV) != HAL_OK) {
    /* Fail loud: a quietly lossy console is the failure this file exists to prevent. */
    board_serial_print("CHIRP. rx dma init failed; serial input is dead\r\n");
    return;
  }

  __HAL_LINKDMA(oTCLIuart, hdmarx, s_rx_dma);
  oTCLIuart->RxEventCallback = rx_event;
  oTCLIuart->ErrorCallback   = rx_error;
  HAL_NVIC_SetPriority(GPDMA1_Channel7_IRQn, 7, 0);
  HAL_NVIC_EnableIRQ(GPDMA1_Channel7_IRQn);
  s_dma_last = 0u;

  if (HAL_UARTEx_ReceiveToIdle_DMA(oTCLIuart, s_dma_buf, sizeof s_dma_buf) != HAL_OK) {
    board_serial_print("CHIRP. rx dma start failed; serial input is dead\r\n");
  }
}

/* ------------------------------------------------------------------ backstop */

/* Second line of defence: if anything routes bytes back through
 * otPlatUartReceived, a CHIRP line still must not reach ot-cli. */
void __wrap_otPlatUartReceived(const uint8_t *aBuf, uint16_t aBufLength)
{
  uint16_t i;

  if (!is_chirp_line((const char *)aBuf, aBufLength)) {
    __real_otPlatUartReceived(aBuf, aBufLength);
    return;
  }

  /* Not our own task, so the assembler is idle; start clean. */
  s_line_len = 0u;
  s_line_bad = NULL;

  for (i = 0; i < aBufLength; i++) {
    uint8_t c = aBuf[i];
    if (c == '\r' || c == '\n') { dispatch_line(); continue; }
    if (s_line_len < BOARD_SERIAL_LINE_MAX) { s_line[s_line_len++] = (char)c; }
    else if (s_line_bad == NULL)            { s_line_bad = "too-long"; }
  }
  dispatch_line();
}
