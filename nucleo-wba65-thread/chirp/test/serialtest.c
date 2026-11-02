/* Host checks for board_serial.c, against stub ST headers:
 *   - a burst of replies must not overrun ST's 1536-byte CLI output buffer
 *     (the model copies ST's two-buffer output path, with a guard area that
 *     records any overrun);
 *   - the line rules of CHIRP-PROV v1.8: a line over the limit is thrown away
 *     whole and gets no answer, a setup line whose bytes stop for 1.5 s is
 *     given up wherever it is, and CR, LF and CRLF each end a line. A CLI
 *     line typed by a person is never timed;
 *   - no secret reaches the console or ot-cli: not the rest of a setup line
 *     given up on a pause, not a push with stray bytes in front;
 *   - bytes a full ring lost damage the line they were in.
 * Run with make test. */

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
static char     st_wire[65536];
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
  if (st_wire_len + n < sizeof st_wire) {
    memcpy(st_wire + st_wire_len, st_buf[st_idx], n);
    st_wire_len += n;
  }
  st_size[st_idx] = 0u;
  st_idx ^= 1u;
}

static UART_HandleTypeDef st_uart = { HAL_UART_STATE_READY, HAL_UART_STATE_READY, 0, 0, 0, 0, 0 };
UART_HandleTypeDef *oTCLIuart = &st_uart;

/* ---- the rest of the platform -------------------------------------------------- */

static uint32_t tick = 100000u;
uint32_t HAL_GetTick(void) { return tick; }

HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *h) { (void)h; return HAL_OK; }
HAL_StatusTypeDef HAL_DMA_ConfigChannelAttributes(DMA_HandleTypeDef *h, uint32_t a) { (void)h; (void)a; return HAL_OK; }
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *h) { (void)h; }
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *h) { (void)h; return HAL_OK; }
/* The HAL's own 1-byte receive, which board_serial.c's wrap passes any
 * other UART to; nothing here arms one. */
HAL_StatusTypeDef __real_HAL_UART_Receive_IT(UART_HandleTypeDef *h, uint8_t *b, uint16_t n)
{ (void)h; (void)b; (void)n; return HAL_OK; }
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *h, uint8_t *b, uint16_t n)
{
  (void)h; (void)b; (void)n; return HAL_OK;
}
void HAL_NVIC_SetPriority(int irq, uint32_t pre, uint32_t sub) { (void)irq; (void)pre; (void)sub; }
void HAL_NVIC_EnableIRQ(int irq) { (void)irq; }
void UTIL_SEQ_SetTask(uint32_t task, uint32_t prio) { (void)task; (void)prio; }
void UTIL_SEQ_RegTask(uint32_t task, uint32_t flags, void (*fn)(void)) { (void)task; (void)flags; (void)fn; }

/* What ot-cli was handed. */
static char     cli_got[8192];
static unsigned cli_len;
void __real_otPlatUartReceived(const uint8_t *aBuf, uint16_t aBufLength)
{
  if (cli_len + aBufLength < sizeof cli_got) { memcpy(cli_got + cli_len, aBuf, aBufLength); cli_len += aBufLength; }
  cli_got[cli_len] = '\0';
}

/* ---- the test ----------------------------------------------------------------- */

#define ANNOUNCE "CHIRP! v=2 hw=wba65-thread-3f9a12 fw=v1.6.0 key_set=0 radios=thread t=demo-nucleo-wba65-thread\r\n"

static int fails, passes;

static void expect(int cond, const char *why)
{
  printf("%s  %s\n", cond ? "ok  " : "FAIL", why);
  fails += !cond;
  passes += cond;
}

/* What the provisioning responder gets: every line, and the last one. */
static char     chirp_last[BOARD_SERIAL_LINE_MAX + 1];
static unsigned chirp_lines;

static void on_line(char *line, unsigned len)
{
  chirp_lines++;
  memcpy(chirp_last, line, len);
  chirp_last[len] = '\0';
  if (len == 6u && memcmp(line, "CHIRP?", 6u) == 0) { board_serial_print("%s", ANNOUNCE); }
}

