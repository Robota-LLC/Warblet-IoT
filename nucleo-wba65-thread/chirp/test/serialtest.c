/* Host check for board_serial.c: a burst of replies must not overrun ST's
 * 1536-byte CLI output buffer. The model copies ST's two-buffer output path,
 * with a guard area that records any overrun. Run with make test. */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../src/board_serial.c"

/* ---- ST's output path, modelled --------------------------------------------- */

#define ST_BUFFER_SIZE 1536u
#define ST_GUARD       8192u

static char     st_buf[2][ST_BUFFER_SIZE + ST_GUARD];
static unsigned st_size[2];
static unsigned st_idx;
static int      st_tx_scheduled;
static unsigned st_peak;
static char     st_wire[16384];
static unsigned st_wire_len;

int CliUartOutput(void *aContext, const char *aFormat, va_list aArguments)
{
  unsigned size = st_size[st_idx];
  int      ret  = vsnprintf(&st_buf[st_idx][size], ST_BUFFER_SIZE, aFormat, aArguments);

  (void)aContext;
  if (ret > 0 && strncmp(&st_buf[st_idx][size], "> ", 2) != 0) {
    st_size[st_idx] += (unsigned)ret;
    if (st_size[st_idx] > st_peak) { st_peak = st_size[st_idx]; }
    st_tx_scheduled = 1;
  }
  return ret;
}

/* The DMA is modelled as finishing at once. */
void arcUartProcess(void)
{
  unsigned n = st_size[st_idx];

  if (!st_tx_scheduled || n == 0u) { return; }
  if (n > ST_BUFFER_SIZE) { n = ST_BUFFER_SIZE; }
  memcpy(st_wire + st_wire_len, st_buf[st_idx], n);
  st_wire_len += n;
  st_size[st_idx] = 0u;
  st_idx ^= 1u;
}

static UART_HandleTypeDef st_uart = { HAL_UART_STATE_READY, HAL_UART_STATE_READY, 0, 0, 0 };
UART_HandleTypeDef *oTCLIuart = &st_uart;

/* ---- the rest of the platform, inert ------------------------------------------ */

HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *h) { (void)h; return HAL_OK; }
HAL_StatusTypeDef HAL_DMA_ConfigChannelAttributes(DMA_HandleTypeDef *h, uint32_t a) { (void)h; (void)a; return HAL_OK; }
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *h) { (void)h; }
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *h) { (void)h; return HAL_OK; }
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *h, uint8_t *b, uint16_t n)
{
  (void)h; (void)b; (void)n; return HAL_OK;
}
void HAL_NVIC_SetPriority(int irq, uint32_t pre, uint32_t sub) { (void)irq; (void)pre; (void)sub; }
void HAL_NVIC_EnableIRQ(int irq) { (void)irq; }
void UTIL_SEQ_SetTask(uint32_t task, uint32_t prio) { (void)task; (void)prio; }
void UTIL_SEQ_RegTask(uint32_t task, uint32_t flags, void (*fn)(void)) { (void)task; (void)flags; (void)fn; }
void __real_otPlatUartReceived(const uint8_t *aBuf, uint16_t aBufLength) { (void)aBuf; (void)aBufLength; }

/* ---- the test ----------------------------------------------------------------- */

#define ANNOUNCE "CHIRP! v=2 hw=wba65-demo-3f9a12 radios=thread sig=1 t=demo-nucleo-wba65-thread\r\n"

static int fails;

static void expect(int cond, const char *why)
{
  printf("%s  %s\n", cond ? "ok  " : "FAIL", why);
  fails += !cond;
}

/* What the provisioning responder does with each probe. */
static void on_line(char *line, unsigned len)
{
  if (len == 6u && memcmp(line, "CHIRP?", 6u) == 0) { board_serial_print("%s", ANNOUNCE); }
}

int main(void)
{
  static char want[16384];
  unsigned    i, want_len = 0u;

  board_serial_begin(on_line);

  /* 30 probes arrive in one DMA event and are answered in one task run. */
  for (i = 0u; i < 30u; i++) {
    memcpy(&s_dma_buf[i * 8u], "CHIRP?\r\n", 8u);
    memcpy(want + want_len, ANNOUNCE, sizeof ANNOUNCE - 1u);
    want_len += (unsigned)sizeof ANNOUNCE - 1u;
  }
  rx_event(oTCLIuart, 240u);
  rx_task();

  printf("      30 replies, %u bytes; peak fill of ST's buffer %u of %u\n",
         want_len, st_peak, ST_BUFFER_SIZE);
  expect(st_peak <= ST_BUFFER_SIZE, "a 30-probe burst stays inside ST's output buffer");

  arcUartProcess();
  expect(st_wire_len == want_len && memcmp(st_wire, want, want_len) == 0,
         "every reply reaches the wire whole and in order");

  /* One oversized print is cut to a bounded line, not passed through. */
  {
    static char big[5000];
    unsigned    before = st_wire_len;

    memset(big, 'x', sizeof big - 1u);
    board_serial_print("%s", big);
    arcUartProcess();
    expect(st_peak <= ST_BUFFER_SIZE && st_wire_len - before > 0u && st_wire_len - before <= 256u,
           "a 5000-byte print is cut to at most 256 bytes");
  }

  printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
