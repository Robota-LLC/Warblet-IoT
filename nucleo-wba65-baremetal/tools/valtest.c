/* Host checks for this demo's own code, with the board stood in (the UART, the
 * die id and the flash here, tools/stub/board.h for the rest):
 *   - every push of the shared CHIRP-PROV v1.8 vectors' relay profile, fed a
 *     byte at a time as a console line: the answer, whether it is stored, and
 *     the settings held after, read back as the next start reads them;
 *   - the rules behind them one by one: the refusals, the Thread object, the
 *     id, the credential, the longest record;
 *   - the announce: v1.8's line, with this demo's slug, fw and capabilities
 *     as demo.json declares them;
 *   - the store: two pages; a save whose flash fails at any step, or loses
 *     power at any step, and the newest valid record winning at start;
 *   - the line: one over the limit, a line that stops, stray bytes in front;
 *   - no request without a credential;
 *   - JSON, SHA-256, HMAC and the signed input.
 * Run with make test VECTORS=<path to chirp-prov-v1.8.json>; the vectors live
 * with Warblet's checks, not in this folder. What this cannot show: a real
 * start, real flash and its ECC, and the console itself. Those are the bench's. */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "chirpval.h"
#include "flashcfg.h"
#include "sha256.h"
#include "chirp_sign.h"

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

/* ---- the board's parts ------------------------------------------------------ */

unsigned test_uid = 0x00abcdefu;         /* the die id: wba65-demo-abcdef */
#define DIE_ID "wba65-demo-abcdef"

/* What the firmware prints, kept for the checks; and one byte waiting to be read. */
static char     said[65536];
static unsigned said_len;
static int      rx = -1;

void uart_init(void) { }
void uart_putc(char c) { if (said_len + 1u < sizeof said) { said[said_len++] = c; said[said_len] = '\0'; } }
void uart_write(const char *s, unsigned n) { for (unsigned i = 0; i < n; i++) { uart_putc(s[i]); } }
void uart_puts(const char *s) { while (*s) { uart_putc(*s++); } }
void uart_line(const char *s) { uart_puts(s); uart_puts("\r\n"); }
void uart_put_uint(unsigned v) { char b[16]; snprintf(b, sizeof b, "%u", v); uart_puts(b); }
void uart_put_hex(unsigned v, int digits) { char b[16]; snprintf(b, sizeof b, "%0*x", digits, v); uart_puts(b); }
int  uart_getc(void) { int c = rx; rx = -1; return c; }
void uart_flush(void) { }

static int dfu_entered;
void dfu_enter_system_bootloader(void) { dfu_entered++; }

/* ---- the flash, two 8 KB pages, with faults ---------------------------------- */
/* Erase sets a page to 0xFF; a 16-byte write needs those bytes erased, as on
 * the chip. A power cut stops everything until power_on(); the step it cuts is
 * left half done (a half-erased page, a torn quadword whose ECC fails on read)
 * or not done at all. One failed operation fails in one of several ways. */

#define FAKE_BASE 0x081FC000u
static unsigned char flash_mem[2][CFG_PAGE_SIZE];
static unsigned char flash_torn[2][CFG_PAGE_SIZE / 16u];

enum { CUT_TORN, CUT_CLEAN };
enum { FAIL_NOTHING, FAIL_HALF, FAIL_LANDED, FAIL_LIES, FAIL_KINDS };
static int  steps;                   /* mutating steps since power_on() */
static int  cut_at = -1, cut_mode;   /* the step the power goes at; -1 never */
static int  fail_at = -1, fail_kind; /* the step that fails; -1 none */
static int  power_gone;

static void power_on(void) { power_gone = 0; cut_at = fail_at = -1; steps = 0; }

static void flash_blank(void)
{
  memset(flash_mem, 0xFF, sizeof flash_mem);
  memset(flash_torn, 0, sizeof flash_torn);
  power_on();
}

unsigned cfg_flash_page(unsigned i) { return FAKE_BASE + i * CFG_PAGE_SIZE; }

static unsigned char *at(unsigned addr, unsigned *page, unsigned *off)
{
  *page = (addr - FAKE_BASE) / CFG_PAGE_SIZE;
  *off  = (addr - FAKE_BASE) % CFG_PAGE_SIZE;
  return &flash_mem[*page][*off];
}

int cfg_flash_read(unsigned addr, void *dst, unsigned n)
{
  unsigned page, off;
  unsigned char *p = at(addr, &page, &off);

  if (power_gone) { return -2; }
  for (unsigned i = 0; i < n; i++) {
    if (flash_torn[page][(off + i) / 16u]) { return -2; }
  }
  memcpy(dst, p, n);
  return 0;
}

static void erase_half(unsigned i) { memset(flash_mem[i], 0xFF, CFG_PAGE_SIZE / 2u); }

int cfg_flash_erase(unsigned i)
{
  int s;

  if (power_gone) { return -1; }
  s = steps++;
  if (s == cut_at) {
    if (cut_mode == CUT_TORN) { erase_half(i); }
    power_gone = 1;
    return -1;
  }
  if (s == fail_at && (fail_kind == FAIL_NOTHING || fail_kind == FAIL_HALF)) {
    if (fail_kind == FAIL_HALF) { erase_half(i); }
    return -1;
  }
  memset(flash_mem[i], 0xFF, CFG_PAGE_SIZE);
  memset(flash_torn[i], 0, sizeof flash_torn[i]);
  return 0;
}

int cfg_flash_write(unsigned addr, const unsigned char qw[16])
{
  unsigned page, off, k;
  unsigned char *p = at(addr, &page, &off);
  int s;

  if (power_gone) { return -1; }
  s = steps++;
  for (k = 0; k < 16u; k++) { if (p[k] != 0xFF || flash_torn[page][off / 16u]) { return -1; } }
  if (s == cut_at) {
    if (cut_mode == CUT_TORN) { memset(p, 0x5A, 16u); flash_torn[page][off / 16u] = 1; }
    power_gone = 1;
    return -1;
  }
  if (s == fail_at) {
    switch (fail_kind) {
      case FAIL_NOTHING: return -1;
      case FAIL_HALF:    memset(p, 0x5A, 16u); flash_torn[page][off / 16u] = 1; return -1;
      case FAIL_LANDED:  memcpy(p, qw, 16u); return -1;       /* written, but reported failed */
      case FAIL_LIES:    memcpy(p, qw, 16u); p[3] ^= 0x10u; return 0;   /* a bit wrong, reported fine */
    }
  }
  memcpy(p, qw, 16u);
  return 0;
}

/* ---- the firmware under test --------------------------------------------------- */

#include "../src/chirp_prov.c"

static unsigned clock_ms = 1000u;

/* A real start: RAM forgotten, the store read, the credentials loaded. */
static void start(void)
{
  power_on();
  s_len = 0u; s_marked = s_skip = 0;
  (void)cfg_load();
  chirp_send_reload();
}

static void send_text(const char *text, unsigned gap_ms)
{
  for (const char *c = text; *c != '\0'; c++) {
    clock_ms += gap_ms;
    rx = (unsigned char)*c;
    chirp_prov_poll(clock_ms);
  }
}

/* The first line the firmware printed for a console line ("" for none). */
static char first_line[1024];
static const char *answer_to(const char *line)
{
  char *nl;
  said_len = 0u;
  said[0] = '\0';
  send_text(line, 1u);
  snprintf(first_line, sizeof first_line, "%s", said);
  if ((nl = strpbrk(first_line, "\r\n")) != NULL) { *nl = '\0'; }
  return first_line;
}

static const char *push(const char *json)
{
  static char line[CHIRP_LINE_MAX + 16];
  snprintf(line, sizeof line, "CHIRP+ %s\r\n", json);
  return answer_to(line);
}

/* A board that holds `record` (NULL: never set up), just started. */
static void board_with(const char *record)
{
  flash_blank();
  (void)cfg_load();
  if (record != NULL) { (void)cfg_store(record, (unsigned)strlen(record)); }
  start();
}

/* ---- JSON compared as values ------------------------------------------------------ */

static int decoded(const js_val *v, char *buf, unsigned n)
{
  return js_str_copy(v, buf, n);
}

static int same_obj(const char *a, unsigned an, const char *b, unsigned bn, int top);

