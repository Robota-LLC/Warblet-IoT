/* Host check for this demo's real start and its console, with the chip stood
 * in (tools/stub/chip): main() as the reset handler calls it, running the
 * real uart.c, chirp_prov.c, chirp_send.c and the store, on a clock where
 * the UART sends and receives at 115200 and the receiver holds what the
 * chip's does (one byte in RDR).
 *   - no report goes out without a stored credential, and the start names
 *     the id the board answers to;
 *   - a push paced as the setup page sends it, arriving while a report is
 *     printed, is stored whole;
 *   - a byte damaged on the wire, bytes lost while interrupts were masked,
 *     and bytes the receive ring had no room for each throw their line away:
 *     no answer, nothing stored, and the next push lands;
 *   - a probe after a push that stopped is answered, during a report too.
 * Each scenario is one start from reset, so run with no argument the test
 * runs itself once per scenario. Run with make test. What this cannot show:
 * the chip's own timing, the ST-LINK's USB bridge, and real flash. Those are
 * the bench's. */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L   /* popen */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "chirpval.h"
#include "flashcfg.h"

#ifdef _WIN32
#define popen  _popen
#define pclose _pclose
#endif

static int fails, passes;

static void check(int ok, const char *what, const char *detail)
{
  if (ok) {
    passes++;
    printf("ok    %s\n", what);
  } else {
    fails++;
    printf("FAIL  %s%s%s\n", what, detail ? "\n      " : "", detail ? detail : "");
  }
}

/* ---- the firmware under test ------------------------------------------------ */

#define main firmware_main
#include "../src/uart.c"
#include "../src/main.c"
#undef main

static int dfu_entered;
void dfu_enter_system_bootloader(void) { dfu_entered++; }

/* ---- the clock, the wire, the chip -------------------------------------------- */

#define MS            1000000ull     /* nanoseconds */
#define NS_PER_BYTE   86806ull       /* 10 bits at 115200 */
#define NS_PER_ACCESS 500ull         /* the CPU between two register accesses */
#define NS_IDLE_STEP  100000ull      /* ... while nothing is due for a while */
#define TDR_EMPTY     0xFFFFFFFFu

static unsigned long long now_ns, end_ns = 3600000ull * MS;

/* What the host sends: each byte at the time its stop bit ends. */
typedef struct { unsigned long long at; unsigned char c, damaged; } wire_byte;
static wire_byte wire_in[16384];
static unsigned  n_in, next_in;

/* What the board printed, and when its first report was on the wire. */
static char               wire_out[1u << 18];
static unsigned           n_out;
static unsigned long long report_from, report_to;

/* The main loop busy from hold_at for hold_ns, with interrupts masked or not. */
static unsigned long long hold_at = ~0ull, hold_ns;
static int                hold_masks;

static uint32_t rdr_value;
static uint32_t read_rdr(void) { chip_usart1.ISR &= ~USART_ISR_RXNE; return rdr_value; }

RCC_TypeDef   chip_rcc;
GPIO_TypeDef  chip_gpio[4];
USART_TypeDef chip_usart1 = { 0, 0, 0, 0, USART_ISR_TXE | USART_ISR_TC, 0, TDR_EMPTY, read_rdr };
SysTick_Type  chip_systick;

static unsigned long long shift_free_ns, next_tick_ns;
static void             (*usart_irq)(void);
static int                in_irq, masked, tick_pending;

void chip_enable_irq(IRQn_Type irq, void (*handler)(void)) { if (irq == USART1_IRQn) { usart_irq = handler; } }
void NVIC_SetPriority(IRQn_Type irq, uint32_t priority) { (void)irq; (void)priority; }