/* Bytes off the wire: one DMA event per 256 bytes, all at the current tick. */
static void receive(const char *bytes, unsigned n)
{
  while (n > 0u) {
    unsigned k = n < 256u ? n : 256u;
    memcpy(s_dma_buf, bytes, k);
    rx_event(oTCLIuart, (uint16_t)k);
    rx_task();
    bytes += k;
    n -= k;
  }
}

static void receive_text(const char *text) { receive(text, (unsigned)strlen(text)); }

/* Bytes through ot-cli's receive hook instead, in calls of `piece` bytes, the
 * scheduler running the task after each (ST's 1-byte receive is piece 1). */
static void hook(const char *bytes, unsigned n, unsigned piece)
{
  while (n > 0u) {
    unsigned k = n < piece ? n : piece;
    __wrap_otPlatUartReceived((const uint8_t *)bytes, (uint16_t)k);
    rx_task();
    bytes += k;
    n -= k;
  }
}

static void hook_text(const char *text, unsigned piece) { hook(text, (unsigned)strlen(text), piece); }

static void reset_seen(void)
{
  chirp_lines = 0u;
  chirp_last[0] = '\0';
  cli_len = 0u;
  cli_got[0] = '\0';
  arcUartProcess();
  st_wire_len = 0u;
}

static void test_burst(void)
{
  static char want[16384];
  unsigned    i, want_len = 0u;

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
}

