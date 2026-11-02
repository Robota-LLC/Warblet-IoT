/* Host check for this demo's real start, with OpenThread and ST's platform
 * stood in (chirp/test/stub and the fakes below): __wrap_otCliInit() as ST's
 * start calls it, then the scheduler, the timers and the console, running the
 * real chirp_prov.c, chirp_report.c, board_serial.c and the store.
 *   - no report goes out without a stored credential;
 *   - a credential at its limit, made of quotation marks and backslashes, is
 *     sent: after a push with no restart, and after the next start;
 *   - the rest of a setup line given up on a pause prints nothing of itself.
 * Each scenario is one start from reset, so run with no argument the test
 * runs itself once per scenario. Run with make test. What this cannot show:
 * OpenThread itself, ST's scheduler, UART and flash, and the radio. Those
 * are the bench's. */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L   /* popen */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chirpval.h"
#include "flashcfg.h"
#include "stm32wbaxx.h"
#include "stm32_seq.h"
#include "stm32_timer.h"
#include "openthread/instance.h"

#ifdef _WIN32
#define popen  _popen
#define pclose _pclose
#endif

/* chirp_prov.c's start, which the linker wraps around ST's otCliInit(). */
void __wrap_otCliInit(otInstance *aInstance, otCliOutputCallback aCallback, void *aContext);

static int fails, passes;

static void expect(int ok, const char *what, const char *detail)
{
  if (ok) {
    passes++;
    printf("ok    %s\n", what);
  } else {
    fails++;
    printf("FAIL  %s%s%s\n", what, detail ? "\n      " : "", detail ? detail : "");
  }
}

/* ---- time, the scheduler and the timers ------------------------------------ */

static uint32_t now_ms = 1000u;
uint32_t HAL_GetTick(void) { return now_ms; }

static void   (*tasks[32])(void);
static uint32_t pending;

void UTIL_SEQ_RegTask(uint32_t task, uint32_t flags, void (*fn)(void))
{
  (void)flags;
  for (unsigned i = 0u; i < 32u; i++) { if (task & (1u << i)) { tasks[i] = fn; } }
}

void UTIL_SEQ_SetTask(uint32_t task, uint32_t prio) { (void)prio; pending |= task; }

static void run_tasks(void)
{
  while (pending != 0u) {
    unsigned i = 0u;
    while ((pending & (1u << i)) == 0u) { i++; }
    pending &= ~(1u << i);
    if (tasks[i] != NULL) { tasks[i](); }
  }
}

static UTIL_TIMER_Object_t *timers[8];
static unsigned             n_timers;

UTIL_TIMER_Status_t UTIL_TIMER_Create(UTIL_TIMER_Object_t *t, uint32_t period, UTIL_TIMER_Mode_t mode,
                                      void (*callback)(void *arg), void *arg)
{
  if (n_timers == sizeof timers / sizeof timers[0]) { return UTIL_TIMER_INVALID_PARAM; }
  t->period = period; t->mode = (int)mode; t->callback = callback; t->arg = arg; t->running = 0;
  timers[n_timers++] = t;
  return UTIL_TIMER_OK;
}

UTIL_TIMER_Status_t UTIL_TIMER_Start(UTIL_TIMER_Object_t *t) { t->running = 1; t->due = now_ms + t->period; return UTIL_TIMER_OK; }
UTIL_TIMER_Status_t UTIL_TIMER_Stop(UTIL_TIMER_Object_t *t)  { t->running = 0; return UTIL_TIMER_OK; }

/* Let time pass a millisecond at a time: due timers fire, the scheduler runs. */
static void pass_ms(uint32_t ms)
{
  for (uint32_t end = now_ms + ms; now_ms < end; ) {
    now_ms++;
    for (unsigned i = 0u; i < n_timers; i++) {
      UTIL_TIMER_Object_t *t = timers[i];
      if (!t->running || t->due != now_ms) { continue; }
      if (t->mode == UTIL_TIMER_PERIODIC) { t->due += t->period; } else { t->running = 0; }
      t->callback(t->arg);
    }
    run_tasks();
  }
}

/* ---- the console: ST's CLI output sink, its UART, ot-cli's input -------------- */

static char     console[65536];
static unsigned console_len;