/* Strings by their decoded bytes, numbers by their text, objects in any order,
 * arrays in order. */
static int same_value(const js_val *a, const js_val *b)
{
  static char x[4096], y[4096];
  if (a->t != b->t) { return 0; }
  switch (a->t) {
    case JS_STR: {
      int nx = decoded(a, x, sizeof x), ny;
      if (nx < 0) { return 0; }
      {
        static char xs[4096];
        memcpy(xs, x, (size_t)nx);
        ny = decoded(b, y, sizeof y);
        return ny == nx && memcmp(xs, y, (size_t)nx) == 0;
      }
    }
    case JS_NUM: return a->n == b->n && memcmp(a->p, b->p, a->n) == 0;
    case JS_OBJ: return same_obj(a->p, a->n, b->p, b->n, 0);
    case JS_ARR: {
      unsigned pa = 0u, pb = 0u;
      js_val   ea, eb;
      int      ra, rb;
      for (;;) {
        ra = js_arr_next(a->p, a->n, &pa, &ea);
        rb = js_arr_next(b->p, b->n, &pb, &eb);
        if (ra != rb) { return 0; }
        if (ra != 1) { return ra == 0; }
        if (!same_value(&ea, &eb)) { return 0; }
      }
    }
    default: return 1;
  }
}

/* A member's name; at the top of a record a claim counts as the token, as the
 * vectors write a setup code. */
static void member_name(const js_val *k, int top, char name[64])
{
  if (js_str_copy(k, name, 64) < 0) { name[0] = '\0'; }
  if (top && strcmp(name, "claim") == 0) { strcpy(name, "token"); }
}

static int same_obj(const char *a, unsigned an, const char *b, unsigned bn, int top)
{
  unsigned pa = 0u, count_a = 0u, count_b = 0u, pb;
  js_val   ka, va, kb, vb;
  char     na[64], nb[64];

  pb = 0u;
  while (js_obj_next(b, bn, &pb, &kb, &vb) == 1) { count_b++; }
  while (js_obj_next(a, an, &pa, &ka, &va) == 1) {
    int found = 0;
    count_a++;
    member_name(&ka, top, na);
    pb = 0u;
    while (!found && js_obj_next(b, bn, &pb, &kb, &vb) == 1) {
      member_name(&kb, top, nb);
      if (strcmp(na, nb) == 0) { found = same_value(&va, &vb); if (!found) { return 0; } }
    }
    if (!found) { return 0; }
  }
  return count_a == count_b;
}

static int same_record(const char *a, const char *b)
{
  return same_obj(a, (unsigned)strlen(a), b, (unsigned)strlen(b), 1);
}

/* What the next start reads, as text ("{}" when nothing). */
static const char *held_after_restart(void)
{
  static char copy[CFG_MAX_PAYLOAD + 1];
  unsigned    n = 0u;
  const char *j;

  start();
  j = cfg_json(&n);
  snprintf(copy, sizeof copy, "%s", j ? j : "{}");
  return copy;
}

static char *read_file(const char *path)
{
  FILE *f = fopen(path, "rb");
  long  n;
  char *text;

  if (f == NULL) { return NULL; }
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  text = malloc((size_t)n + 1u);
  if (text != NULL && fread(text, 1, (size_t)n, f) == (size_t)n) { text[n] = '\0'; }
  else if (text != NULL) { free(text); text = NULL; }
  fclose(f);
  return text;
}

/* A string member of obj into buf ("" when absent). */
static const char *str_of(const js_val *obj, const char *key, char *buf, unsigned n)
{
  js_val v;
  buf[0] = '\0';
  if (js_obj_get(obj->p, obj->n, key, &v) == 1 && v.t == JS_STR && js_str_copy(&v, buf, n) < 0) { buf[0] = '\0'; }
  return buf;
}

/* The object's source text into buf, NULL for {}. */
static const char *obj_text(const js_val *obj, const char *key, char *buf, unsigned n)
{
  js_val   v, k2, v2;
  unsigned pos = 0u;

  if (js_obj_get(obj->p, obj->n, key, &v) != 1 || v.t != JS_OBJ || v.n + 1u > n) { return NULL; }
  if (js_obj_next(v.p, v.n, &pos, &k2, &v2) != 1) { return NULL; }
  memcpy(buf, v.p, v.n);
  buf[v.n] = '\0';
  return buf;
}

/* ---- the shared vectors ----------------------------------------------------------- */

static char *s_vectors;

static void test_vectors(const char *path)
{
  js_val   root, pushes, v;
  unsigned pos = 0u;
  int      n = 0;
  static char name[512], pushed[8192], want[128], stored[4096], after[4096], detail[2048];

  puts("\n--- the shared CHIRP-PROV v1.8 vectors, relay profile (this board) ---");
  if (path == NULL) {
    printf("SKIP  the shared vectors: no path given (make test VECTORS=<path to chirp-prov-v1.8.json>)\n");
    return;
  }
  s_vectors = read_file(path);
  check(s_vectors != NULL, "the shared vectors read", path);
  if (s_vectors == NULL) { return; }
  root.p = s_vectors; root.n = (unsigned)strlen(s_vectors); root.t = JS_OBJ;
  if (js_obj_get(root.p, root.n, "pushes", &pushes) != 1 || pushes.t != JS_ARR) {
    check(0, "the vectors hold pushes", path);
    return;
  }
  while (js_arr_next(pushes.p, pushes.n, &pos, &v) == 1) {
    char        profile[32];
    const char *st, *af, *got, *held;
    js_val      stores;
    int         will_store, stored_ok;
    char        before[CFG_MAX_PAYLOAD + 1];

    if (strcmp(str_of(&v, "profile", profile, sizeof profile), "relay") != 0) { continue; }
    str_of(&v, "name", name, sizeof name);
    str_of(&v, "push", pushed, sizeof pushed);
    str_of(&v, "answer", want, sizeof want);
    will_store = js_obj_get(v.p, v.n, "stores", &stores) == 1 && stores.t == JS_TRUE;
    st = obj_text(&v, "stored", stored, sizeof stored);
    af = obj_text(&v, "after", after, sizeof after);

    /* The vector's settings, held as the board would hold them. */
    board_with(st);
    stored_ok = st == NULL || cfg_json(NULL) != NULL;
    check(stored_ok, "the stored settings load", name);
    snprintf(before, sizeof before, "%s", cfg_json(NULL) ? cfg_json(NULL) : "{}");

    got = push(pushed);
    snprintf(detail, sizeof detail, "%s\n      got  %s\n      want %s", name, got, want);
    check(strcmp(got, want) == 0, "a v1.8 push gets the contract's answer", detail);

    held = held_after_restart();
    if (will_store) {
      snprintf(detail, sizeof detail, "%s\n      held %s\n      want %s", name, held, af ? af : "{}");
      check(same_record(held, af ? af : "{}"), "  ... and the next start reads the vector's settings", detail);
    } else {
      snprintf(detail, sizeof detail, "%s\n      held %s", name, held);
      check(strcmp(held, before) == 0 && same_record(held, af ? af : "{}"),
            "  ... and a refusal leaves the record as it was", detail);
    }
    n++;
  }
  check(n >= 20, "the vectors hold the relay profile's pushes", NULL);
  printf("      %d relay pushes answered and stored as the contract says\n", n);
}

/* ---- the rules one by one --------------------------------------------------------- */

#define KEY_A "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="
#define KEY_B "BBECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="
#define TKEY  "00112233445566778899aabbccddeeff"

static void expect(const char *record, const char *json, const char *want, const char *why)
{
  char detail[1024];
  const char *got;

  board_with(record);
  got = push(json);
  snprintf(detail, sizeof detail, "got %s\n      push %s", got, json);
  check(strcmp(got, want) == 0, why, detail);
}

/* The push is taken, and the next start reads `want`. */
static void expect_held(const char *record, const char *json, const char *want, const char *why)
{
  char detail[2048];
  const char *got, *held;

  board_with(record);
  got = push(json);
  held = held_after_restart();
  snprintf(detail, sizeof detail, "answer %s\n      held %s\n      want %s", got, held, want);
  check(strcmp(got, "CHIRP= ok") == 0 && same_record(held, want), why, detail);
}