static void test_lines(void)
{
  static char line[BOARD_SERIAL_LINE_MAX + 256];
  const char *inner = "CHIRP+ {\"hwid\":\"board-new\"}";
  size_t      n;
  int         clean = 1;

  puts("\n--- the line rules ---");

  /* Exactly the limit is read; one byte more is thrown away whole, with no answer. */
  reset_seen();
  strcpy(line, "CHIRP+ {\"hwid\":\"b\"}");
  n = strlen(line);
  memset(line + n, ' ', BOARD_SERIAL_LINE_MAX - n);
  strcpy(line + BOARD_SERIAL_LINE_MAX, "\r\n");
  receive_text(line);
  expect(chirp_lines == 1u && strlen(chirp_last) == BOARD_SERIAL_LINE_MAX,
         "a setup line of exactly 4096 bytes is read");
  reset_seen();
  line[BOARD_SERIAL_LINE_MAX] = ' ';
  strcpy(line + BOARD_SERIAL_LINE_MAX + 1u, "\r\n");
  receive_text(line);
  arcUartProcess();
  st_wire[st_wire_len] = '\0';
  expect(chirp_lines == 0u && strstr(st_wire, "CHIRP=") == NULL && cli_len == 0u,
         "one of 4097 is thrown away: not read, not answered, not handed to the CLI");

  /* No tail of an over-long line is a command: an inner push before, across or past the limit. */
  for (size_t cut = 0u; cut < strlen(inner); cut++) {
    size_t pad = BOARD_SERIAL_LINE_MAX - 7u - cut;
    reset_seen();
    memcpy(line, "CHIRP+ ", 7u);
    memset(line + 7, 'j', pad);
    sprintf(line + 7 + pad, "%s\r\n", inner);
    receive_text(line);
    clean = clean && chirp_lines == 0u && cli_len == 0u;
  }
  expect(clean, "no tail of a line over the limit is read as a command");
  reset_seen();
  receive_text("CHIRP?\r\n");
  expect(chirp_lines == 1u && strcmp(chirp_last, "CHIRP?") == 0, "the line after it is read");

  /* CR, LF and CRLF each end a line, once. */
  reset_seen();
  receive_text("CHIRP?\rCHIRP?\nCHIRP?\r\n");
  expect(chirp_lines == 3u, "CR, LF and CRLF each end a line, once");

  /* A setup line that stops for 1.5 s is given up; what follows is not joined to it. */
  reset_seen();
  receive_text("CHIRP+ {\"hwid\":");
  tick += BOARD_SERIAL_IDLE_MS;
  receive_text("\"late\"}\r\n");
  expect(chirp_lines == 0u, "a setup line whose bytes stopped for 1.5 s is given up, its tail not read as one");
  reset_seen();
  tick += 3000u;
  receive_text("CHIRP?\r\n");
  expect(chirp_lines == 1u && strcmp(chirp_last, "CHIRP?") == 0, "a probe after a pause is read");

  /* Pieces 1 s apart, as a browser tab out of view sends them, land. */
  reset_seen();
  {
    const char *push = "CHIRP+ {\"thread\":{\"dataset\":\"0e080000000000010000000300001835060004001fffe002\"},"
                       "\"token\":\"tok-0123456789\",\"hwid\":\"board-1\"}";
    for (const char *p = push; *p != '\0'; p += (strlen(p) < 64u ? strlen(p) : 64u)) {
      receive(p, (unsigned)(strlen(p) < 64u ? strlen(p) : 64u));
      tick += 1000u;
    }
    receive_text("\r\n");
    expect(chirp_lines == 1u && strcmp(chirp_last, push) == 0, "a push in 64-byte pieces a second apart lands whole");
  }

  /* A person typing at the CLI is never timed out. */
  reset_seen();
  receive_text("sta");
  tick += 5000u;
  receive_text("te\r\n");
  expect(cli_len == 7u && strcmp(cli_got, "state\r\n") == 0, "a CLI line typed slowly reaches ot-cli whole");

  /* The console is shared with ot-cli: a line is a setup line when it starts
   * with CHIRP, so stray bytes in front make it a CLI line, which ot-cli
   * refuses; the page's next probe, 500 ms later, is read. */
  reset_seen();
  receive_text("\x5a\xa6" "CHIRP?\r\n");
  expect(chirp_lines == 0u && cli_len > 0u, "stray bytes in front: a CLI line, not a setup line");
  receive_text("CHIRP?\r\n");
  expect(chirp_lines == 1u, "  ... and the probe after it is read");

  /* A line damaged by a receive overrun is not read either. */
  reset_seen();
  receive_text("CHIRP+ {\"hw");
  s_ring_lost = 1u;
  receive_text("id\":\"x\"}\r\n");
  arcUartProcess();
  st_wire[st_wire_len] = '\0';
  expect(chirp_lines == 0u && strstr(st_wire, "CHIRP=") == NULL, "a line an overrun damaged is dropped, unanswered");
}

/* Does any 4 bytes of the secret reach the console or ot-cli? */
static int leaks(const char *secret)
{
  char piece[5];

  arcUartProcess();
  st_wire[st_wire_len] = '\0';
  for (size_t i = 0u; i + 4u <= strlen(secret); i++) {
    memcpy(piece, secret + i, 4u);
    piece[4] = '\0';
    if (strstr(st_wire, piece) != NULL || strstr(cli_got, piece) != NULL) { return 1; }
  }
  return 0;
}

/* After any of the cases below, the console works as before: a probe is
 * answered and a CLI command reaches ot-cli. */
static void then_console_works(const char *what)
{
  char why[160];

  reset_seen();
  receive_text("CHIRP?\r\n");
  arcUartProcess();
  snprintf(why, sizeof why, "  ... then a fresh probe is answered (%s)", what);
  expect(chirp_lines == 1u && strcmp(chirp_last, "CHIRP?") == 0 && st_wire_len == sizeof ANNOUNCE - 1u, why);
  reset_seen();
  receive_text("state\r\n");
  snprintf(why, sizeof why, "  ... and a CLI command reaches ot-cli (%s)", what);
  expect(strcmp(cli_got, "state\r\n") == 0, why);
}