/* What the firmware wrote since the last access: cleared flags, a byte to send. */
static void sync(void)
{
  USART_TypeDef *u = &chip_usart1;

  if (u->ICR != 0u) {
    u->ISR &= ~(u->ICR & (USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF));
    u->ICR = 0u;
  }
  if (u->TDR != TDR_EMPTY && now_ns >= shift_free_ns) {
    if (n_out + 1u < sizeof wire_out) { wire_out[n_out++] = (char)u->TDR; wire_out[n_out] = '\0'; }
    if (report_from == 0u && n_out >= 15u && strcmp(wire_out + n_out - 15u, "CHIRP. uplink L") == 0) {
      report_from = now_ns - 14u * NS_PER_BYTE;
    }
    if (report_from != 0u && report_to == 0u && wire_out[n_out - 1u] == '\n' && strstr(wire_out, "CHIRP^ body ")) {
      report_to = now_ns + NS_PER_BYTE;
    }
    shift_free_ns = now_ns + NS_PER_BYTE;
    u->TDR = TDR_EMPTY;
  }
  u->ISR &= ~(USART_ISR_TXE | USART_ISR_TC);
  if (u->TDR == TDR_EMPTY) { u->ISR |= USART_ISR_TXE | (now_ns >= shift_free_ns ? USART_ISR_TC : 0u); }
}

static void take_interrupts(void)
{
  if (in_irq || masked) { return; }
  if (tick_pending) { tick_pending = 0; in_irq = 1; SysTick_Handler(); in_irq = 0; }
  if (usart_irq != NULL && (chip_usart1.CR1 & USART_CR1_RXNEIE) &&
      (chip_usart1.ISR & (USART_ISR_RXNE | USART_ISR_ORE))) {
    in_irq = 1; usart_irq(); sync(); in_irq = 0;
  }
}

/* A byte arrives: RDR holds one; with no room it is lost and ORE says so. */
static void byte_arrives(const wire_byte *b)
{
  USART_TypeDef *u = &chip_usart1;

  if ((u->CR1 & (USART_CR1_UE | USART_CR1_RE)) != (USART_CR1_UE | USART_CR1_RE)) { return; }
  if (u->ISR & USART_ISR_RXNE) { u->ISR |= USART_ISR_ORE; return; }
  rdr_value = b->damaged ? 0x7Eu : b->c;     /* a damaged byte reads as another */
  u->ISR |= USART_ISR_RXNE | (b->damaged ? USART_ISR_FE : 0u);
}

static int systick_on(void)
{
  uint32_t on = SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk;
  return (chip_systick.CTRL & on) == on;
}

/* Time passes to t_end: bytes arrive, bytes leave, SysTick ticks. */
static void run_until(unsigned long long t_end)
{
  while (now_ns < t_end) {
    unsigned long long t = t_end;

    if (systick_on() && next_tick_ns == 0u) { next_tick_ns = now_ns + MS; }
    if (next_in < n_in && wire_in[next_in].at < t) { t = wire_in[next_in].at; }
    if (systick_on() && next_tick_ns < t) { t = next_tick_ns; }
    if (chip_usart1.TDR != TDR_EMPTY && shift_free_ns > now_ns && shift_free_ns < t) { t = shift_free_ns; }
    now_ns = t;
    sync();
    while (next_in < n_in && wire_in[next_in].at <= now_ns) {
      byte_arrives(&wire_in[next_in++]);
      take_interrupts();
    }
    if (systick_on() && next_tick_ns <= now_ns) {
      next_tick_ns += MS;
      tick_pending = 1;
    }
    take_interrupts();
  }
}

static void (*scenario_check)(void);

static void finish(void)
{
  scenario_check();
  fflush(stdout);
  exit(fails ? 1 : 0);
}

/* Every register access: a little time passes, or a lot while nothing is due. */
void *chip_access(void *peripheral)
{
  unsigned long long step = NS_PER_ACCESS;

  if (in_irq) { sync(); return peripheral; }
  if (now_ns >= hold_at) {
    unsigned long long until = hold_at + hold_ns;
    hold_at = ~0ull;
    masked  = hold_masks;
    run_until(until);
    masked  = 0;
    take_interrupts();
  }
  if (now_ns >= end_ns) { finish(); }
  if (chip_usart1.TDR == TDR_EMPTY && now_ns >= shift_free_ns && !(chip_usart1.ISR & USART_ISR_RXNE) &&
      (next_in >= n_in || wire_in[next_in].at > now_ns + MS + NS_IDLE_STEP)) {
    step = NS_IDLE_STEP;
  }
  run_until(now_ns + step);
  return peripheral;
}