static void test_rules(void)
{
  const char *T = "{\"token\":\"tok\"}";
  static char big[CHIRP_LINE_MAX];

  puts("\n--- refusals (CHIRP-PROV v1.8) ---");
  /* These two were answered ok before v1.8. */
  expect(NULL, "{\"ssid\":\"workshop\",\"pass\":\"hunter2\",\"aps\":[{\"ssid\":\"workshop\",\"pass\":\"\"}],"
         "\"host\":\"h\",\"claim\":\"C\",\"level\":1}", "CHIRP= err unsupported level",
         "a push with level is refused whole, naming level");
  expect(NULL, "{\"claim\":\"C\",\"thread\":{\"name\":\"n\",\"key\":\"" TKEY "\"},\"unknown\":{\"deep\":[1,2,3]}}",
         "CHIRP= err unsupported unknown", "an unknown field is refused by name");
  expect(T, "{\"tls\":true,\"level\":1}", "CHIRP= err unsupported level", "of two, the name that sorts first");
  expect(T, "{\"zeta\":1,\"Zeta\":1}", "CHIRP= err unsupported Zeta", "  ... byte by byte: Z before z");
  expect(T, "{\"ssid\":5,\"level\":1}", "CHIRP= err unsupported level", "unsupported comes before a bad value");
  expect(T, "{\"two words\":1}", "CHIRP= err bad-json", "a name a refusal cannot carry is bad-json");
  expect(T, "{\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\":1}", "CHIRP= err bad-json", "  ... and so is one of 33 characters");
  expect(T, "{\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\":1}", "CHIRP= err unsupported aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
         "  ... one of 32 is named");
  expect(T, "{\"token\":\"\"}", "CHIRP= err bad-json", "an empty token is refused (no send at Open)");
  expect(T, "{\"claim\":\"\"}", "CHIRP= err bad-json", "an empty setup code is refused");
  expect(T, "{\"key\":\"\"}", "CHIRP= err bad-json", "an empty key is refused");
  expect(T, "{\"claim\":\"C\",\"token\":\"T\",\"ssid\":\"s\"}", "CHIRP= err bad-json", "a claim and a token together");
  expect(T, "{\"claim\":123}", "CHIRP= err bad-json", "a claim that is not text");
  expect(T, "{\"token\":\"tok\\u0007en\"}", "CHIRP= err bad-json", "a token with a control character");
  expect(T, "{\"token\":\"caf\\u00e9\"}", "CHIRP= err bad-json", "a token past ASCII");
  expect(T, "{\"host\":null}", "CHIRP= err bad-json", "host null");
  expect(T, "{\"host\":\"under_score.example\"}", "CHIRP= err bad-json", "a host with an underscore");
  expect(T, "[1,2,3]", "CHIRP= err bad-json", "not an object");
  expect(T, "{\"claim\":\"C\",}", "CHIRP= err bad-json", "a trailing comma");
  expect(T, "{'claim':'C'}", "CHIRP= err bad-json", "single quotes");
  expect(T, "{\"claim\":\"C\"} trailing", "CHIRP= err bad-json", "junk after the object");
  expect(T, "{\"claim\":\"C\"} {}", "CHIRP= err bad-json", "two objects on one line");
  expect(T, "{\"ssid\":NaN}", "CHIRP= err bad-json", "NaN is not JSON");
  expect(T, "", "CHIRP= err bad-json", "nothing after CHIRP+");

  puts("\n--- completeness: a credential alone is complete ---");
  expect(NULL, "{\"claim\":\"C\"}", "CHIRP= ok", "a claim alone");
  expect(NULL, "{\"token\":\"T\"}", "CHIRP= ok", "a token alone");
  expect(NULL, "{\"claim\":\"CHIRP-PLACEHOLDER\",\"hwid\":\"wba65-demo-3f9a12\",\"host\":\"h\",\"key\":\"" KEY_A "\"}",
         "CHIRP= ok", "what the setup page pushes at a fresh board");
  expect(NULL, "{\"host\":\"h\"}", "CHIRP= err missing-field", "no credential");
  expect(NULL, "{\"ssid\":\"s\",\"pass\":\"p\"}", "CHIRP= err missing-field", "networks without a credential");
  expect(NULL, "{\"key\":\"" KEY_A "\"}", "CHIRP= err missing-field", "a key without a credential");
  expect(NULL, "{\"hwid\":\"\",\"token\":\"t\"}", "CHIRP= ok", "an empty id: the board's own");

  puts("\n--- the id ---");
  expect(T, "{\"hwid\":\"example-board-0000\"}", "CHIRP= ok", "a portal id");
  expect(T, "{\"hwid\":\"A.b_c:9-z\"}", "CHIRP= ok", "every allowed punctuation");
  expect(T, "{\"hwid\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde\"}", "CHIRP= ok",
         "63 characters, the most this board holds");
  expect(T, "{\"hwid\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}",
         "CHIRP= err bad-json", "64 characters are refused, not cut short");
  expect(T, "{\"hwid\":\"two words\"}", "CHIRP= err bad-json", "a space");
  expect(T, "{\"hwid\":\"id\\r\\nCHIRP! v=2 hw=other\"}", "CHIRP= err bad-json", "a line break cannot forge an announce");
  expect(T, "{\"hwid\":\"caf\\u00e9\"}", "CHIRP= err bad-json", "past ASCII");
  expect(T, "{\"hwid\":\"a/b\"}", "CHIRP= err bad-json", "a slash");
  expect(T, "{\"hwid\":7}", "CHIRP= err bad-json", "not text");
  expect_held(T, "{\"hwid\":\"bo\\u0061rd\"}", "{\"token\":\"tok\",\"hwid\":\"board\"}", "an escaped id is its decoded text");

  puts("\n--- the credential and the host: the contract's bounds ---");
  expect(T, "{\"token\":\"ttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttt\"}", "CHIRP= ok", "a token of 63");
  expect(T, "{\"token\":\"tttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttt\"}", "CHIRP= err bad-json",
         "a token of 64");
  expect(T, "{\"claim\":\"CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC\"}", "CHIRP= err bad-json",
         "a setup code of 64");
  expect(T, "{\"host\":\"h012345678901234567890123456789012345678901234567890123.example\"}", "CHIRP= ok", "a host of 63");
  expect(T, "{\"host\":\"h0123456789012345678901234567890123456789012345678901234.example\"}", "CHIRP= err bad-json",
         "a host of 64");
  expect(T, "{\"host\":5}", "CHIRP= err bad-json", "a host that is not text");
  expect_held("{\"token\":\"tok\",\"host\":\"core.example.com\"}", "{\"host\":\"\"}", "{\"token\":\"tok\"}",
              "the empty host is Warblet's own: the stored one goes");
  expect_held(T, "{\"token\":\"t\\\"k\"}", "{\"token\":\"t\\\"k\"}", "a quote in a token is kept");

  puts("\n--- the signing key ---");
  expect(T, "{\"key\":\"" KEY_A "\"}", "CHIRP= ok", "44 characters of standard base64");
  expect(T, "{\"key\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8\"}", "CHIRP= err bad-json", "no '=' pad");
  expect(T, "{\"key\":\"c2hvcnQ=\"}", "CHIRP= err bad-json", "not 32 bytes");
  expect(T, "{\"key\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdH!8=\"}", "CHIRP= err bad-json", "outside base64");
  expect(T, "{\"key\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh-_\"}", "CHIRP= err bad-json", "URL-safe");
  expect(T, "{\"key\":42}", "CHIRP= err bad-json", "not text");

  puts("\n--- the new-device rule ---");
  {
    const char *signed_cfg = "{\"token\":\"T\",\"hwid\":\"old-id\",\"key\":\"" KEY_A "\"}";
    expect_held(signed_cfg, "{\"claim\":\"CHIRP-NEW\",\"hwid\":\"new-id\"}",
                "{\"token\":\"CHIRP-NEW\",\"hwid\":\"new-id\"}", "a claim and a new id: the old key goes");
    expect_held(signed_cfg, "{\"token\":\"T2\",\"hwid\":\"OLD-ID\"}", "{\"token\":\"T2\",\"hwid\":\"OLD-ID\"}",
                "an id that differs only in case is another device: the key goes");
    expect_held(signed_cfg, "{\"token\":\"T2\",\"hwid\":\"old-id\"}",
                "{\"token\":\"T2\",\"hwid\":\"old-id\",\"key\":\"" KEY_A "\"}", "the same id keeps the key");
    expect_held(signed_cfg, "{\"claim\":\"CHIRP-NEW\",\"key\":\"" KEY_B "\"}",
                "{\"token\":\"CHIRP-NEW\",\"hwid\":\"old-id\",\"key\":\"" KEY_B "\"}", "a claim that brings a key keeps that one");
    expect_held(signed_cfg, "{\"ssid\":\"cafe\",\"pass\":\"p\"}",
                "{\"ssid\":\"cafe\",\"pass\":\"p\",\"token\":\"T\",\"hwid\":\"old-id\",\"key\":\"" KEY_A "\"}",
                "networks alone keep the key");
    expect_held("{\"token\":\"T\",\"key\":\"" KEY_A "\"}", "{\"token\":\"T2\",\"hwid\":\"" DIE_ID "\"}",
                "{\"token\":\"T2\",\"hwid\":\"" DIE_ID "\",\"key\":\"" KEY_A "\"}",
                "the die id it already answers to keeps the key");
    expect_held(signed_cfg, "{\"hwid\":\"\"}", "{\"token\":\"T\",\"key\":\"" KEY_A "\"}",
                "an empty id starts nothing: the key stays (the contract's words)");
  }

  puts("\n--- networks, kept unused ---");
  {
    const char *listed = "{\"token\":\"T\",\"ssid\":\"cafe\",\"pass\":\"p1\","
                         "\"aps\":[{\"ssid\":\"cafe\",\"pass\":\"p1\"},{\"ssid\":\"office\",\"pass\":\"p2\"}]}";
    expect_held(listed, "{\"pass\":\"new\"}",
                "{\"token\":\"T\",\"ssid\":\"cafe\",\"pass\":\"new\","
                "\"aps\":[{\"ssid\":\"cafe\",\"pass\":\"new\"},{\"ssid\":\"office\",\"pass\":\"p2\"}]}",
                "a pass alone with a list stored is the first network's, in the list too");
    expect_held(listed, "{\"ssid\":\"home\",\"pass\":\"pw\"}", "{\"token\":\"T\",\"ssid\":\"home\",\"pass\":\"pw\"}",
                "an ssid without aps names exactly one network");
    expect_held(T, "{\"aps\":[{\"ssid\":\"a\",\"pass\":\"1\"},{\"ssid\":\"b\",\"pass\":\"2\"}]}",
                "{\"token\":\"tok\",\"ssid\":\"a\",\"pass\":\"1\",\"aps\":[{\"ssid\":\"a\",\"pass\":\"1\"},{\"ssid\":\"b\",\"pass\":\"2\"}]}",
                "aps alone: its first network is the one tried first");
    expect(T, "{\"ssid\":\"x\",\"aps\":[{\"ssid\":\"a\",\"pass\":\"1\"}]}", "CHIRP= err bad-json",
           "an ssid that is not aps's first");
    expect(T, "{\"aps\":[{\"ssid\":\"a\",\"pass\":\"b\",\"bssid\":\"x\"}]}", "CHIRP= err bad-json", "an aps entry with another key");
    expect(T, "{\"aps\":[]}", "CHIRP= err bad-json", "an empty aps");
    expect(T, "{\"aps\":[{\"ssid\":\"n0\",\"pass\":\"\"},{\"ssid\":\"n1\",\"pass\":\"\"},{\"ssid\":\"n2\",\"pass\":\"\"},"
           "{\"ssid\":\"n3\",\"pass\":\"\"},{\"ssid\":\"n4\",\"pass\":\"\"},{\"ssid\":\"n5\",\"pass\":\"\"},"
           "{\"ssid\":\"n6\",\"pass\":\"\"},{\"ssid\":\"n7\",\"pass\":\"\"},{\"ssid\":\"n8\",\"pass\":\"\"}]}",
           "CHIRP= err bad-json", "nine networks");
    expect(T, "{\"ssid\":\"\"}", "CHIRP= err bad-json", "an empty ssid");
    expect(T, "{\"ssid\":\"sssssssssssssssssssssssssssssssss\"}", "CHIRP= err bad-json", "an ssid of 33 bytes");
    expect_held(T, "{\"ssid\":\"caf\\u00e9 \\\"guest\\\"\"}", "{\"token\":\"tok\",\"ssid\":\"caf\\u00e9 \\\"guest\\\"\",\"pass\":\"\"}",
                "escapes and \\u come through as the same text");
  }

  puts("\n--- the Thread object, kept unused ---");
  expect(T, "{\"thread\":{\"name\":\"chirp-lab\",\"key\":\"" TKEY "\",\"panid\":\"1234\",\"xpanid\":\"1111111122222222\","
         "\"channel\":15}}", "CHIRP= ok", "discrete fields");
  expect(T, "{\"thread\":{\"name\":\"n\",\"key\":\"00112233445566778899AABBCCDDEEFF\"}}", "CHIRP= ok", "uppercase key hex");
  expect_held(T, "{\"thread\":{\"dataset\":\"0e08\",\"name\":\"x\",\"key\":\"nope\"}}",
              "{\"token\":\"tok\",\"thread\":{\"dataset\":\"0e08\"}}", "the dataset wins, and is all that is kept");
  expect(T, "{\"thread\":{\"name\":\"n\"}}", "CHIRP= err missing-field", "a name without a key");
  expect(T, "{\"thread\":{\"key\":\"" TKEY "\"}}", "CHIRP= err missing-field", "a key without a name");
  expect(T, "{\"thread\":{\"name\":\"n\"},\"hwid\":\"a b\"}", "CHIRP= err bad-json", "a bad value comes before missing-field");
  expect(T, "{\"thread\":{\"name\":\"n\",\"key\":\"00112233\"}}", "CHIRP= err bad-json", "a key too short");
  expect(T, "{\"thread\":{\"name\":\"n\",\"key\":\"00112233445566778899aabbccddeegg\"}}", "CHIRP= err bad-json", "a key not hex");
  expect(T, "{\"thread\":{\"name\":\"0123456789abcdefg\",\"key\":\"" TKEY "\"}}", "CHIRP= err bad-json", "a name of 17");
  expect(T, "{\"thread\":{\"name\":\"n\",\"key\":\"" TKEY "\",\"channel\":27}}", "CHIRP= err bad-json", "channel 27");
  expect(T, "{\"thread\":{\"name\":\"n\",\"key\":\"" TKEY "\",\"channel\":10}}", "CHIRP= err bad-json", "channel 10");
  expect(T, "{\"thread\":{\"name\":\"n\",\"key\":\"" TKEY "\",\"channel\":4294967307}}", "CHIRP= err bad-json",
         "channel 2^32 + 11 does not wrap to 11");
  expect(T, "{\"thread\":{\"name\":\"n\",\"key\":\"" TKEY "\",\"panid\":\"12g4\"}}", "CHIRP= err bad-json", "panid not hex");
  expect(T, "{\"thread\":{\"name\":\"n\",\"key\":\"" TKEY "\",\"xpanid\":\"1111\"}}", "CHIRP= err bad-json", "xpanid short");
  expect(T, "{\"thread\":{\"dataset\":\"0e0\"}}", "CHIRP= err bad-json", "an odd-length dataset");
  expect(T, "{\"thread\":\"not-an-object\"}", "CHIRP= err bad-json", "thread must be an object");

  puts("\n--- a stored record ---");
  expect_held("{\"token\":\"T\",\"level\":1,\"colour\":\"red\"}", "{\"hwid\":\"h\"}", "{\"token\":\"T\",\"hwid\":\"h\"}",
              "a field the board does not take is not carried over");
  expect("{\"token\":\"tttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttt\"}", "{\"hwid\":\"h\"}",
         "CHIRP= err missing-field", "a stored token over the bound is not carried: no usable credential");

  puts("\n--- the longest record ---");
  {
    /* Every value at its bound, every byte of text one that needs an escape. */
    char *p = big;
    int   i;
    unsigned n = 0u;
    p += sprintf(p, "{\"aps\":[");
    for (i = 0; i < 8; i++) {
      p += sprintf(p, "%s{\"ssid\":\"", i ? "," : "");
      for (int k = 0; k < 31; k++) { p += sprintf(p, "\\\""); }
      p += sprintf(p, "%d\",\"pass\":\"", i);
      for (int k = 0; k < 64; k++) { p += sprintf(p, "\\\\"); }
      p += sprintf(p, "\"}");
    }
    p += sprintf(p, "],\"thread\":{\"dataset\":\"");
    for (i = 0; i < 508; i++) { *p++ = "0123456789abcdef"[i % 16]; }
    p += sprintf(p, "\"},\"claim\":\"");
    for (i = 0; i < 63; i++) { p += sprintf(p, "\\\""); }
    p += sprintf(p, "\",\"hwid\":\"");
    for (i = 0; i < 63; i++) { *p++ = 'h'; }
    p += sprintf(p, "\",\"host\":\"");
    for (i = 0; i < 63; i++) { *p++ = 'n'; }
    p += sprintf(p, "\",\"key\":\"" KEY_A "\"}");
    board_with(NULL);
    check(strcmp(push(big), "CHIRP= ok") == 0, "the longest push is taken", first_line);
    (void)cfg_json(&n);
    printf("      the longest record is %u bytes; CHIRP_RECORD_MAX %u, the store %u\n",
           n, (unsigned)CHIRP_RECORD_MAX, (unsigned)CFG_MAX_PAYLOAD);
    check(n > 0u && n <= CHIRP_RECORD_MAX && CHIRP_RECORD_MAX <= CFG_MAX_PAYLOAD,
          "  ... and fits CHIRP_RECORD_MAX, which fits the store", NULL);
    check(n + 16u > CHIRP_RECORD_MAX, "  ... and CHIRP_RECORD_MAX is within 16 bytes of it", NULL);
    {
      /* What the next start reads is the push's settings, with aps's first
       * network as the one tried first. */
      static char want[CHIRP_LINE_MAX + 512];
      char *w = want;
      int   k;
      w += sprintf(w, "{\"ssid\":\"");
      for (k = 0; k < 31; k++) { w += sprintf(w, "\\\""); }
      w += sprintf(w, "0\",\"pass\":\"");
      for (k = 0; k < 64; k++) { w += sprintf(w, "\\\\"); }
      sprintf(w, "\",%s", big + 1);
      check(same_record(held_after_restart(), want), "  ... and the next start reads every value of it", NULL);
    }
  }
}