/* A setup line stopped for 1.5 s inside a secret, then the rest of it. */
static void stall_inside(const char *push, const char *secret, size_t into, const char *what)
{
  const char *at = strstr(push, secret);
  size_t      cut = (size_t)(at - push) + into;
  char        why[160];

  reset_seen();
  receive(push, (unsigned)cut);
  tick += BOARD_SERIAL_IDLE_MS;
  receive_text(push + cut);
  receive_text("\r\n");
  snprintf(why, sizeof why, "a setup line stopped inside %s: no byte of it reaches the console or ot-cli", what);
  expect(!leaks(secret) && cli_len == 0u, why);
  then_console_works(what);
}

/* Before board_serial_begin() the hook drops what it gets: nothing reaches
 * ot-cli and nothing is answered. */
static void test_hook_before_begin(void)
{
  puts("--- ot-cli's receive hook, before board_serial_begin() ---");
  reset_seen();
  hook_text("CHIRP?\r\n", 1u);
  hook_text("state\r\n", 8u);
  hook_text("CHIRP+ {\"token\":\"tok-EARLY-SECRET-77\"}\r\n", 64u);
  rx_task();
  arcUartProcess();
  st_wire[st_wire_len] = '\0';
  expect(cli_len == 0u && chirp_lines == 0u && st_wire_len == 0u,
         "before the console is ours, the hook drops everything: nothing to ot-cli, no answer");
  expect(!leaks("tok-EARLY-SECRET-77"), "  ... and no byte of a push is shown");
}

/* After the probe and the CLI command, through the hook too. */
static void then_hook_works(const char *what)
{
  char why[160];

  reset_seen();
  hook_text("CHIRP?\r\n", 1u);
  arcUartProcess();
  snprintf(why, sizeof why, "  ... then a probe through the hook is answered (%s)", what);
  expect(chirp_lines == 1u && strcmp(chirp_last, "CHIRP?") == 0 && st_wire_len == sizeof ANNOUNCE - 1u, why);
  reset_seen();
  hook_text("state\r\n", 1u);
  snprintf(why, sizeof why, "  ... and a CLI command through the hook reaches ot-cli (%s)", what);
  expect(strcmp(cli_got, "state\r\n") == 0, why);
}

/* The hook keeps the DMA path's rules: a stray prefix, a push a byte at a
 * time, a marker split across calls, a credential tail after a pause. */
static void test_hook(void)
{
  static const char stray[] = "XCHIRP+ {\"token\":\"tok-HOOK-STRAY-0042\"}\r\n";
  static const char push[]  = "CHIRP+ {\"token\":\"tok-HOOK-BYTEWISE-9\"}\r\n";

  puts("--- ot-cli's receive hook, after board_serial_begin() ---");
  reset_seen();
  hook_text(stray, sizeof stray);
  expect(cli_len == 0u && chirp_lines == 0u && !leaks("tok-HOOK-STRAY-0042"),
         "a push with a stray byte in front, through the hook: not to ot-cli, not echoed, not read");
  then_hook_works("stray prefix");

  reset_seen();
  hook_text(push, 1u);
  expect(chirp_lines == 1u && strncmp(chirp_last, push, sizeof push - 3u) == 0 && cli_len == 0u &&
             !leaks("tok-HOOK-BYTEWISE-9"),
         "a push a byte at a time through the hook is one setup line, never echoed or handed to ot-cli");
  then_hook_works("push a byte at a time");

  reset_seen();
  hook_text("CH", 2u);
  hook_text("IRP+ {\"token\":\"tok-HOOK-SPLIT-1234\"}\r\n", 64u);
  expect(chirp_lines == 1u && cli_len == 0u && !leaks("tok-HOOK-SPLIT-1234"),
         "a CHIRP marker split across hook calls still makes one setup line");
  then_hook_works("marker split");

  reset_seen();
  hook_text("CHIRP+ {\"token\":\"tok-HOOK-", 1u);
  tick += BOARD_SERIAL_IDLE_MS;
  hook_text("TAIL-SECRET-5150\"}\r\n", 1u);
  expect(cli_len == 0u && chirp_lines == 0u && !leaks("TAIL-SECRET-5150"),
         "a setup line that pauses inside its credential, through the hook: the tail reaches nothing");
  then_hook_works("credential tail");
}