/* ---- what the host sends ------------------------------------------------------ */

/* Bytes back to back from at, the one at index `damaged` hit by noise (-1:
 * none). Returns when the last one has arrived. */
static unsigned long long host_sends(unsigned long long at, const char *text, int damaged)
{
  for (int i = 0; text[i] != '\0' && n_in < sizeof wire_in / sizeof wire_in[0]; i++) {
    at += NS_PER_BYTE;
    wire_in[n_in].at = at;
    wire_in[n_in].c = (unsigned char)text[i];
    wire_in[n_in].damaged = (unsigned char)(i == damaged);
    n_in++;
  }
  return at;
}

/* As the setup page sends a line: 64 bytes, then 30 ms, until it is out. */
static unsigned long long page_sends(unsigned long long at, const char *text, int damaged)
{
  char   piece[65];
  size_t n = strlen(text);

  for (size_t i = 0u; i < n; i += 64u) {
    snprintf(piece, sizeof piece, "%.*s", (int)(n - i < 64u ? n - i : 64u), text + i);
    at = host_sends(at, piece, damaged - (int)i);
    if (i + 64u < n) { at += 30u * MS; }
  }
  return at;
}

/* ---- the die id and the flash ------------------------------------------------- */

unsigned test_uid = 0x00abcdefu;
#define DIE_ID "wba65-demo-abcdef"

#define FAKE_BASE 0x081FC000u
static unsigned char flash_mem[2][CFG_PAGE_SIZE];

unsigned cfg_flash_page(unsigned i) { return FAKE_BASE + i * CFG_PAGE_SIZE; }
int cfg_flash_read(unsigned addr, void *dst, unsigned n) { memcpy(dst, &flash_mem[0][0] + (addr - FAKE_BASE), n); return 0; }
int cfg_flash_erase(unsigned i) { memset(flash_mem[i], 0xFF, CFG_PAGE_SIZE); return 0; }
int cfg_flash_write(unsigned addr, const unsigned char qw[16])
{
  memcpy(&flash_mem[0][0] + (addr - FAKE_BASE), qw, 16u);
  return 0;
}

/* The flash holding exactly `record`, and RAM as a reset leaves it: the
 * store's RAM copy is emptied again by a load from a blank flash. */
static void board_holds(const char *record)
{
  static unsigned char kept[2][CFG_PAGE_SIZE];

  (void)cfg_load();
  check(cfg_store(record, (unsigned)strlen(record)) == 0, "the record is in the store", record);
  memcpy(kept, flash_mem, sizeof kept);
  memset(flash_mem, 0xFF, sizeof flash_mem);
  (void)cfg_load();
  memcpy(flash_mem, kept, sizeof kept);
}

/* `push` merged over `record` (NULL: nothing stored), as the board would store it. */
static const char *merged(const char *record, const char *push, const char *id)
{
  static char out[CFG_MAX_PAYLOAD + 1];
  char        reply[CHIRP_REPLY_MAX];
  unsigned    n = 0u;

  if (!chirp_merge(record, record ? (unsigned)strlen(record) : 0u, push, (unsigned)strlen(push), id,
                   out, sizeof out, &n, reply)) {
    check(0, "the push is one the board takes", reply);
    out[0] = '\0';
  }
  return out;
}

/* ---- the scenarios -------------------------------------------------------------- */

#define KEY_A  "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="
#define TOK63  "t00000000011111111112222222222333333333344444444445555555555666"
#define ID63   "board-000011111111112222222222333333333344444444445555555555666"
#define HOST63 "h0000000001111111111222222222233333333334444444444555.example"

/* Every value long, and signed: the longest report, about 470 bytes, 41 ms on the wire. */
static const char HEAVY[] = "{\"token\":\"" TOK63 "\",\"hwid\":\"" ID63 "\",\"host\":\"" HOST63 "\",\"key\":\"" KEY_A "\"}";

