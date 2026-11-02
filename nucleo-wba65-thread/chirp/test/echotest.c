/* Host checks for the start of the console, on ST's real receive path: ST's
 * own openthread/platform/uart.c, as `make fetch` places it, built with
 * board_serial.c and linked the way the firmware is (--wrap). Before
 * board_serial_begin() takes the UART over, ST's 1-byte receive callback
 * would echo a line on CR before our hook saw any of it: a push, or the tail
 * of one a restart cut, would reach the console. These checks send such
 * lines before the takeover and look at what reached the console.
 *   - ST's otPlatUartEnable() leaves the CLI UART's receive unarmed;
 *   - a complete push, and a push's tail, before the takeover: ST takes no
 *     byte, nothing is echoed, nothing reaches ot-cli;
 *   - the control: armed as the HAL arms it with no wrap in the way, ST's
 *     callback does echo a line, so the model reaches ST's echo;
 *   - the wrap refuses the CLI UART's receive and passes any other UART's;
 *   - after the takeover, a probe is read and a CLI command reaches ot-cli,
 *     echoed once, by board_serial.c;
 *   - bytes still in the UART at the takeover (or a byte still being shifted
 *     in, or bytes that come while the DMA channel is set up) are flushed or
 *     dropped as a tail: nothing echoed, no overrun notice, no answer.
 * Run with make test, after make fetch. */

#include <stdio.h>
#include <string.h>

#include "board_serial.h"
#include "echo_hal.h"
#include "stm32_seq.h"

/* ST's uart.c: its entry points and the globals the test reads. */
typedef int otError;
otError otPlatUartEnable(void);
void    arcUartProcess(void);
extern uint8_t otUartRX;

/* ---- the platform ST's uart.c and board_serial.c run on -------------------------- */

UART_HandleTypeDef huart1 = { HAL_UART_STATE_READY, HAL_UART_STATE_READY, 0, 0, 0, 0, 0 };   /* the CLI's */
UART_HandleTypeDef huart2 = { HAL_UART_STATE_READY, HAL_UART_STATE_READY, 0, 0, 0, 0, 0 };   /* another one */

static uint32_t tick = 100000u;
uint32_t HAL_GetTick(void) { return tick; }

/* Bytes that arrive while board_serial_begin() sets its DMA channel up. */
static const char *during_setup = "";
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *h)
{
  (void)h;
  uart_holds(during_setup, (unsigned)strlen(during_setup));
  during_setup = "";
  return HAL_OK;
}
HAL_StatusTypeDef HAL_DMA_ConfigChannelAttributes(DMA_HandleTypeDef *h, uint32_t a) { (void)h; (void)a; return HAL_OK; }
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *h) { (void)h; }
void HAL_NVIC_SetPriority(int irq, uint32_t pre, uint32_t sub) { (void)irq; (void)pre; (void)sub; }
void HAL_NVIC_EnableIRQ(int irq) { (void)irq; }

/* ST's scheduler, cut down: board_serial.c's task, and ST's UART task, which
 * runs arcUartProcess() (app_thread.c). */
static void (*tasks[32])(void);
static uint32_t due;
void UTIL_SEQ_RegTask(uint32_t task, uint32_t flags, void (*fn)(void))
{
  (void)flags;
  for (unsigned i = 0u; i < 32u; i++) { if (task & (1u << i)) { tasks[i] = fn; } }
}
void UTIL_SEQ_SetTask(uint32_t task, uint32_t prio) { (void)prio; due |= task; }
static int st_uart_due;
void APP_THREAD_ScheduleUART(void) { st_uart_due = 1; }

static void run_tasks(void)
{
  for (int round = 0; round < 8 && (due != 0u || st_uart_due); round++) {
    uint32_t now = due;
    due = 0u;
    for (unsigned i = 0u; i < 32u; i++) { if ((now & (1u << i)) && tasks[i] != NULL) { tasks[i](); } }
    if (st_uart_due) { st_uart_due = 0; arcUartProcess(); }
  }
}

/* What ot-cli was handed: OpenThread's CLI input, which board_serial.c's
 * __real_otPlatUartReceived() reaches through --wrap. */
static char     cli_got[512];
static unsigned cli_len;
void otPlatUartReceived(const uint8_t *aBuf, uint16_t aBufLength)
{
  if (cli_len + aBufLength < sizeof cli_got) { memcpy(cli_got + cli_len, aBuf, aBufLength); cli_len += aBufLength; }
  cli_got[cli_len] = '\0';
}

/* What the provisioning responder gets. */
static unsigned chirp_lines;
static void on_line(char *line, unsigned len) { (void)line; (void)len; chirp_lines++; }

/* ---- the test ----------------------------------------------------------------- */

static int fails, passes;

static void expect(int cond, const char *why)
{
  printf("%s  %s\n", cond ? "ok  " : "FAIL", why);
  fails += !cond;
  passes += cond;
}

/* Does any 4 bytes of the secret reach the console or ot-cli? */
static int shown(const char *secret)
{
  char piece[5];
  for (size_t i = 0u; i + 4u <= strlen(secret); i++) {
    memcpy(piece, secret + i, 4u);
    piece[4] = '\0';
    if (strstr(wire_out, piece) != NULL || strstr(cli_got, piece) != NULL) { return 1; }
  }
  return 0;
}

static void clear_seen(void)
{
  wire_out_len = 0u;
  wire_out[0] = '\0';
  cli_len = 0u;
  cli_got[0] = '\0';
  it_bytes = 0u;
}