int CliUartOutput(void *aContext, const char *aFormat, va_list aArguments)
{
  unsigned room = (unsigned)sizeof console - console_len;
  int      n    = vsnprintf(console + console_len, room, aFormat, aArguments);

  (void)aContext;
  if (n > 0) { console_len += ((unsigned)n < room) ? (unsigned)n : room - 1u; }
  return n;
}

void arcUartProcess(void) { }

static UART_HandleTypeDef uart = { HAL_UART_STATE_READY, HAL_UART_STATE_READY, 0, 0, 0 };
UART_HandleTypeDef *oTCLIuart = &uart;

static char     cli_got[4096];
static unsigned cli_len;

void __real_otPlatUartReceived(const uint8_t *aBuf, uint16_t aBufLength)
{
  if (cli_len + aBufLength < sizeof cli_got) { memcpy(cli_got + cli_len, aBuf, aBufLength); cli_len += aBufLength; }
}

void __real_otCliInit(otInstance *aInstance, otCliOutputCallback aCallback, void *aContext)
{
  (void)aInstance; (void)aCallback; (void)aContext;
}

/* board_serial.c's receive buffer, handed over when it starts reception. */
static uint8_t *rx_buf;

HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *h, uint8_t *b, uint16_t n)
{
  (void)h; (void)n;
  rx_buf = b;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *h) { (void)h; return HAL_OK; }
HAL_StatusTypeDef HAL_DMA_ConfigChannelAttributes(DMA_HandleTypeDef *h, uint32_t a) { (void)h; (void)a; return HAL_OK; }
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *h) { (void)h; }
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *h) { (void)h; return HAL_OK; }
void HAL_NVIC_SetPriority(int irq, uint32_t pre, uint32_t sub) { (void)irq; (void)pre; (void)sub; }
void HAL_NVIC_EnableIRQ(int irq) { (void)irq; }

/* Bytes off the wire, now: one receive event per 200 bytes, then the scheduler. */
static void type(const char *text)
{
  for (size_t n = strlen(text); n > 0u; ) {
    uint16_t k = (uint16_t)(n < 200u ? n : 200u);
    memcpy(rx_buf, text, k);
    uart.RxEventCallback(&uart, k);
    run_tasks();
    text += k;
    n -= k;
  }
}

/* What the console printed since the last call. */
static const char *printed(void)
{
  static unsigned seen;
  static char     out[sizeof console];

  memcpy(out, console + seen, console_len - seen);
  out[console_len - seen] = '\0';
  seen = console_len;
  return out;
}

/* ---- OpenThread: a stack that attaches at once, and the datagrams it sends ----- */

struct otMessage { unsigned unused; };
static struct otMessage         message;
static char                     ot_instance;
static otDeviceRole             role = OT_DEVICE_ROLE_DISABLED;
static int                      ip6_up;
static otOperationalDatasetTlvs active;
static unsigned                 dns_asked, sent;
static char                     dgram[256];
static unsigned                 dgram_len;

otDeviceRole otThreadGetDeviceRole(otInstance *aInstance) { (void)aInstance; return role; }
const char  *otThreadDeviceRoleToString(otDeviceRole aRole) { return aRole == OT_DEVICE_ROLE_CHILD ? "child" : "disabled"; }
const char  *otThreadErrorToString(otError aError) { return aError == OT_ERROR_NONE ? "OK" : "Failed"; }
otError      otThreadSetEnabled(otInstance *aInstance, bool aEnabled)
{
  (void)aInstance;
  role = aEnabled ? OT_DEVICE_ROLE_CHILD : OT_DEVICE_ROLE_DISABLED;
  return OT_ERROR_NONE;
}
bool    otIp6IsEnabled(otInstance *aInstance) { (void)aInstance; return ip6_up != 0; }
otError otIp6SetEnabled(otInstance *aInstance, bool aEnabled) { (void)aInstance; ip6_up = aEnabled; return OT_ERROR_NONE; }
otError otSetStateChangedCallback(otInstance *aInstance, otStateChangedCallback aCallback, void *aContext)
{
  (void)aInstance; (void)aCallback; (void)aContext;
  return OT_ERROR_NONE;
}