static char        want_record[CFG_MAX_PAYLOAD + 1];
static unsigned long long push_from, push_to;   /* when its first byte comes, and its last */

/* The board's answers ("CHIRP= ...") printed at or after byte `from` of the output. */
static int answers_from(unsigned from, char *first, size_t n)
{
  int count = 0;

  first[0] = '\0';
  for (const char *p = wire_out + from; (p = strstr(p, "CHIRP= ")) != NULL; p++) {
    if (count++ == 0) { snprintf(first, n, "%.*s", (int)strcspn(p, "\r\n"), p); }
  }
  return count;
}

/* -- the start, with and without a credential -- */

static void check_no_report(void)
{
  check(strstr(wire_out, "CHIRP^") == NULL && strstr(wire_out, "no uplink: no credential stored yet") != NULL,
        "in 100 s no report goes out, and the console says why", wire_out);
}

static void blank(void)
{
  check(strstr(wire_out, "CHIRP. cfg none") != NULL, "never set up: the start says so", wire_out);
  check_no_report();
}

static void no_credential_setup(void)       { board_holds("{\"ssid\":\"cafe\",\"pass\":\"p\"}"); }
static void credential_too_long_setup(void)
{
  board_holds("{\"token\":\"tttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttt\"}");
}

static void token_setup(void) { board_holds(merged(NULL, "{\"token\":\"tok-1\",\"hwid\":\"board-7\"}", DIE_ID)); end_ns = 31000u * MS; }
static void token(void)
{
  check(strstr(wire_out, "boot hw=board-7 ") != NULL, "the start names the id the board answers to", wire_out);
  check(strstr(wire_out, "CHIRP^ POST /ingest/board-7 HTTP/1.1\r\n") != NULL &&
        strstr(wire_out, "CHIRP^ X-Chirp-Token: tok-1\r\n") != NULL,
        "a stored token: a report goes out 30 s after the start", NULL);
}

/* -- a paced push while a report is printed: the report comes 30 s after the
 *    start; the push starts at 29.825 s + 25 ms * step, so its pieces meet the
 *    report at every point of it -- */

static char paced_line[1024];

static void paced_setup_at(int step)
{
  static char json[900];
  char        ssid[33], pass[65], tok[64], ds[201];

  memset(ssid, 's', 32); ssid[32] = '\0';
  for (int i = 0; i < 64; i++) { pass[i] = "abcdefghijklmnopqrstuvwxyz012345"[i % 32]; }
  pass[64] = '\0';
  memset(tok, 'n', 63); tok[63] = '\0';
  for (int i = 0; i < 200; i++) { ds[i] = "0123456789abcdef"[i % 16]; }
  ds[200] = '\0';
  snprintf(json, sizeof json, "{\"ssid\":\"%s\",\"pass\":\"%s\",\"token\":\"%s\",\"thread\":{\"dataset\":\"%s\"}}",
           ssid, pass, tok, ds);
  snprintf(paced_line, sizeof paced_line, "CHIRP+ %s\r\n", json);
  board_holds(HEAVY);
  snprintf(want_record, sizeof want_record, "%s", merged(HEAVY, json, ID63));
  push_from = 29825u * MS + (unsigned long long)step * 25u * MS;
  push_to = page_sends(push_from, paced_line, -1);
  end_ns = push_from + 1000u * MS;
}

static void paced(void)
{
  char        first[96], why[200];
  unsigned    len = 0u;
  const char *held = cfg_json(&len);
  int         n = answers_from(0u, first, sizeof first);

  snprintf(why, sizeof why, "a paced push from %u ms, meeting the report: answered ok, stored whole",
           (unsigned)(push_from / MS));
  check(report_from < push_to && push_from < report_to, "  (the report was on the wire while the push came)", NULL);
  {
    char detail[256];
    snprintf(detail, sizeof detail, "answer \"%s\" (%d answer%s); %s", first, n, n == 1 ? "" : "s",
             held && strcmp(held, want_record) == 0 ? "stored whole" :
             held && strcmp(held, HEAVY) == 0 ? "nothing stored" : "a different record stored");
    check(n == 1 && strcmp(first, "CHIRP= ok") == 0 && held != NULL && strcmp(held, want_record) == 0, why, detail);
  }
}