static unsigned count(const char *hay, const char *needle)
{
  unsigned n = 0u;
  for (const char *p = hay; (p = strstr(p, needle)) != NULL; p++) { n++; }
  return n;
}

int main(void)
{
  static const char push[] = "CHIRP+ {\"token\":\"tok-ECHO-SECRET-5150\"}\r\n";
  static const char tail[] = "n\":\"tok-ECHO-TAIL-7788\"}\r\n";
  uint8_t           other;

  puts("--- ST's receive, before the takeover ---");
  (void)otPlatUartEnable();
  expect(!hal_it_armed(&huart1), "ST's otPlatUartEnable() leaves the CLI UART's receive unarmed");

  clear_seen();
  wire_in(&huart1, push, (unsigned)strlen(push));
  run_tasks();
  expect(it_bytes == 0u && !shown("tok-ECHO-SECRET-5150") && wire_out_len == 0u && cli_len == 0u,
         "a complete push before the takeover: ST takes no byte, nothing is echoed, nothing reaches ot-cli");

  clear_seen();
  wire_in(&huart1, tail, (unsigned)strlen(tail));
  run_tasks();
  expect(it_bytes == 0u && !shown("tok-ECHO-TAIL-7788") && wire_out_len == 0u && cli_len == 0u,
         "the tail of a push a restart cut, before the takeover: ST takes no byte, nothing is echoed");

  /* The control: ST's receive armed for each byte as the HAL arms it when
   * nothing is in the way, so its own callback runs. */
  clear_seen();
  for (const char *c = "ab\r"; *c != '\0'; c++) {
    hal_arm_it(&huart1, &otUartRX);
    wire_in(&huart1, c, 1u);
  }
  run_tasks();
  expect(it_bytes == 3u && strstr(wire_out, "ab\r\n") != NULL,
         "the control: armed as the HAL arms it with no wrap in the way, ST's own callback echoes a line on CR");
  (void)HAL_UART_AbortReceive(&huart1);

  /* Calls from outside, as ST's: the CLI UART's receive refused, another
   * UART's passed on. */
  expect(HAL_UART_Receive_IT(&huart1, &other, 1u) != HAL_OK && !hal_it_armed(&huart1),
         "a 1-byte receive asked for on the CLI UART is refused");
  expect(HAL_UART_Receive_IT(&huart2, &other, 1u) == HAL_OK && hal_it_armed(&huart2),
         "  ... and one asked for on another UART is armed");

  puts("--- after the takeover ---");
  clear_seen();
  board_serial_begin(on_line);
  expect(hal_dma_running(&huart1) && !hal_it_armed(&huart1),
         "board_serial_begin() receives with DMA; ST's 1-byte receive stays unarmed");

  tick += BOARD_SERIAL_IDLE_MS;
  clear_seen();
  wire_in(&huart1, "CHIRP?\r\n", 8u);
  run_tasks();
  expect(chirp_lines == 1u && it_bytes == 0u && strstr(wire_out, "CHIRP?") == NULL,
         "a probe after the idle limit is read, not echoed");

  tick += BOARD_SERIAL_IDLE_MS;
  clear_seen();
  wire_in(&huart1, "state\r\n", 7u);
  run_tasks();
  expect(strcmp(cli_got, "state\r\n") == 0 && count(wire_out, "state") == 1u && it_bytes == 0u,
         "a CLI command after the idle limit reaches ot-cli, echoed once, by board_serial.c");

  puts("--- bytes still in the UART at the takeover ---");
  {
    /* Each case is a start again, as after a restart, with a push arriving
     * across it: what is in the UART at the takeover, and what follows it
     * within the idle limit, is the tail of a push. It is dropped through its
     * line ending: nothing echoed, nothing answered, nothing to ot-cli. A
     * probe after the idle limit is answered. */
    static const struct {
      const char *why, *waiting, *setup;
      unsigned    leaves;
    } cases[] = {
      {"bytes waiting in the UART at the takeover are flushed; the rest is dropped as a tail",
       "\"token\":\"tok-WAIT", "", 0u},
      {"a byte still being shifted in at the flush reaches the DMA first, and is dropped as a tail",
       "\"token\":\"tok-WAIT", "", 1u},
      {"bytes that come while the DMA channel is set up are flushed before it starts: no overrun notice",
       "", "\"tok-WAIT", 0u},
    };
    for (unsigned i = 0u; i < sizeof cases / sizeof cases[0]; i++) {
      unsigned before;
      hal_uart_reset(&huart1);
      flush_leaves = cases[i].leaves;
      clear_seen();
      wire_in(&huart1, cases[i].waiting, (unsigned)strlen(cases[i].waiting));   /* no receive armed: held */
      during_setup = cases[i].setup;
      before = chirp_lines;
      board_serial_begin(on_line);
      hal_dma_takes_waiting(&huart1);
      run_tasks();
      tick += 300u;                                                             /* within the idle limit */
      wire_in(&huart1, "ING-4242\"}\r\n", 12u);
      run_tasks();
      expect(!shown("tok-WAITING-4242") && wire_out_len == 0u && cli_len == 0u && chirp_lines == before &&
                 uart_waiting_bytes() == 0u,
             cases[i].why);
      tick += BOARD_SERIAL_IDLE_MS;
      clear_seen();
      wire_in(&huart1, "CHIRP?\r\n", 8u);
      run_tasks();
      expect(chirp_lines == before + 1u, "  ... and a probe after the idle limit is read");
    }
    flush_leaves = 0u;
  }

  printf("\nechotest: %d passed, %d failed\n", passes, fails);
  printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