/* A push cut by a restart: its start came before board_serial_begin() (the
 * hook drops it) or while nothing read the UART, and its tail after it,
 * within the idle limit. */
static void restart_with(const char *before, unsigned nb)
{
  s_begun = 0u;
  reset_seen();
  hook(before, nb, 64u);
  board_serial_begin(on_line);
}

static void test_restart(void)
{
  static const char push[] = "CHIRP+ {\"token\":\"tok-RESTART-SECRET-77\"}\r\n";
  const unsigned n = (unsigned)sizeof push - 1u;
  unsigned k, j, bad = 0u, first_bad = 0u, cases = 0u, first_split = 0u;
  char why[240];

  puts("--- a push cut by a restart ---");
  for (k = 1u; k < n; k++) {
    restart_with(push, k);
    tick += 300u;                         /* the tail, within the idle limit */
    reset_seen();
    receive(push + k, n - k);
    arcUartProcess();
    st_wire[st_wire_len] = '\0';
    if (cli_len != 0u || chirp_lines != 0u || st_wire_len != 0u || leaks("RESTART-SECRET-77")) {
      bad++;
      if (first_bad == 0u) { first_bad = k; }
    }
  }
  snprintf(why, sizeof why, "a push split at each of its %u positions across a restart, its tail within the idle "
           "limit: nothing to ot-cli, nothing echoed, no answer (%u wrong, the first at byte %u)", n - 1u, bad, first_bad);
  expect(bad == 0u, why);

  /* The tail in two pieces, the page stalling past the idle limit between
   * them: the first byte after the start made it a tail, and a pause inside
   * it follows the ordinary rule, dropped through its line ending. */
  bad = first_bad = 0u;
  for (k = 1u; k < n; k++) {
    for (j = 1u; j < n - k; j++) {
      restart_with(push, k);
      tick += 300u;
      reset_seen();
      receive(push + k, j);
      tick += BOARD_SERIAL_IDLE_MS;
      receive(push + k + j, n - k - j);
      arcUartProcess();
      st_wire[st_wire_len] = '\0';
      cases++;
      if (cli_len != 0u || chirp_lines != 0u || st_wire_len != 0u || leaks("RESTART-SECRET-77")) {
        if (bad++ == 0u) { first_bad = k; first_split = j; }
      }
    }
  }
  snprintf(why, sizeof why, "a push cut by a restart, its tail in two pieces with a pause past the idle limit "
           "between, at every split: nothing to ot-cli, nothing echoed, no answer (%u of %u wrong, the first "
           "cut at byte %u, the tail split at %u)", bad, cases, first_bad, first_split);
  expect(bad == 0u, why);

  /* After the tail: a probe is answered, and a command sent once the idle
   * limit has passed reaches ot-cli. */
  reset_seen();
  receive_text("CHIRP?\r\n");
  arcUartProcess();
  expect(chirp_lines == 1u && st_wire_len == sizeof ANNOUNCE - 1u, "  ... then a probe is answered");
  tick += BOARD_SERIAL_IDLE_MS;
  reset_seen();
  receive_text("state\r\n");
  expect(strcmp(cli_got, "state\r\n") == 0, "  ... and a CLI command after the idle limit reaches ot-cli");

  /* A command typed within the idle limit of a start cannot be told from the
   * tail of a push: it is dropped. One typed after the idle limit is not. */
  restart_with("", 0u);
  tick += 200u;
  reset_seen();
  receive_text("state\r\n");
  arcUartProcess();
  st_wire[st_wire_len] = '\0';
  expect(cli_len == 0u && st_wire_len == 0u, "a CLI command within the idle limit of a start is dropped, unechoed");
  restart_with("", 0u);
  tick += BOARD_SERIAL_IDLE_MS;
  reset_seen();
  receive_text("state\r\n");
  expect(strcmp(cli_got, "state\r\n") == 0, "a CLI command once the idle limit has passed since a start reaches ot-cli");

  /* A fresh probe at once after a start is a line that starts with CHIRP. */
  restart_with("", 0u);
  tick += 100u;
  reset_seen();
  receive_text("CHIRP?\r\n");
  arcUartProcess();
  expect(chirp_lines == 1u && st_wire_len == sizeof ANNOUNCE - 1u, "a probe at once after a start is answered");

  /* A batch is taken in with BASEPRI raised to the UART's priority, and put back. */
  test_basepri = 0u;
  test_basepri_raised = 0u;
  receive_text("CHIRP?\r\n");
  expect(test_basepri_raised > 0u && test_basepri == 0u,
         "a batch is taken in with BASEPRI raised to the UART's priority, then restored");
}