void otDatasetConvertToTlvs(const otOperationalDataset *aDataset, otOperationalDatasetTlvs *aTlvs)
{
  (void)aDataset;
  aTlvs->mLength = 1u;
  aTlvs->mTlvs[0] = 0x0e;
}
otError otDatasetGetActiveTlvs(otInstance *aInstance, otOperationalDatasetTlvs *aTlvs)
{
  (void)aInstance;
  *aTlvs = active;
  return active.mLength > 0u ? OT_ERROR_NONE : OT_ERROR_INVALID_STATE;
}
otError otDatasetSetActiveTlvs(otInstance *aInstance, const otOperationalDatasetTlvs *aTlvs)
{
  (void)aInstance;
  active = *aTlvs;
  return OT_ERROR_NONE;
}
otError otDatasetSetActive(otInstance *aInstance, const otOperationalDataset *aDataset)
{
  (void)aInstance;
  otDatasetConvertToTlvs(aDataset, &active);
  return OT_ERROR_NONE;
}

otError otDnsClientResolveAddress(otInstance *aInstance, const char *aHostName, otDnsAddressCallback aCallback,
                                  void *aContext, const otDnsQueryConfig *aConfig)
{
  (void)aInstance; (void)aHostName; (void)aConfig;
  dns_asked++;
  aCallback(OT_ERROR_NONE, NULL, aContext);
  return OT_ERROR_NONE;
}
otError otDnsAddressResponseGetAddress(const otDnsAddressResponse *aResponse, uint16_t aIndex,
                                       otIp6Address *aAddress, uint32_t *aTtl)
{
  (void)aResponse; (void)aIndex;
  memset(aAddress, 0, sizeof *aAddress);
  *aTtl = 300u;
  return OT_ERROR_NONE;
}
otError otUdpOpen(otInstance *aInstance, otUdpSocket *aSocket, otUdpReceive aCallback, void *aContext)
{
  (void)aInstance; (void)aCallback; (void)aContext;
  aSocket->mOpen = 1;
  return OT_ERROR_NONE;
}
otMessage *otUdpNewMessage(otInstance *aInstance, const otMessageSettings *aSettings)
{
  (void)aInstance; (void)aSettings;
  return &message;
}
otError otMessageAppend(otMessage *aMessage, const void *aBuf, uint16_t aLength)
{
  (void)aMessage;
  if (aLength >= sizeof dgram) { return OT_ERROR_PARSE; }
  memcpy(dgram, aBuf, aLength);
  dgram[aLength] = '\0';
  dgram_len = aLength;
  return OT_ERROR_NONE;
}
otError otUdpSend(otInstance *aInstance, otUdpSocket *aSocket, otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
  (void)aInstance; (void)aSocket; (void)aMessage; (void)aMessageInfo;
  sent++;
  return OT_ERROR_NONE;
}
void otMessageFree(otMessage *aMessage) { (void)aMessage; }

void chirp_dfu_enter(otInstance *inst) { (void)inst; }

/* ---- the die id and the flash ---------------------------------------------------- */

unsigned test_uid = 0x00abcdefu;
#define DIE_ID "wba65-thread-abcdef"

#define FAKE_BASE 0x081FC000u
static unsigned char flash_mem[2][CFG_PAGE_SIZE];

unsigned cfg_flash_page(unsigned i) { return FAKE_BASE + i * CFG_PAGE_SIZE; }
unsigned cfg_flash_slot(void) { return 5u; }
int cfg_flash_read(unsigned addr, void *dst, unsigned n) { memcpy(dst, &flash_mem[0][0] + (addr - FAKE_BASE), n); return 0; }
int cfg_flash_erase(unsigned i) { memset(flash_mem[i], 0xFF, CFG_PAGE_SIZE); return 0; }
int cfg_flash_write(unsigned addr, const unsigned char qw[16])
{
  memcpy(&flash_mem[0][0] + (addr - FAKE_BASE), qw, 16u);
  return 0;
}

/* The flash holding exactly `record`, as an older image may have left it. */
static void stored_raw(const char *record)
{
  static char buf[CFG_MAX_PAYLOAD + 1];
  unsigned    n = 0u;

  (void)cfg_load(buf, sizeof buf, &n);
  expect(cfg_store(record, (unsigned)strlen(record)) == 0, "the record is in the store", record);
}