/* ---- the announce ----------------------------------------------------------------- */

/* JSON text with the spaces outside strings taken out. */
static void compact(const char *in, unsigned n, char *out)
{
  int quoted = 0;
  for (unsigned i = 0; i < n; i++) {
    char c = in[i];
    if (c == '"' && (i == 0 || in[i - 1] != '\\')) { quoted = !quoted; }
    if (quoted || (c != ' ' && c != '\n' && c != '\r' && c != '\t')) { *out++ = c; }
  }
  *out = '\0';
}

static void test_announce(void)
{
  char   *doc = read_file("demo.json");
  js_val  fw, firmware, caps, slug;
  char    want[1024], fwv[80], slugv[64], capsv[512];
  const char *line;
  unsigned n = 0;

  puts("\n--- the announce: v1.8's line, as demo.json declares ---");
  check(doc != NULL, "demo.json reads", NULL);
  if (doc == NULL) { return; }
  if (js_obj_get(doc, (unsigned)strlen(doc), "firmware", &firmware) != 1 ||
      js_obj_get(firmware.p, firmware.n, "fw", &fw) != 1 || fw.t != JS_STR ||
      js_obj_get(firmware.p, firmware.n, "capabilities", &caps) != 1 || caps.t != JS_OBJ ||
      js_obj_get(doc, (unsigned)strlen(doc), "slug", &slug) != 1 || slug.t != JS_STR) {
    check(0, "demo.json declares its slug, fw and capabilities", NULL);
    free(doc);
    return;
  }
  js_str_copy(&fw, fwv, sizeof fwv);
  js_str_copy(&slug, slugv, sizeof slugv);
  compact(caps.p, caps.n, capsv);
  check(strcmp(capsv, CHIRP_CAPS) == 0, "CHIRP_CAPS is demo.json's capabilities", capsv);
  check(strcmp(fwv, CHIRP_FW) == 0, "the Makefile's FW_VERSION is demo.json's fw", fwv);

  board_with("{\"token\":\"tok\",\"hwid\":\"board-1\"}");
  line = answer_to("CHIRP?\r\n");
  snprintf(want, sizeof want, "CHIRP! v=2 hw=board-1 fw=%s key_set=0 radios= t=%s caps=%s", fwv, slugv, capsv);
  check(strcmp(line, want) == 0, "the line: v=2 hw fw key_set, radios empty, demo.json's slug, caps last", line);
  board_with("{\"token\":\"tok\",\"hwid\":\"board-1\",\"key\":\"" KEY_A "\"}");
  check(strstr(answer_to("CHIRP?\r\n"), " key_set=1 ") != NULL, "a stored key: key_set=1", first_line);
  board_with("{\"token\":\"tok\"}");
  check(strstr(answer_to("CHIRP?\r\n"), " hw=" DIE_ID " ") != NULL, "no id stored: the die's own", first_line);
  board_with("{\"token\":\"tok\",\"hwid\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde\"}");
  line = answer_to("CHIRP?\r\n");
  n = (unsigned)strlen(line);
  check(n <= 512u && strstr(line, "abcde fw=") != NULL, "with the longest id the line fits 512 bytes", line);
  push("{\"key\":\"" KEY_A "\"}");
  check(strstr(answer_to("CHIRP?\r\n"), " key_set=1 ") != NULL, "a key pushed now is announced without a restart",
        first_line);

  if (s_vectors != NULL) {
    js_val profiles, relay, vcaps;
    char   vc[512];
    if (js_obj_get(s_vectors, (unsigned)strlen(s_vectors), "profiles", &profiles) == 1 &&
        js_obj_get(profiles.p, profiles.n, "relay", &relay) == 1 &&
        js_obj_get(relay.p, relay.n, "caps", &vcaps) == 1 && js_str_copy(&vcaps, vc, sizeof vc) > 0) {
      check(strcmp(vc, CHIRP_CAPS) == 0, "CHIRP_CAPS is the vectors' relay profile", vc);
    } else {
      check(0, "the vectors name the relay profile's caps", NULL);
    }
  }
  free(doc);
}