static void test_secrets(void)
{
  static const char tok[]  = "tok-S3CRET-0123456789";
  static const char key[]  = "00112233445566778899aabbccddeeff";
  static const char code[] = "CHIRP-SETUP-4711-ABCD";
  static const char ds[]   = "0e080000000000010000000300001835060004001fffe0020811111111222222220708fd";
  char push[512];

  puts("\n--- no secret reaches the console or ot-cli ---");

  snprintf(push, sizeof push, "CHIRP+ {\"thread\":{\"dataset\":\"%s\"},\"token\":\"%s\"}", ds, tok);
  stall_inside(push, tok, 7u, "a token");
  snprintf(push, sizeof push, "CHIRP+ {\"thread\":{\"name\":\"lab\",\"key\":\"%s\"},\"token\":\"t\"}", key);
  stall_inside(push, key, 10u, "a network key");
  snprintf(push, sizeof push, "CHIRP+ {\"thread\":{\"dataset\":\"%s\"},\"claim\":\"%s\"}", ds, code);
  stall_inside(push, code, 8u, "a setup code");
  snprintf(push, sizeof push, "CHIRP+ {\"thread\":{\"dataset\":\"%s\"},\"token\":\"t\"}", ds);
  stall_inside(push, ds, 21u, "a dataset");

  /* Split just before a setup code, the rest starts with CHIRP: it is read
   * as a line of its own, so it goes to the responder (chirp_prov.c answers
   * it without a byte of it), never to ot-cli. */
  snprintf(push, sizeof push, "CHIRP+ {\"thread\":{\"dataset\":\"%s\"},\"claim\":\"%s\"}", ds, code);
  reset_seen();
  receive(push, (unsigned)(strstr(push, code) - push));
  tick += BOARD_SERIAL_IDLE_MS;
  receive_text(strstr(push, code));
  receive_text("\r\n");
  expect(chirp_lines == 1u && strncmp(chirp_last, code, sizeof code - 1u) == 0 && cli_len == 0u && !leaks(code),
         "the rest of a setup line that starts with CHIRP goes to the responder, not to ot-cli");
  then_console_works("the rest started with CHIRP");

  /* Two pauses inside one token. */
  snprintf(push, sizeof push, "CHIRP+ {\"token\":\"%s\"}", tok);
  reset_seen();
  receive(push, 20u);
  tick += BOARD_SERIAL_IDLE_MS;
  receive(push + 20, 5u);
  tick += BOARD_SERIAL_IDLE_MS;
  receive_text(push + 25);
  receive_text("\r\n");
  expect(!leaks(tok) && cli_len == 0u, "a token stopped twice: no byte of it reaches the console or ot-cli");
  then_console_works("two pauses");

  /* A whole push with a stray byte in front, or CHIRP+ further in. */
  reset_seen();
  snprintf(push, sizeof push, "\x5a" "CHIRP+ {\"thread\":{\"dataset\":\"%s\"},\"token\":\"%s\"}\r\n", ds, tok);
  receive_text(push);
  arcUartProcess();
  expect(!leaks(tok) && !leaks(ds) && cli_len == 0u && chirp_lines == 0u && st_wire_len == 0u,
         "a push with a stray byte in front: not echoed, not handed to ot-cli, no answer");
  then_console_works("a stray byte in front");
  reset_seen();
  snprintf(push, sizeof push, "state CHIRP+ {\"token\":\"%s\"}\r\n", tok);
  receive_text(push);
  arcUartProcess();
  expect(!leaks(tok) && cli_len == 0u && st_wire_len == 0u, "a line holding CHIRP+ further in is dropped the same way");
  then_console_works("CHIRP+ further in");
}

