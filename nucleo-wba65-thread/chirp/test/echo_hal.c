/* The HAL's UART for echotest: see echo_hal.h. Each HAL call does what the
 * HAL's own does, in its order, as far as a test can see; the names in the
 * comments are the HAL's. */
#include <string.h>

#include "echo_hal.h"

char     wire_out[8192];
unsigned wire_out_len;
unsigned it_bytes;
unsigned abort_fails;
unsigned hal_dma_starts;

#define FLAG_ORE  0x1u   /* the sticky flags of ISR, cleared through ICR */
#define FLAG_FE   0x2u
#define FLAG_NE   0x4u
#define FLAG_IDLE 0x8u
#define ERRORS    (FLAG_ORE | FLAG_FE | FLAG_NE)

/* A UART and its HAL state: the handle carries gState, RxState and the
 * callbacks; the rest is the hardware's (registers) or the HAL's own. */
struct uart {
  UART_HandleTypeDef *h;
  int      rdr_full;              /* RXNE: the data register holds a byte */
  uint8_t  rdr;
  int      shifting;              /* a byte is being shifted in */
  uint8_t  shift;
  int      shift_fe;
  uint32_t isr;                   /* FLAG_*: set by the line, cleared through ICR */
  int      idle_armed;            /* a byte landed since the line was last idle */
  int      rxneie, eie, idleie;   /* CR1/CR3: the interrupts on */
  int      dmar;                  /* CR3.DMAR: the UART asks the DMA for each byte */
  int      chan;                  /* the DMA channel runs */
  int      toidle;                /* ReceptionType: HAL_UART_RECEPTION_TOIDLE */
  uint8_t *it_buf;                /* the 1-byte receive's buffer */
  uint8_t *dma_buf;
  uint16_t dma_size, dma_pos;
};

static struct uart uarts[2];

static struct uart *uart_of(const UART_HandleTypeDef *h)
{
  for (unsigned i = 0u; i < 2u; i++) {
    if (uarts[i].h == h) { return &uarts[i]; }
  }
  for (unsigned i = 0u; i < 2u; i++) {
    if (uarts[i].h == NULL) { uarts[i].h = (UART_HandleTypeDef *)h; return &uarts[i]; }
  }
  return &uarts[1];
}

static void error_callback(UART_HandleTypeDef *h)
{
  if (h->ErrorCallback != NULL) { h->ErrorCallback(h); }
}

static void event_callback(UART_HandleTypeDef *h, uint16_t pos)
{
  if (h->RxEventCallback != NULL) { h->RxEventCallback(h, pos); }
}

/* UART_EndRxTransfer(): the receive interrupts off, RxState ready. */
static void end_rx(struct uart *u)
{
  u->rxneie = 0;
  u->eie    = 0;
  if (u->toidle) { u->idleie = 0; }
  u->h->RxState = HAL_UART_STATE_READY;
  u->toidle     = 0;
  u->it_buf     = NULL;
}

/* UART_RxISR_8BIT(), a 1-byte receive: the byte in place, the receive over,
 * then the callback (which may arm the next one). */
static void rx_isr(struct uart *u)
{
  uint8_t *buf = u->it_buf;

  *buf        = u->rdr;
  u->rdr_full = 0;
  end_rx(u);
  it_bytes++;
  if (u->h->RxCpltCallback != NULL) { u->h->RxCpltCallback(u->h); }
}

/* HAL_UART_IRQHandler()'s error path. The flags whose interrupts are on are
 * cleared; a byte waiting for the 1-byte receive is read first. In DMA mode,
 * or on an overrun, the error is blocking: the receive is ended
 * (UART_EndRxTransfer()), the DMA request turned off and the channel
 * aborted, and only then is the error callback called (UART_DMAAbortOnError(),
 * at the end of the abort). Otherwise the callback is told and the receive
 * goes on. */