/* ---- the store -------------------------------------------------------------------- */

static const char OLD[] = "{\"token\":\"tok-old\",\"hwid\":\"board-old\"}";
static const char NEW[] = "{\"ssid\":\"home\",\"pass\":\"pw\",\"token\":\"tok-new\",\"hwid\":\"board-new\","
                          "\"key\":\"" KEY_A "\"}";

/* What the next start reads, raw ("" for nothing). */
static const char *reads(void)
{
  static char copy[CFG_MAX_PAYLOAD + 1];
  const char *j;
  power_on();
  (void)cfg_load();
  j = cfg_json(NULL);
  snprintf(copy, sizeof copy, "%s", j ? j : "");
  return copy;
}

static int blank_page(unsigned i)
{
  for (unsigned k = 0; k < CFG_PAGE_SIZE; k++) { if (flash_mem[i][k] != 0xFF) { return 0; } }
  return 1;
}

static unsigned crc_add(unsigned c, const unsigned char *d, unsigned n)
{
  for (unsigned i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) { c = (c >> 1) ^ (0xEDB88320u & (unsigned)(-(int)(c & 1u))); }
  }
  return c;
}

/* A record written the way flashcfg.c writes one: 'CWB1', version 2, the
 * length, seq, and the CRC-32 of seq, length and payload, then the payload. */
static void put_record(unsigned page, unsigned seq, const char *json)
{
  unsigned      len = (unsigned)strlen(json), crc;
  unsigned char h[16], b[6];

  memset(flash_mem[page], 0xFF, CFG_PAGE_SIZE);
  memset(flash_torn[page], 0, sizeof flash_torn[page]);
  for (int i = 0; i < 4; i++) { b[i] = (unsigned char)(seq >> (8 * i)); }
  b[4] = (unsigned char)len;
  b[5] = (unsigned char)(len >> 8);
  crc = ~crc_add(crc_add(0xFFFFFFFFu, b, 6u), (const unsigned char *)json, len);
  for (int i = 0; i < 4; i++) {
    h[i]      = (unsigned char)(0x31425743u >> (8 * i));
    h[8 + i]  = (unsigned char)(seq >> (8 * i));
    h[12 + i] = (unsigned char)(crc >> (8 * i));
  }
  h[4] = 2u; h[5] = 0u;
  h[6] = (unsigned char)len; h[7] = (unsigned char)(len >> 8);
  memcpy(flash_mem[page], h, 16u);
  memset(flash_mem[page] + 16, 0, (len + 15u) & ~15u);
  memcpy(flash_mem[page] + 16, json, len);
}