/* A setup line idle for 1.5 s is given up wherever it is, so the probe the
 * page sends next is read alone: answered, never joined into bad-json. */
static void test_idle_anywhere(void)
{
  static char long_line[BOARD_SERIAL_LINE_MAX + 64];
  const char *push = "CHIRP+ {\"token\":\"tok\",\"hwid\":\"board-1\"}";
  size_t      cuts[] = { 5u, 6u, 8u, 11u, 19u, 27u, strlen(push) };
  char        why[160];

  puts("\n--- a setup line idle for 1.5 s is given up wherever it is ---");
  for (size_t i = 0u; i < sizeof cuts / sizeof cuts[0]; i++) {
    reset_seen();
    receive(push, (unsigned)cuts[i]);
    tick += BOARD_SERIAL_IDLE_MS;
    receive_text("CHIRP?\r\n");
    snprintf(why, sizeof why, "stopped after %u bytes: the probe after it is read alone", (unsigned)cuts[i]);
    expect(chirp_lines == 1u && strcmp(chirp_last, "CHIRP?") == 0, why);
  }

  /* Over the limit. */
  reset_seen();
  memcpy(long_line, "CHIRP+ ", 7u);
  memset(long_line + 7, 'j', BOARD_SERIAL_LINE_MAX);
  receive(long_line, BOARD_SERIAL_LINE_MAX + 7u);
  tick += BOARD_SERIAL_IDLE_MS;
  receive_text("CHIRP?\r\n");
  expect(chirp_lines == 1u && strcmp(chirp_last, "CHIRP?") == 0,
         "stopped over the limit: the probe after it is read alone");

  /* Damaged by an overrun. */
  reset_seen();
  receive_text("CHIRP+ {\"hw");
  s_ring_lost = 1u;
  receive_text("id\":");
  tick += BOARD_SERIAL_IDLE_MS;
  receive_text("CHIRP?\r\n");
  expect(chirp_lines == 1u && strcmp(chirp_last, "CHIRP?") == 0,
         "stopped after an overrun damaged it: the probe after it is read alone");
}

/* Bytes the ring had no room for damage the line they were in, not the one
 * the scheduler happened to be assembling when it next ran. */
static void test_ring_full(void)
{
  static char push[RX_RING + 600];
  unsigned    n, at;

  puts("\n--- a full ring ---");
  reset_seen();
  n = (unsigned)sprintf(push, "CHIRP?\r\nCHIRP+ {\"thread\":{\"dataset\":\"");
  for (; n < RX_RING + 400u; n++) { push[n] = "0123456789abcdef"[n % 16u]; }
  n += (unsigned)sprintf(push + n, "\"},\"token\":\"t\"}\r\n");

  /* The scheduler is held up: the DMA events come, the task does not run. */
  for (at = 0u; at < RX_RING + 200u; at += 200u) {
    memcpy(s_dma_buf, push + at, 200u);
    rx_event(oTCLIuart, 200u);
  }
  rx_task();
  receive(push + at, n - at);
  expect(chirp_lines == 1u && strcmp(chirp_last, "CHIRP?") == 0,
         "a probe that came whole is answered; the push that lost bytes is not read");
}

int main(void)
{
  test_hook_before_begin();
  board_serial_begin(on_line);
  test_burst();
  test_lines();
  test_secrets();
  test_idle_anywhere();
  test_ring_full();
  test_hook();
  test_restart();

  printf("\nserialtest: %d passed, %d failed\n", passes, fails);
  printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