static void error_irq(struct uart *u)
{
  uint32_t errors = u->isr & ERRORS;

  u->isr &= ~ERRORS;
  if (u->rdr_full && u->rxneie) { rx_isr(u); }
  if (u->dmar || (errors & FLAG_ORE) != 0u) {
    end_rx(u);
    if (u->dmar) { u->dmar = 0; u->chan = 0; }
  }
  error_callback(u->h);
}

/* HAL_UART_IRQHandler()'s idle path, for a DMA receive to idle in normal
 * mode: with some of the buffer filled, the reception ends (error interrupt
 * and DMA request off, RxState ready, the channel aborted) and the event
 * callback hears how far it got. */
static void idle_irq(struct uart *u)
{
  u->isr &= ~FLAG_IDLE;
  if (u->dmar && u->dma_pos > 0u && u->dma_pos < u->dma_size) {
    u->eie        = 0;
    u->dmar       = 0;
    u->h->RxState = HAL_UART_STATE_READY;
    u->toidle     = 0;
    u->idleie     = 0;
    u->chan       = 0;
    event_callback(u->h, u->dma_pos);
  }
}

/* The DMA takes the byte in the data register. At half the buffer the event
 * callback hears of it (UART_DMARxHalfCplt()); with the buffer full the
 * reception ends, ready, and it hears of that (UART_DMAReceiveCplt()). */
static void dma_take(struct uart *u)
{
  u->dma_buf[u->dma_pos++] = u->rdr;
  u->rdr_full = 0;
  if (u->dma_pos == u->dma_size) {
    u->chan       = 0;
    u->eie        = 0;
    u->dmar       = 0;
    u->idleie     = 0;
    u->h->RxState = HAL_UART_STATE_READY;
    event_callback(u->h, u->dma_size);
  } else if (u->dma_pos == u->dma_size / 2u) {
    event_callback(u->h, u->dma_pos);
  }
}

/* Whatever is pending, as the DMA and the UART's interrupt take it: the DMA
 * reads a byte as soon as it lands, and the interrupt handler checks the
 * error flags before the received byte, and the idle line after both. */
static void service(struct uart *u)
{
  for (;;) {
    if (u->rdr_full && u->dmar && u->chan)                 { dma_take(u);  continue; }
    if ((u->isr & ERRORS) != 0u && (u->eie || u->rxneie))  { error_irq(u); continue; }
    if (u->rdr_full && u->rxneie)                          { rx_isr(u);    continue; }
    if ((u->isr & FLAG_IDLE) != 0u && u->idleie && u->toidle) { idle_irq(u); continue; }
    return;
  }
}

/* A byte has been shifted in: into the data register, or lost to an overrun
 * if the register still holds one. */
static void land(struct uart *u, uint8_t byte, int framing_error)
{
  if (u->rdr_full) {
    u->isr |= FLAG_ORE;
  } else {
    u->rdr      = byte;
    u->rdr_full = 1;
    if (framing_error) { u->isr |= FLAG_FE; }
  }
  u->idle_armed = 1;
  service(u);
}

static void land_shifting(struct uart *u)
{
  if (u->shifting) {
    u->shifting = 0;
    land(u, u->shift, u->shift_fe);
  }
}

static void line_idle(struct uart *u)
{
  if (u->idle_armed) {
    u->idle_armed = 0;
    u->isr |= FLAG_IDLE;
  }
  service(u);
}

void wire_in_bad(UART_HandleTypeDef *h, const char *bytes, unsigned n, unsigned bad)
{
  struct uart *u = uart_of(h);

  land_shifting(u);
  for (unsigned i = 0u; i < n; i++) { land(u, (uint8_t)bytes[i], i == bad); }
  line_idle(u);
}

void wire_in(UART_HandleTypeDef *h, const char *bytes, unsigned n) { wire_in_bad(h, bytes, n, n); }

void wire_starts(UART_HandleTypeDef *h, char byte)
{
  struct uart *u = uart_of(h);

  land_shifting(u);
  u->shifting = 1;
  u->shift    = (uint8_t)byte;
  u->shift_fe = 0;
}

void wire_lands(UART_HandleTypeDef *h)
{
  struct uart *u = uart_of(h);

  land_shifting(u);
  line_idle(u);
}