/* The steps a save of NEW takes over OLD: erase, writes, erase. */
static int save_steps(void)
{
  flash_blank();
  (void)cfg_load();
  (void)cfg_store(OLD, sizeof OLD - 1u);
  power_on();
  (void)cfg_load();
  steps = 0;
  (void)cfg_store(NEW, sizeof NEW - 1u);
  return steps;
}

static void test_store(void)
{
  int  total, k, kind, mode, good;
  char what[160];

  puts("\n--- the store: two pages, the newest valid record wins ---");
  flash_blank();
  check(cfg_load() == -1 && cfg_json(NULL) == NULL, "a blank store holds nothing", NULL);
  check(cfg_store(OLD, sizeof OLD - 1u) == 0 && strcmp(reads(), OLD) == 0, "a save is read back whole", NULL);
  check(cfg_page_addr() == FAKE_BASE, "  ... from the first page", NULL);
  check(cfg_store(NEW, sizeof NEW - 1u) == 0 && strcmp(reads(), NEW) == 0, "a second save is read back", NULL);
  check(cfg_page_addr() == FAKE_BASE + CFG_PAGE_SIZE && blank_page(0),
        "  ... from the other page, and the old page is erased", NULL);
  power_on(); (void)cfg_load();
  check(cfg_store(OLD, sizeof OLD - 1u) == 0 && strcmp(reads(), OLD) == 0 && cfg_page_addr() == FAKE_BASE,
        "a third goes back to the first page", NULL);

  /* The format, written by hand: the record the board reads is the one the test wrote. */
  flash_blank();
  put_record(1, 41u, NEW);
  check(strcmp(reads(), NEW) == 0 && cfg_page_addr() == FAKE_BASE + CFG_PAGE_SIZE,
        "a record in the second page alone is read", NULL);
  power_on();
  check(cfg_store(OLD, sizeof OLD - 1u) == 0 && blank_page(1), "the next save goes to the first page", NULL);
  put_record(1, 41u, NEW);
  check(strcmp(reads(), OLD) == 0, "  ... with seq 42, so it beats the 41 written back beside it", NULL);

  /* Two valid records: the newer wins, across the wrap of the count too. */
  {
    unsigned seqs[3][2] = { {5u, 6u}, {7u, 6u}, {0xFFFFFFFFu, 0u} };
    for (int c = 0; c < 3; c++) {
      flash_blank();
      put_record(0, seqs[c][0], OLD);
      put_record(1, seqs[c][1], NEW);
      snprintf(what, sizeof what, "seq %u against seq %u: the newer wins", seqs[c][0], seqs[c][1]);
      check(strcmp(reads(), ((int)(seqs[c][1] - seqs[c][0]) > 0) ? NEW : OLD) == 0, what, NULL);
    }
  }

  /* A newer record that cannot be read, or whose CRC fails: the older one is read. */
  flash_blank();
  put_record(0, 1u, OLD);
  put_record(1, 2u, NEW);
  flash_torn[1][0] = 1;
  check(strcmp(reads(), OLD) == 0, "a newer page that cannot be read (ECC): the older record is read", NULL);
  put_record(1, 2u, NEW);
  flash_mem[1][20] ^= 1u;
  check(strcmp(reads(), OLD) == 0, "a newer page whose CRC fails: the older record is read", NULL);
  flash_torn[0][1] = 1;
  flash_torn[1][1] = 1;
  check(cfg_load() == -2 && cfg_json(NULL) == NULL, "neither page readable: nothing held, and said so (-2)", NULL);
  flash_blank();
  memcpy(flash_mem[1], "\x43\x57\x42\x31\x01\x00\x10\x00", 8u);    /* version 1, one page: not read */
  check(cfg_load() == -1, "a record of the one-page store before (version 1) is not read", NULL);

  /* One failed operation at any step of a save: the answer is what the next
   * start reads, every time. */
  total = save_steps();
  printf("      a save of %u bytes over another takes %d flash steps\n", (unsigned)sizeof NEW - 1u, total);
  good = 1;
  for (kind = 0; kind < FAIL_KINDS; kind++) {
    for (k = 0; k < total; k++) {
      int r;
      const char *held;
      flash_blank();
      (void)cfg_load();
      (void)cfg_store(OLD, sizeof OLD - 1u);
      power_on();
      (void)cfg_load();
      fail_at = k; fail_kind = kind;
      r = cfg_store(NEW, sizeof NEW - 1u);
      held = reads();
      if (strcmp(held, r == 0 ? NEW : OLD) != 0) {
        snprintf(what, sizeof what, "failure kind %d at step %d: answered %d, next start read %s", kind, k, r, held);
        check(0, what, NULL);
        good = 0;
      }
      /* ... and later saves go on. */
      power_on();
      (void)cfg_load();
      if (cfg_store(OLD, sizeof OLD - 1u) != 0 || strcmp(reads(), OLD) != 0) {
        snprintf(what, sizeof what, "failure kind %d at step %d: the next save did not land", kind, k);
        check(0, what, NULL);
        good = 0;
      }
    }
  }
  check(good, "one failed flash operation at any step: the answer is what the next start reads", NULL);

  /* A power cut at any step: the next start reads the old or the new record,
   * whole, and the new one whenever the save said ok. */
  good = 1;
  for (mode = CUT_TORN; mode <= CUT_CLEAN; mode++) {
    for (k = 0; k <= total; k++) {
      int r;
      const char *held;
      flash_blank();
      (void)cfg_load();
      (void)cfg_store(OLD, sizeof OLD - 1u);
      power_on();
      (void)cfg_load();
      cut_at = k; cut_mode = mode;
      r = cfg_store(NEW, sizeof NEW - 1u);
      held = reads();
      if (!((strcmp(held, OLD) == 0 || strcmp(held, NEW) == 0) && (r != 0 || strcmp(held, NEW) == 0))) {
        snprintf(what, sizeof what, "power cut (%s) at step %d: answered %d, next start read %s",
                 mode == CUT_TORN ? "torn" : "clean", k, r, held);
        check(0, what, NULL);
        good = 0;
      }
      power_on();
      (void)cfg_load();
      if (cfg_store(OLD, sizeof OLD - 1u) != 0 || strcmp(reads(), OLD) != 0) { good = 0; }
    }
  }
  check(good, "a power cut at any step, torn or clean: old or new, whole, and new when it said ok", NULL);

  /* Through the console: store-failed leaves the old settings in force. */
  board_with(OLD);
  fail_at = 0; fail_kind = FAIL_NOTHING;
  check(strcmp(push("{\"token\":\"tok-2\"}"), "CHIRP= err store-failed") == 0, "a store the flash refuses: store-failed",
        first_line);
  check(strcmp(held_after_restart(), OLD) == 0, "  ... and the next start reads the old settings", NULL);
}

/* ---- the line ----------------------------------------------------------------------- */