/* The flash as a push left it: merged by chirpval.c over nothing, and stored. */
static void stored_by_push(const char *push)
{
  static char rec[CFG_MAX_PAYLOAD + 1];
  char        reply[CHIRP_REPLY_MAX];
  unsigned    n = 0u;

  if (!chirp_merge(0, 0u, push, (unsigned)strlen(push), DIE_ID, rec, sizeof rec, &n, reply)) {
    expect(0, "the push is taken", reply);
    return;
  }
  stored_raw(rec);
}

/* Power on: ST's start calls otCliInit, then its scheduler runs. */
static void start(void)
{
  __wrap_otCliInit((otInstance *)(void *)&ot_instance, NULL, NULL);
  run_tasks();
}

/* ---- the scenarios ----------------------------------------------------------------- */

#define DATASET "0e080000000000010000000300001835060004001fffe0020811111111222222220708fd"
#define THREAD  "\"thread\":{\"dataset\":\"" DATASET "\"}"

/* 63 characters, the most a credential holds, alternating " and \, raw and
 * as JSON writes them; and 40 quotation marks. */
static char cred63[64], cred63_json[128], quotes40[41], quotes40_json[81];

static void make_credentials(void)
{
  for (unsigned i = 0u; i < 63u; i++) {
    cred63[i] = (i & 1u) ? '\\' : '"';
    cred63_json[2u * i] = '\\';
    cred63_json[2u * i + 1u] = cred63[i];
  }
  for (unsigned i = 0u; i < 40u; i++) {
    quotes40[i] = '"';
    quotes40_json[2u * i] = '\\';
    quotes40_json[2u * i + 1u] = '"';
  }
}

/* The datagram's credential field, the bytes between the id and the payload. */
static int sent_with(const char *field)
{
  char want[160];
  int  n = snprintf(want, sizeof want, "CHIRP1 " DIE_ID " %s ", field);
  return sent > 0u && dgram_len == (unsigned)n + 2u && memcmp(dgram, want, (size_t)n) == 0;
}

static void blank(void)
{
  start();
  pass_ms(100000u);
  type("CHIRP?\r\n");
  expect(strstr(console, "CHIRP. cfg none") != NULL, "never set up: the start says so", console);
  expect(strstr(console, " key_set=0 ") != NULL, "  ... and the announce says no key", NULL);
  expect(sent == 0u && dns_asked == 0u, "  ... and in 100 s no report goes out, not even a lookup", NULL);
}

static void no_credential(void)
{
  stored_raw("{" THREAD "}");
  start();
  pass_ms(100000u);
  expect(role == OT_DEVICE_ROLE_CHILD, "a stored network and no credential: the board joins", NULL);
  expect(strstr(console, "no credential stored") != NULL && sent == 0u && dns_asked == 0u,
         "  ... and in 100 s no report goes out, and the console says why", console);
}

static void credential_too_long(void)
{
  stored_raw("{" THREAD ",\"token\":\"tttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttt\"}");
  start();
  pass_ms(100000u);
  expect(sent == 0u && dns_asked == 0u, "a stored token of 64, over the bound: in 100 s no report goes out", console);
  expect(strstr(console, "stored credential is unusable") != NULL, "  ... and the console says it cannot use it", NULL);
}

static void token(void)
{
  stored_by_push("{" THREAD ",\"token\":\"tok-1\"}");
  start();
  pass_ms(31000u);
  expect(sent == 1u && sent_with("tok-1"), "a stored token: the first report goes out 30 s after the start", dgram);
}

static void reload(const char *field, const char *json, const char *raw, const char *what)
{
  char push[256], want[128], why[128];

  snprintf(push, sizeof push, "{" THREAD ",\"%s\":\"%s\"}", field, json);
  snprintf(want, sizeof want, "%s%s", field[0] == 'c' ? "claim:" : "", raw);
  stored_by_push(push);
  start();
  pass_ms(31000u);
  snprintf(why, sizeof why, "%s, stored: the next start sends it, every byte", what);
  expect(sent_with(want) && strstr(console, "no credential") == NULL, why, console);
}

static void quotes40_reload(void) { reload("token", quotes40_json, quotes40, "a token of 40 quotation marks"); }
static void cred63_reload(void)   { reload("token", cred63_json, cred63, "a token of 63 quotes and backslashes"); }
static void claim63_reload(void)  { reload("claim", cred63_json, cred63, "a setup code of 63 quotes and backslashes"); }