int uart_holds_nothing(const UART_HandleTypeDef *h)
{
  const struct uart *u = uart_of(h);
  return !u->rdr_full && !u->shifting && (u->isr & ERRORS) == 0u;
}

void hal_dma_channel_init(UART_HandleTypeDef *h) { uart_of(h)->chan = 0; }

void hal_uart_reset(UART_HandleTypeDef *h)
{
  struct uart *u = uart_of(h);

  memset(u, 0, sizeof *u);
  u->h               = h;
  h->RxState         = HAL_UART_STATE_READY;
  h->RxEventCallback = NULL;
  h->ErrorCallback   = NULL;
}

/* UART_Start_Receive_IT(), 8 bits, no parity: RxState busy, the error and
 * receive interrupts on; a byte already waiting is taken at once. */
void hal_arm_it(UART_HandleTypeDef *h, uint8_t *buf)
{
  struct uart *u = uart_of(h);

  u->it_buf  = buf;
  u->toidle  = 0;
  h->RxState = HAL_UART_STATE_BUSY_RX;
  u->eie     = 1;
  u->rxneie  = 1;
  service(u);
}

int hal_it_armed(const UART_HandleTypeDef *h) { const struct uart *u = uart_of(h); return u->rxneie && u->it_buf != NULL; }
int hal_dma_running(const UART_HandleTypeDef *h) { const struct uart *u = uart_of(h); return u->dmar && u->chan; }

HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *h, uint8_t *buf, uint16_t size)
{
  (void)size;
  if (h->RxState != HAL_UART_STATE_READY) { return HAL_BUSY; }
  hal_arm_it(h, buf);
  return HAL_OK;
}

/* HAL_UARTEx_ReceiveToIdle_DMA() and UART_Start_Receive_DMA(): RxState busy,
 * the channel started (busy if it still runs), then the error interrupt on,
 * which takes an error already flagged before the DMA request is on, then
 * the request, which hands the DMA a byte already waiting. If an error ended
 * the reception meanwhile, the start fails, as the HAL's says; else the idle
 * flag is cleared and the idle interrupt turned on. */
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *h, uint8_t *buf, uint16_t size)
{
  struct uart *u = uart_of(h);

  hal_dma_starts++;
  if (h->RxState != HAL_UART_STATE_READY) { return HAL_BUSY; }
  if (buf == NULL || size == 0u) { return HAL_ERROR; }
  u->toidle  = 1;
  h->RxState = HAL_UART_STATE_BUSY_RX;
  if (h->hdmarx != NULL) {
    if (u->chan) { h->RxState = HAL_UART_STATE_READY; return HAL_ERROR; }
    u->chan     = 1;
    u->dma_buf  = buf;
    u->dma_size = size;
    u->dma_pos  = 0u;
  }
  u->eie = 1;
  service(u);
  u->dmar = 1;
  service(u);
  if (!u->toidle) { return HAL_ERROR; }
  u->isr   &= ~FLAG_IDLE;
  u->idleie = 1;
  return HAL_OK;
}

/* HAL_UART_AbortReceive(): the receive interrupts off; in DMA mode the
 * request off and the channel aborted, and if that abort times out, it
 * returns HAL_TIMEOUT there and then. Else the error flags are cleared, the
 * data register flushed (RXFRQ; a byte still being shifted in is not there
 * yet), and RxState is ready. */
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *h)
{
  struct uart *u    = uart_of(h);
  int          fail = abort_fails != 0u && --abort_fails == 0u;

  u->rxneie = 0;
  u->eie    = 0;
  u->it_buf = NULL;
  if (u->toidle) { u->idleie = 0; }
  if (u->dmar) {
    u->dmar = 0;
    if (h->hdmarx != NULL && !fail) { u->chan = 0; }
  }
  if (fail) { return HAL_TIMEOUT; }
  u->isr     &= ~ERRORS;
  u->rdr_full = 0;
  h->RxState  = HAL_UART_STATE_READY;
  u->toidle   = 0;
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