static void test_line(void)
{
  static char line[CHIRP_LINE_MAX + 256];
  const char *inner = "CHIRP+ {\"hwid\":\"board-new\"}";
  int none = 1;

  puts("\n--- the line ---");
  /* No tail of an over-long line is a push: an inner push before, across or past the limit. */
  for (size_t cut = 0; cut < strlen(inner); cut++) {
    size_t pad = CHIRP_LINE_MAX - 7u - cut;
    board_with(OLD);
    memcpy(line, "CHIRP+ ", 7u);
    memset(line + 7, 'j', pad);
    sprintf(line + 7 + pad, "%s\r\n", inner);
    none = none && answer_to(line)[0] == '\0' && strcmp(held_after_restart(), OLD) == 0;
  }
  check(none, "a line over the limit gets no answer, and no tail of it is read as a push", NULL);
  check(strcmp(answer_to("CHIRP+ {\"hwid\":\"board-2\"}\r\n"), "CHIRP= ok") == 0, "the line after it is read", first_line);

  /* The limit counts from "CHIRP": exactly 4096 after stray bytes. */
  board_with(OLD);
  memset(line, 'z', 50u);
  strcpy(line + 50, "CHIRP+ {\"hwid\":\"board-3\"}");
  {
    size_t n = strlen(line + 50);
    memset(line + 50 + n, ' ', CHIRP_LINE_MAX - n);
    strcpy(line + 50 + CHIRP_LINE_MAX, "\r\n");
  }
  check(strcmp(answer_to(line), "CHIRP= ok") == 0, "a line of exactly 4096 from CHIRP, after stray bytes, is read",
        first_line);
  line[50 + CHIRP_LINE_MAX] = ' ';
  strcpy(line + 51 + CHIRP_LINE_MAX, "\r\n");
  check(answer_to(line)[0] == '\0', "  ... and one of 4097 is thrown away", first_line);

  /* A line that stops for 1.5 s is given up; pieces a second apart land. */
  board_with(OLD);
  said_len = 0u; said[0] = '\0';
  send_text("CHIRP+ {\"hwid\":", 1u);
  clock_ms += 1500u;
  send_text("\"board-late\"}\r\n", 1u);
  check(said_len == 0u, "a line that stopped for 1.5 s is given up, and what follows is not joined to it", said);
  check(strncmp(answer_to("CHIRP?\r\n"), "CHIRP! ", 7) == 0, "a probe after it is answered", first_line);
  said_len = 0u;
  send_text("CHIRP+ {\"hwid\":\"board-paced\"}\r\n", 1000u);
  check(strncmp(said, "CHIRP= ok", 9) == 0, "a push whose bytes come a second apart lands", said);
  said_len = 0u;
  send_text("CHI", 1u);
  clock_ms += 5000u;
  send_text("RP?\r\n", 1u);
  check(strncmp(said, "CHIRP! ", 7) == 0, "a pause before \"CHIRP\" is complete is not a stopped line", said);

  /* Stray bytes in front of a command are dropped, and not kept. */
  memset(line, 'z', 200u);
  line[200] = '\0';
  send_text(line, 1u);
  check(s_len <= JUNK_KEPT, "stray bytes before CHIRP are not kept (at most 64)", NULL);
  check(strncmp(answer_to("\x5a\xa6" "CHIRP?\r\n"), "CHIRP! ", 7) == 0, "a probe after stray bytes is answered", first_line);

  /* CR, LF and CRLF each end a line. */
  said_len = 0u; said[0] = '\0';
  send_text("CHIRP?\rCHIRP?\nCHIRP?\r\n", 1u);
  {
    int count = 0;
    for (const char *p = said; (p = strstr(p, "CHIRP! ")) != NULL; p++) { count++; }
    check(count == 3, "CR, LF and CRLF each end a line, once", said);
  }
  check(strcmp(answer_to("CHIRP?  \t\r\n"), "") != 0, "trailing spaces after a probe are not part of it", first_line);
}

/* ---- no request without a credential ------------------------------------------------- */

static void test_no_open(void)
{
  puts("\n--- nothing is sent without a credential ---");
  board_with(NULL);
  said_len = 0u; said[0] = '\0';
  chirp_send_init();
  chirp_send_tick(0u);
  chirp_send_tick(30000u);
  chirp_send_tick(60000u);
  check(strstr(said, "CHIRP^") == NULL && strstr(said, "no credential") != NULL,
        "no setup: no request, and the console says why", said);
  check(strcmp(push("{\"token\":\"\"}"), "CHIRP= err bad-json") == 0, "an empty token is refused", first_line);
  said_len = 0u; said[0] = '\0';
  chirp_send_tick(90000u);
  check(strstr(said, "CHIRP^") == NULL, "  ... and still nothing is sent", said);
  check(strcmp(push("{\"claim\":\"CHIRP-TEST\"}"), "CHIRP= ok") == 0, "a setup code is taken", first_line);
  said_len = 0u; said[0] = '\0';
  chirp_send_tick(120000u);
  check(strstr(said, "CHIRP^ X-Chirp-Claim: CHIRP-TEST\r\n") != NULL && strstr(said, "CHIRP^ body ") != NULL,
        "  ... and the next report carries it, with no reboot", said);
}

/* ---- JSON, SHA-256, HMAC, signed input (unchanged from before v1.8) ------------------ */

static void hex_of(const unsigned char *d, unsigned n, char *out)
{
  static const char H[] = "0123456789abcdef";
  for (unsigned i = 0; i < n; i++) {
    out[i * 2] = H[d[i] >> 4];
    out[i * 2 + 1] = H[d[i] & 0xF];
  }
  out[n * 2] = '\0';
}

static void check_hex(const char *got, const char *want, const char *why)
{
  char detail[200];
  snprintf(detail, sizeof detail, "want %s\n      got  %s", want, got);
  check(strcmp(got, want) == 0, why, detail);
}

/* The contract's example key: 32 ASCII bytes, visibly not a real credential. */
static const unsigned char vector_key[] = "0123456789abcdef0123456789abcdef";
#define VECTOR_KEY_LEN 32u

#define BODY_MAX 64u

/* Build the signed input the way the firmware does (chirp_sign_prefix, then
 * the body), check those bytes as hex, then check the HMAC from chirp_sign.
 * NULL means the message has no such field. */
static void check_signed(const unsigned char *key, unsigned klen,
                         const char *nonce, const char *tag,
                         const void *body, unsigned blen,
                         const char *want_input_hex, const char *want_hmac,
                         const char *why)
{
  unsigned char input[CHIRP_SIGN_PREFIX_MAX + BODY_MAX];
  char          hex[2u * sizeof input + 1u];
  char          got[65];
  int           plen = chirp_sign_prefix((char *)input, CHIRP_SIGN_PREFIX_MAX, nonce, tag);

  if (plen < 0 || blen > BODY_MAX) {
    check(0, why, "the signed input did not fit");
    return;
  }
  memcpy(input + plen, body, blen);
  hex_of(input, (unsigned)plen + blen, hex);
  if (strcmp(hex, want_input_hex) != 0) {
    check_hex(hex, want_input_hex, why);
    return;
  }
  chirp_sign(key, klen, input, (unsigned)plen + blen, got);
  check_hex(got, want_hmac, why);
}

static void check_sha(const char *msg, const char *want, const char *why)
{
  unsigned char d[SHA256_DIGEST];
  char got[2 * SHA256_DIGEST + 1];
  sha256(msg, (unsigned)strlen(msg), d);
  hex_of(d, SHA256_DIGEST, got);
  check_hex(got, want, why);
}

static void check_summary(void)
{
  const char *full = "{\"thread\":{\"name\":\"chirp-lab\",\"key\":\"" TKEY "\",\"channel\":15},\"token\":\"tok\"}";

  puts("\n--- summary fields (what the boot line prints) ---");
  board_with(full);
  said_len = 0u; said[0] = '\0';
  chirp_prov_init();
  check(strstr(said, " thread=chirp-lab") != NULL && strstr(said, " ch=15") != NULL &&
        strstr(said, TKEY) == NULL && strstr(said, " key=sha256:") != NULL,
        "the boot summary names the network and fingerprints its key", said);
}

/* js_num_int must refuse, not wrap, a number past LONG_MAX. */
static void check_num(const char *text, int want_rc, long want, const char *why)
{
  js_val v;
  long got = 0;
  int rc;

  v.t = JS_NUM; v.p = text; v.n = (unsigned)strlen(text);
  rc = js_num_int(&v, &got);
  check(rc == want_rc && (rc != 0 || got == want), why, text);
}

static void test_num_limits(void)
{
  char max[32], over[32], tenfold[40], neg[40];

  snprintf(max, sizeof max, "%ld", LONG_MAX);
  snprintf(over, sizeof over, "%lu", (unsigned long)LONG_MAX + 1ul);
  snprintf(tenfold, sizeof tenfold, "%ld0", LONG_MAX);
  snprintf(neg, sizeof neg, "-%ld", LONG_MAX);

  puts("\n--- integer limits ---");
  check_num(max, 0, LONG_MAX, "LONG_MAX is accepted");
  check_num(neg, 0, -LONG_MAX, "-LONG_MAX is accepted");
  check_num(over, -1, 0, "LONG_MAX + 1 is refused, not wrapped");
  check_num(tenfold, -1, 0, "ten times LONG_MAX is refused");
  check_num("99999999999999999999", -1, 0, "twenty nines are refused");
}