#define PACED(k) static void paced_##k(void) { paced_setup_at(k); }
PACED(0) PACED(1) PACED(2) PACED(3) PACED(4) PACED(5) PACED(6) PACED(7) PACED(8) PACED(9)

/* -- a line that loses bytes is thrown away, and the next push lands -- */

static const char NEXT_PUSH[] = "{\"hwid\":\"board-8\"}";

static void damaged_then_next(const char *line, int damaged, int paced)
{
  static char next[64];

  board_holds(HEAVY);
  snprintf(want_record, sizeof want_record, "%s", merged(HEAVY, NEXT_PUSH, ID63));
  push_from = 5000u * MS;
  if (paced) { (void)page_sends(push_from, line, damaged); } else { (void)host_sends(push_from, line, damaged); }
  snprintf(next, sizeof next, "CHIRP+ %s\r\n", NEXT_PUSH);
  (void)page_sends(7000u * MS, next, -1);
  end_ns = 8000u * MS;
}

static void check_dropped(const char *what)
{
  char        first[96], detail[256];
  unsigned    len = 0u;
  const char *held = cfg_json(&len);
  int         n = answers_from(0u, first, sizeof first);

  snprintf(detail, sizeof detail, "%d answer%s, the first \"%s\"; %s", n, n == 1 ? "" : "s", first,
           held && strcmp(held, want_record) == 0 ? "the next push stored, nothing else" : "another record stored");
  check(n == 1 && strcmp(first, "CHIRP= ok") == 0 && held != NULL && strcmp(held, want_record) == 0, what, detail);
}

static void noise_setup(void)
{
  damaged_then_next("CHIRP+ {\"token\":\"tok-noise-0123456789\"}\r\n", 20, 1);
}
static void noise(void) { check_dropped("a byte damaged on the wire: that push gets no answer and is not stored; the next lands"); }

static void masked_setup(void)
{
  damaged_then_next("CHIRP+ {\"token\":\"" TOK63 "\",\"pass\":\"0123456789012345678901234567890123456789\"}\r\n", -1, 0);
  hold_at = push_from + 3u * MS;         /* a flash erase masks interrupts for ms */
  hold_ns = 5u * MS;
  hold_masks = 1;
}
static void masked_overrun(void)
{
  check_dropped("bytes lost while interrupts were masked: that push gets no answer and is not stored; the next lands");
}

static void ring_setup(void)
{
  static char line[2048];
  char       *p = line;

  p += sprintf(p, "CHIRP+ {\"aps\":[");
  for (int i = 0; i < 8; i++) {
    p += sprintf(p, "%s{\"ssid\":\"net-%d-0123456789012345678901234\",\"pass\":\""
                    "pass-%d-012345678901234567890123456789012345678901234567890123\"}", i ? "," : "", i, i);
  }
  p += sprintf(p, "],\"thread\":{\"dataset\":\"");
  for (int i = 0; i < 508; i++) { *p++ = "0123456789abcdef"[i % 16]; }
  sprintf(p, "\"}}\r\n");
  damaged_then_next(line, -1, 0);

  /* The main loop held up longer than the ring covers (1023 bytes, 89 ms),
   * and back before the line ends, so its tail and line ending come after. */
  hold_at = push_from + 2u * MS;
  hold_ns = strlen(line) * NS_PER_BYTE - 20u * MS;
  hold_masks = 0;
  check(hold_ns > 1023u * NS_PER_BYTE + 10u * MS, "  (the hold is longer than the ring covers)", NULL);
}
static void ring_full(void)
{
  check_dropped("a push at full speed while the main loop is held: the ring fills, the push is "
                "thrown away unanswered; the next lands");
}