static void cred63_push(void)
{
  char        line[256];
  const char *answer;

  stored_by_push("{" THREAD ",\"token\":\"tok-1\"}");
  start();
  (void)printed();
  snprintf(line, sizeof line, "CHIRP+ {\"token\":\"%s\"}\r\n", cred63_json);
  type(line);
  answer = printed();
  expect(strncmp(answer, "CHIRP= ok\r\n", 11) == 0 && strstr(answer, "no credential") == NULL,
         "a token of 63 quotes and backslashes, pushed: ok, and nothing says no credential", answer);
  sent = 0u;
  pass_ms(31000u);
  expect(sent_with(cred63) && strstr(console, "no credential") == NULL,
         "  ... and the next report sends it, every byte, with no restart", console);
}

static void split_code(void)
{
  const char *head = "CHIRP+ {" THREAD ",\"claim\":\"";
  char        line[256];

  stored_by_push("{" THREAD ",\"token\":\"tok-1\"}");
  start();
  pass_ms(5000u);                        /* the start has said all it says */
  (void)printed();

  /* Given up just before the code: the rest starts with CHIRP and is read. */
  type(head);
  pass_ms(1500u);
  type("CHIRP-SETUP-4711-ABCD\"}\r\n");
  expect(strcmp(printed(), "CHIRP= err unsupported\r\n") == 0 && cli_len == 0u,
         "a setup line given up just before its code: the rest is answered with nothing of itself", console);

  /* Given up inside the code: the rest is dropped. */
  snprintf(line, sizeof line, "%sCHIRP-SE", head);
  type(line);
  pass_ms(1500u);
  type("TUP-4711-ABCD\"}\r\n");
  expect(printed()[0] == '\0' && cli_len == 0u, "a setup line given up inside its code: the rest prints nothing", console);

  type("CHIRP?\r\n");
  expect(strncmp(printed(), "CHIRP! v=2 hw=" DIE_ID " ", 21) == 0, "  ... and the probe after it is answered", NULL);
  sent = 0u;
  pass_ms(31000u);
  expect(sent_with("tok-1"), "  ... and the board still reports with the token it held", dgram);
}

static const struct { const char *name; void (*run)(void); } scenarios[] = {
  { "blank",               blank },
  { "no-credential",       no_credential },
  { "credential-too-long", credential_too_long },
  { "token",               token },
  { "quotes40-reload",     quotes40_reload },
  { "cred63-reload",       cred63_reload },
  { "claim63-reload",      claim63_reload },
  { "cred63-push",         cred63_push },
  { "split-code",          split_code },
};

#define N_SCENARIOS (sizeof scenarios / sizeof scenarios[0])

int main(int argc, char **argv)
{
  char cmd[1024], line[4096];

  memset(flash_mem, 0xFF, sizeof flash_mem);
  make_credentials();

  /* One scenario, one start. */
  if (argc > 1) {
    for (unsigned i = 0u; i < N_SCENARIOS; i++) {
      if (strcmp(argv[1], scenarios[i].name) == 0) {
        printf("\n--- %s ---\n", scenarios[i].name);
        scenarios[i].run();
        return fails ? 1 : 0;
      }
    }
    printf("FAIL  no scenario named %s\n", argv[1]);
    return 1;
  }

  /* Every scenario, each in a fresh run of this program. */
  for (unsigned i = 0u; i < N_SCENARIOS; i++) {
    FILE *p;
    int   seen_fail = 0;

    snprintf(cmd, sizeof cmd, "\"%s\" %s", argv[0], scenarios[i].name);
#ifdef _WIN32
    for (char *c = cmd; *c != '\0'; c++) { if (*c == '/') { *c = '\\'; } }   /* cmd.exe reads / as a switch */
#endif
    fflush(stdout);
    p = popen(cmd, "r");
    if (p == NULL) { expect(0, "the scenario runs", scenarios[i].name); continue; }
    while (fgets(line, sizeof line, p) != NULL) {
      fputs(line, stdout);
      if (strncmp(line, "ok    ", 6) == 0)  { passes++; }
      if (strncmp(line, "FAIL  ", 6) == 0) { fails++; seen_fail = 1; }
    }
    if (pclose(p) != 0 && !seen_fail) { expect(0, "the scenario ran to its end", scenarios[i].name); }
  }

  printf("\nboottest: %d passed, %d failed\n", passes, fails);
  printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