static void test_sign(void)
{
  puts("\n--- sha256 (FIPS 180-4 examples) ---");
  check_sha("abc",
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "sha256(\"abc\")");
  check_sha("",
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "sha256(\"\") — the empty message");
  check_sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
            "sha256(56 bytes) — padding spills into a second block");
  check_sha("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
            "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
            "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1",
            "sha256(112 bytes) — two full blocks");

  puts("\n--- hmac-sha256 (RFC 4231) ---");
  {
    unsigned char key[131], data[50];
    char got[65];
    unsigned i;

    const char *d1 = "Hi There";
    const char *d2 = "what do ya want for nothing?";
    const char *d6 = "Test Using Larger Than Block-Size Key - Hash Key First";

    for (i = 0; i < 20u; i++) { key[i] = 0x0b; }
    chirp_sign(key, 20u, d1, (unsigned)strlen(d1), got);
    check_hex(got, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
              "RFC 4231 case 1");

    chirp_sign((const unsigned char *)"Jefe", 4u, d2, (unsigned)strlen(d2), got);
    check_hex(got, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
              "RFC 4231 case 2 — key shorter than the block");

    for (i = 0; i < 25u; i++) { key[i] = (unsigned char)(i + 1u); }
    for (i = 0; i < 50u; i++) { data[i] = 0xcd; }
    chirp_sign(key, 25u, data, 50u, got);
    check_hex(got, "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b",
              "RFC 4231 case 4");

    for (i = 0; i < 131u; i++) { key[i] = 0xaa; }
    chirp_sign(key, 131u, d6, (unsigned)strlen(d6), got);
    check_hex(got, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
              "RFC 4231 case 6 — key longer than the block");
  }

  puts("\n--- signed input: <nonce> \\n tag=<tag> \\n <body> (contract examples) ---");
  {
    static const char nonce[] = "1757332800123";
    static const char body[]  = "temp=21.4";
    static const char moved[] = "1757332800123\ntag=snap\ntemp=21.4";

    check_signed(vector_key, VECTOR_KEY_LEN, nonce, "snap", body, 9u,
                 "313735373333323830303132330a7461673d736e61700a74656d703d32312e34",
                 "1239148f97a361adfb7173445a84a68a4f0b3eb915d626b98e1080f7e6f78e81",
                 "nonce and tag");
    check_signed(vector_key, VECTOR_KEY_LEN, NULL, NULL, body, 9u,
                 "0a7461673d0a74656d703d32312e34",
                 "fd071ea6328d4ca04bf766d76c4298359a07db6b161fbc8446b336511954bda2",
                 "neither: \"\\ntag=\\n\" + body, the shape this board signs");
    check_signed(vector_key, VECTOR_KEY_LEN, "", "", body, 9u,
                 "0a7461673d0a74656d703d32312e34",
                 "fd071ea6328d4ca04bf766d76c4298359a07db6b161fbc8446b336511954bda2",
                 "empty strings sign the same as no fields");
    check_signed(vector_key, VECTOR_KEY_LEN, NULL, "snap", body, 9u,
                 "0a7461673d736e61700a74656d703d32312e34",
                 "aaa23e9e54a70f6c76ff1bfdc282831c3c62c0d574f592fdd831b554bef3e946",
                 "tag only: the empty nonce line is still there");
    check_signed(vector_key, VECTOR_KEY_LEN, NULL, NULL, "", 0u,
                 "0a7461673d0a",
                 "e33be001a2833da36aae83c191aa790e65ef7d07bbc061f9b236eed063e0eb38",
                 "empty body: only the two lines are signed");
    check_signed(vector_key, VECTOR_KEY_LEN, NULL, NULL, "a\nb\n", 4u,
                 "0a7461673d0a610a620a",
                 "cb339ac72c21c9ce109fe397965bacd56fdc35ca8be8f2aad1dd0eb2e1dd311d",
                 "multi-line body is signed byte for byte");
    check_signed(vector_key, VECTOR_KEY_LEN, NULL, NULL, moved, (unsigned)strlen(moved),
                 "0a7461673d0a313735373333323830303132330a7461673d736e61700a74656d703d32312e34",
                 "054bf69a4a1eb145077ab94da1e61c29fff03f98e8d79d4f2352157b9177a32c",
                 "the nonce and tag lines moved into the body");

    /* Moving the nonce and tag lines into the body must change the signature. */
    {
      unsigned char a[CHIRP_SIGN_PREFIX_MAX + BODY_MAX], b[CHIRP_SIGN_PREFIX_MAX + BODY_MAX];
      char          ha[65], hb[65];
      int           pa = chirp_sign_prefix((char *)a, CHIRP_SIGN_PREFIX_MAX, nonce, "snap");
      int           pb = chirp_sign_prefix((char *)b, CHIRP_SIGN_PREFIX_MAX, NULL, NULL);

      if (pa < 0 || pb < 0) {
        printf("FAIL  substitution: chirp_sign_prefix refused a prefix it has room for\n");
        fails++;
      } else {
        unsigned na = (unsigned)pa + 9u, nb = (unsigned)pb + (unsigned)strlen(moved);
        memcpy(a + pa, body, 9u);
        memcpy(b + pb, moved, strlen(moved));
        chirp_sign(vector_key, VECTOR_KEY_LEN, a, na, ha);
        chirp_sign(vector_key, VECTOR_KEY_LEN, b, nb, hb);
        if ((na == nb && memcmp(a, b, na) == 0) || strcmp(ha, hb) == 0) {
          printf("FAIL  substitution: fields moved into the body sign the same\n");
          fails++;
        } else {
          printf("ok    substitution: fields moved into the body sign differently\n");
        }
      }
    }

    /* The prefix refuses a buffer that is too small instead of cutting it short. */
    {
      char     small[8], big[CHIRP_SIGN_PREFIX_MAX];
      unsigned i;
      char     longest_nonce[CHIRP_NONCE_MAX + 1u], longest_tag[CHIRP_TAG_MAX + 1u];

      for (i = 0u; i < CHIRP_NONCE_MAX; i++) { longest_nonce[i] = '9'; }
      longest_nonce[CHIRP_NONCE_MAX] = '\0';
      for (i = 0u; i < CHIRP_TAG_MAX; i++) { longest_tag[i] = 't'; }
      longest_tag[CHIRP_TAG_MAX] = '\0';

      if (chirp_sign_prefix(small, 6u, NULL, NULL) == -1 &&
          chirp_sign_prefix(small, 7u, NULL, NULL) == 6 &&
          chirp_sign_prefix(big, sizeof big, longest_nonce, longest_tag) ==
            (int)(CHIRP_NONCE_MAX + CHIRP_TAG_MAX + 6u)) {
        printf("ok    prefix: refuses a short buffer, fits the longest nonce and tag\n");
      } else {
        printf("FAIL  prefix: buffer bounds\n");
        fails++;
      }
    }
  }

  puts("\n--- the demo's own L2 frame (the README quotes this) ---");
  {
    /* README example key: bytes 0x00 through 0x1f, base64
     * AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=. Never use it as a device
     * credential. Body 00 c9 is 201 tenths = 20.1 C. */
    unsigned char key[32], frame_body[2];
    unsigned i;
    for (i = 0; i < 32u; i++) { key[i] = (unsigned char)i; }
    frame_body[0] = 0x00; frame_body[1] = 0xc9;
    check_signed(key, 32u, NULL, NULL, frame_body, 2u,
                 "0a7461673d0a00c9",
                 "227c7ed07675979193145d78675ecab09be24ad8eac4d2dc3c777fe01fdadbb6",
                 "X-Chirp-Signature over \"\\ntag=\\n\" + body 00c9");
  }
}

int main(int argc, char **argv)
{
  test_vectors(argc > 1 ? argv[1] : NULL);
  test_rules();
  test_announce();
  test_store();
  test_line();
  test_no_open();
  check_summary();
  test_num_limits();
  test_sign();

  printf("\nvaltest: %d passed, %d failed\n", passes, fails);
  printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