/* -- a probe after a push that stopped -- */

static unsigned long long probe_ns;

static void stall_setup_at(unsigned long long probe_at)
{
  probe_ns = probe_at;
  board_holds(HEAVY);
  push_from = (probe_at > 3000u * MS) ? probe_at - 3000u * MS : 0u;
  (void)page_sends(push_from, "CHIRP+ {\"token\":\"tok-stalled\",\"hwid\":\"board-st", -1);
  (void)host_sends(probe_at, "CHIRP?\r\n", -1);
  end_ns = probe_at + 500u * MS;
}

static void stall_setup(void)  { stall_setup_at(5000u * MS); }
static void stall_report_setup(void) { stall_setup_at(30050u * MS); }   /* while the report prints */

static void stalled(void)
{
  check(strstr(wire_out, "CHIRP! v=2 hw=" ID63 " ") != NULL && strstr(wire_out, "bad-json") == NULL,
        "a push stopped, and 1.5 s or more later the page's probe: answered, no bad-json", wire_out);
}

static void stalled_report(void)
{
  const char *body = strstr(wire_out, "CHIRP^ body ");

  check(report_from < probe_ns && probe_ns < report_to, "  (the probe came while the report was on the wire)", NULL);
  check(body != NULL && strstr(body, "CHIRP! v=2 ") != NULL && strstr(wire_out, "bad-json") == NULL,
        "  ... and the same probe arriving while a report is printed: answered after it", wire_out);
}

static const struct {
  const char *name;
  void      (*setup)(void);
  void      (*check)(void);
} scenarios[] = {
  { "blank",               0,                         blank },
  { "no-credential",       no_credential_setup,       check_no_report },
  { "credential-too-long", credential_too_long_setup, check_no_report },
  { "token",               token_setup,               token },
  { "paced-0",  paced_0,  paced }, { "paced-1",  paced_1,  paced }, { "paced-2",  paced_2,  paced },
  { "paced-3",  paced_3,  paced }, { "paced-4",  paced_4,  paced }, { "paced-5",  paced_5,  paced },
  { "paced-6",  paced_6,  paced }, { "paced-7",  paced_7,  paced }, { "paced-8",  paced_8,  paced },
  { "paced-9",  paced_9,  paced },
  { "noise",               noise_setup,               noise },
  { "masked-overrun",      masked_setup,              masked_overrun },
  { "ring-full",           ring_setup,                ring_full },
  { "stall",               stall_setup,               stalled },
  { "stall-report",        stall_report_setup,        stalled_report },
};

#define N_SCENARIOS (sizeof scenarios / sizeof scenarios[0])

int main(int argc, char **argv)
{
  char cmd[1024], line[8192];

  memset(flash_mem, 0xFF, sizeof flash_mem);
  chip_gpio[2].IDR = 1u << BTN_PIN;      /* B1 not pressed (active low) */
  end_ns = 100000u * MS;

  /* One scenario: set it up, then reset into the firmware; finish() ends it. */
  if (argc > 1) {
    for (unsigned i = 0u; i < N_SCENARIOS; i++) {
      if (strcmp(argv[1], scenarios[i].name) == 0) {
        printf("\n--- %s ---\n", scenarios[i].name);
        if (scenarios[i].setup != 0) { scenarios[i].setup(); }
        scenario_check = scenarios[i].check;
        (void)firmware_main();
        check(0, "the firmware's main loop ended", NULL);
        return 1;
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
    if (p == NULL) { check(0, "the scenario runs", scenarios[i].name); continue; }
    while (fgets(line, sizeof line, p) != NULL) {
      fputs(line, stdout);
      if (strncmp(line, "ok    ", 6) == 0)  { passes++; }
      if (strncmp(line, "FAIL  ", 6) == 0) { fails++; seen_fail = 1; }
    }
    if (pclose(p) != 0 && !seen_fail) { check(0, "the scenario ran to its end", scenarios[i].name); }
  }

  printf("\nboottest: %d passed, %d failed\n", passes, fails);
  printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
