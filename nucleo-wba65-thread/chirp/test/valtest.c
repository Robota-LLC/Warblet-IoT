/* Host checks for this demo's hardware-free code:
 *   - every push of the shared CHIRP-PROV v1.8 vectors' thread profile, merged
 *     (chirpval.c) and stored (flashcfg.c, on a flash of its own) the way
 *     chirp_prov.c does: the answer, whether it is stored, and the settings
 *     the next start reads;
 *   - the rules behind them one by one: the refusals, the Thread object, the
 *     id, the credential;
 *   - the store: two pages; a save whose flash fails at any step, or loses
 *     power at any step, and the newest valid record winning at start;
 *   - datasets, UDP frames, SHA-256, HMAC and the signed input.
 * Run with make test VECTORS=<path to chirp-prov-v1.8.json>; the vectors live
 * with Warblet's checks, not in this folder. What this cannot show: chirp_prov.c
 * itself, which needs OpenThread (its few lines from merge to store are
 * mirrored in push() below), real flash and its ECC, and the console. Those
 * are the bench's. serialtest.c holds the line rules, hwidtest.c the announce. */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "chirpval.h"
#include "chirp_ds.h"
#include "chirp_frame.h"
#include "chirp_hwid.h"
#include "chirp_sign.h"
#include "flashcfg.h"
#include "mbedtls/sha256.h"   /* the host stand-in in chirp/test/ */

static int fails, passes;

static void verdict(int ok, const char *what, const char *detail)
{
  if (ok) {
    passes++;
    printf("ok    %s\n", what);
  } else {
    fails++;
    printf("FAIL  %s%s%s\n", what, detail ? "\n      " : "", detail ? detail : "");
  }
}

/* ---- the flash, two 8 KB pages, with faults ---------------------------------- */
/* Erase sets a page to 0xFF; a 16-byte write needs those bytes erased, as on
 * the chip. A power cut stops everything until power_on(); the step it cuts is
 * left half done (a half-erased page, a torn quadword whose ECC fails on read)
 * or not done at all. One failed operation fails in one of several ways. */

#define FAKE_BASE 0x081FC000u
#define FAKE_SLOT 5u                 /* dual bank, the store in bank 2 */
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
unsigned cfg_flash_slot(void) { return FAKE_SLOT; }

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

/* ---- the board: chirp_prov.c's steps from a push to the store ------------------- */

#define DIE_UID 0xabcdefu
#define DIE_ID  "wba65-thread-abcdef"

static char     s_cfg[CFG_MAX_PAYLOAD + 1];
static unsigned s_cfg_len;
static int      s_cfg_present;
static char     s_merged[CFG_MAX_PAYLOAD + 1];
static char     s_answer[CHIRP_REPLY_MAX];

/* A real start: RAM forgotten, the store read, the id taken from it. */
static void start(void)
{
  power_on();
  chirp_hwid_setup(DIE_UID);
  s_cfg_present = cfg_load(s_cfg, sizeof s_cfg, &s_cfg_len) == 0;
  if (!s_cfg_present) { s_cfg_len = 0u; }
  if (s_cfg_present) { chirp_hwid_config(s_cfg, s_cfg_len); }
}

/* What chirp_prov.c's handle_config() answers a push, up to applying it. */
static const char *push(const char *json)
{
  unsigned mlen = 0u;

  if (!chirp_merge(s_cfg_present ? s_cfg : 0, s_cfg_len, json, (unsigned)strlen(json), chirp_hwid(),
                   s_merged, sizeof s_merged, &mlen, s_answer)) {
    return s_answer;
  }
  if (cfg_store(s_merged, mlen) != 0) { return "CHIRP= err store-failed"; }
  memcpy(s_cfg, s_merged, mlen + 1u);
  s_cfg_len     = mlen;
  s_cfg_present = 1;
  chirp_hwid_config(s_cfg, s_cfg_len);
  return "CHIRP= ok";
}

/* A board that holds `record` (NULL: never set up), just started. */
static void board_with(const char *record)
{
  unsigned n;
  flash_blank();
  (void)cfg_load(s_cfg, sizeof s_cfg, &n);
  if (record != NULL) { (void)cfg_store(record, (unsigned)strlen(record)); }
  start();
}

/* What the next start reads, as text ("{}" when nothing). */
static const char *held_after_restart(void)
{
  static char copy[CFG_MAX_PAYLOAD + 1];
  start();
  snprintf(copy, sizeof copy, "%s", s_cfg_present ? s_cfg : "{}");
  return copy;
}

/* ---- JSON compared as values ------------------------------------------------------ */

static int same_obj(const char *a, unsigned an, const char *b, unsigned bn, int top);

/* Strings by their decoded bytes, numbers by their text, objects in any order,
 * arrays in order. */
static int same_value(const js_val *a, const js_val *b)
{
  static char x[4096], y[4096];
  int nx, ny;

  if (a->t != b->t) { return 0; }
  switch (a->t) {
    case JS_STR:
      nx = js_str_copy(a, x, sizeof x);
      ny = js_str_copy(b, y, sizeof y);
      return nx >= 0 && ny == nx && memcmp(x, y, (size_t)nx) == 0;
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
  unsigned pa = 0u, count_a = 0u, count_b = 0u, pb = 0u;
  js_val   ka, va, kb, vb;
  char     na[64], nb[64];

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

static void test_vectors(const char *path)
{
  char    *doc;
  js_val   pushes, v;
  unsigned pos = 0u;
  int      n = 0;
  static char name[512], pushed[8192], want[128], stored[4096], after[4096], detail[2048];

  puts("--- the shared CHIRP-PROV v1.8 vectors, thread profile (this board) ---");
  if (path == NULL) {
    printf("SKIP  the shared vectors: no path given (make test VECTORS=<path to chirp-prov-v1.8.json>)\n");
    return;
  }
  doc = read_file(path);
  verdict(doc != NULL, "the shared vectors read", path);
  if (doc == NULL) { return; }
  if (js_obj_get(doc, (unsigned)strlen(doc), "pushes", &pushes) != 1 || pushes.t != JS_ARR) {
    verdict(0, "the vectors hold pushes", path);
    free(doc);
    return;
  }
  while (js_arr_next(pushes.p, pushes.n, &pos, &v) == 1) {
    char        profile[32], before[CFG_MAX_PAYLOAD + 1];
    const char *st, *af, *got, *held;
    js_val      stores;
    int         will_store;

    if (strcmp(str_of(&v, "profile", profile, sizeof profile), "thread") != 0) { continue; }
    str_of(&v, "name", name, sizeof name);
    str_of(&v, "push", pushed, sizeof pushed);
    str_of(&v, "answer", want, sizeof want);
    will_store = js_obj_get(v.p, v.n, "stores", &stores) == 1 && stores.t == JS_TRUE;
    st = obj_text(&v, "stored", stored, sizeof stored);
    af = obj_text(&v, "after", after, sizeof after);

    board_with(st);
    verdict(st == NULL || s_cfg_present, "the stored settings load", name);
    snprintf(before, sizeof before, "%s", s_cfg_present ? s_cfg : "{}");

    got = push(pushed);
    snprintf(detail, sizeof detail, "%s\n      got  %s\n      want %s", name, got, want);
    verdict(strcmp(got, want) == 0, "a v1.8 push gets the contract's answer", detail);

    held = held_after_restart();
    if (will_store) {
      snprintf(detail, sizeof detail, "%s\n      held %s\n      want %s", name, held, af ? af : "{}");
      verdict(same_record(held, af ? af : "{}"), "  ... and the next start reads the vector's settings", detail);
    } else {
      snprintf(detail, sizeof detail, "%s\n      held %s", name, held);
      verdict(strcmp(held, before) == 0 && same_record(held, af ? af : "{}"),
              "  ... and a refusal leaves the record as it was", detail);
    }
    n++;
  }
  verdict(n >= 15, "the vectors hold the thread profile's pushes", NULL);
  printf("      %d thread pushes answered and stored as the contract says\n", n);

  {
    js_val profiles, thread, vcaps;
    char   vc[512];
    verdict(js_obj_get(doc, (unsigned)strlen(doc), "profiles", &profiles) == 1 &&
            js_obj_get(profiles.p, profiles.n, "thread", &thread) == 1 &&
            js_obj_get(thread.p, thread.n, "caps", &vcaps) == 1 && js_str_copy(&vcaps, vc, sizeof vc) > 0 &&
            strcmp(vc, CHIRP_CAPS) == 0, "CHIRP_CAPS is the vectors' thread profile", NULL);
  }
  free(doc);
}

/* ---- the rules one by one --------------------------------------------------------- */

#define KEY44 "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="
#define KEY_B "BBECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="
#define TKEY  "00112233445566778899aabbccddeeff"
#define SET   "{\"token\":\"tok\",\"thread\":{\"dataset\":\"0e08aabb\"}}"

static void expect(const char *record, const char *json, const char *want, const char *why)
{
  char        detail[1024];
  const char *got;

  board_with(record);
  got = push(json);
  snprintf(detail, sizeof detail, "got %s\n      push %s", got, json);
  verdict(strcmp(got, want) == 0, why, detail);
}

static void expect_held(const char *record, const char *json, const char *want, const char *why)
{
  char        detail[2048];
  const char *got, *held;

  board_with(record);
  got = push(json);
  held = held_after_restart();
  snprintf(detail, sizeof detail, "answer %s\n      held %s\n      want %s", got, held, want);
  verdict(strcmp(got, "CHIRP= ok") == 0 && same_record(held, want), why, detail);
}

/* A push on a board never set up: ok, or the refusal's word. */
static void check(const char *json, const char *want, const char *why)
{
  char line[CHIRP_REPLY_MAX];
  snprintf(line, sizeof line, want ? "CHIRP= err %s" : "CHIRP= ok", want);
  expect(NULL, json, line, why);
}

static void test_rules(void)
{
  puts("\n--- refusals (CHIRP-PROV v1.8) ---");
  /* These two were answered ok before v1.8. */
  expect(NULL, "{\"ssid\":\"workshop\",\"pass\":\"hunter2\",\"aps\":[{\"ssid\":\"workshop\",\"pass\":\"\"}],"
         "\"host\":\"h\",\"claim\":\"C\",\"level\":1}", "CHIRP= err unsupported aps",
         "a push with level and networks: the name that sorts first, aps");
  expect(NULL, "{\"claim\":\"C\",\"thread\":{\"name\":\"n\",\"key\":\"" TKEY "\"},\"unknown\":{\"deep\":[1,2,3]}}",
         "CHIRP= err unsupported unknown", "an unknown field is refused by name");
  expect(SET, "{\"level\":1}", "CHIRP= err unsupported level", "level");
  expect(SET, "{\"tls\":false}", "CHIRP= err unsupported tls", "tls");
  expect(SET, "{\"ssid\":\"s\"}", "CHIRP= err unsupported ssid", "ssid: no Wi-Fi on this board");
  expect(SET, "{\"pass\":\"p\"}", "CHIRP= err unsupported pass", "pass");
  expect(SET, "{\"two words\":1}", "CHIRP= err bad-json", "a name a refusal cannot carry is bad-json");
  expect(SET, "{\"token\":\"\"}", "CHIRP= err bad-json", "an empty token is refused (no send at Open)");
  expect(SET, "{\"claim\":\"\"}", "CHIRP= err bad-json", "an empty setup code is refused");
  expect(SET, "{\"key\":\"\"}", "CHIRP= err bad-json", "an empty key is refused");
  expect(SET, "{\"key\":\"not-base64\"}", "CHIRP= err bad-json", "a malformed signing key");
  expect(SET, "{\"key\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8\"}", "CHIRP= err bad-json", "a 43-character key");
  expect(SET, "{\"key\":\"" KEY44 "\",\"claim\":\"C\",\"token\":\"T\"}", "CHIRP= err bad-json", "claim and token together");
  expect(SET, "{\"host\":null}", "CHIRP= err bad-json", "host null");

  puts("\n--- completeness: a credential and a thread object ---");
  check("{\"key\":\"" KEY44 "\"}", "missing-field", "a key alone at a board never set up");
  check("{\"token\":\"T\"}", "missing-field", "a credential alone has nothing to join");
  check("{\"thread\":{\"dataset\":\"0e08\"}}", "missing-field", "a thread object alone has no credential");
  check("{\"token\":\"T\",\"thread\":{\"dataset\":\"0e08\"}}", 0, "a credential and a thread object");
  expect_held(SET, "{\"key\":\"" KEY44 "\"}", "{\"token\":\"tok\",\"thread\":{\"dataset\":\"0e08aabb\"},\"key\":\"" KEY44 "\"}",
              "a key alone keeps the token and the dataset");
  expect_held(SET, "{\"key\":\"" KEY44 "\",\"token\":\"tok-new\"}",
              "{\"token\":\"tok-new\",\"thread\":{\"dataset\":\"0e08aabb\"},\"key\":\"" KEY44 "\"}",
              "a key and a token: the token is replaced, the network kept");
  expect_held("{\"claim\":\"CHIRP-OLD\",\"thread\":{\"dataset\":\"0e08\"}}", "{\"token\":\"T\"}",
              "{\"token\":\"T\",\"thread\":{\"dataset\":\"0e08\"}}", "a token replaces a stored claim");

  puts("\n--- the id, the credential, the host ---");
  expect(SET, "{\"hwid\":\"0123456789abcdef0123456789abcdef0123456789abcde\"}", "CHIRP= ok",
         "an id of 47, the most this board holds");
  expect(SET, "{\"hwid\":\"0123456789abcdef0123456789abcdef0123456789abcdef\"}", "CHIRP= err bad-json",
         "an id of 48 is refused, not cut short");
  expect(SET, "{\"hwid\":\"id\\r\\nCHIRP! v=2 hw=other\"}", "CHIRP= err bad-json", "a line break cannot forge an announce");
  expect(SET, "{\"hwid\":\"caf\\u00e9\"}", "CHIRP= err bad-json", "an id past ASCII");
  expect(SET, "{\"hwid\":7}", "CHIRP= err bad-json", "an id that is not text");
  expect(SET, "{\"token\":\"ttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttt\"}", "CHIRP= ok", "a token of 63");
  expect(SET, "{\"token\":\"tttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttt\"}", "CHIRP= err bad-json",
         "a token of 64");
  expect(SET, "{\"host\":\"h012345678901234567890123456789012345678901234567890123.example\"}", "CHIRP= ok", "a host of 63");
  expect(SET, "{\"host\":\"h0123456789012345678901234567890123456789012345678901234.example\"}", "CHIRP= err bad-json",
         "a host of 64");
  expect(SET, "{\"host\":5}", "CHIRP= err bad-json", "a host that is not text");

  puts("\n--- the new-device rule ---");
  {
    const char *signed_cfg = "{\"token\":\"tok-old\",\"key\":\"" KEY44 "\",\"hwid\":\"curious-otter-1a2b\","
                             "\"thread\":{\"dataset\":\"0e08aabb\"}}";
    expect_held(signed_cfg, "{\"claim\":\"CHIRP-NEW\",\"hwid\":\"sunny-otter-7c2f\"}",
                "{\"claim\":\"CHIRP-NEW\",\"hwid\":\"sunny-otter-7c2f\",\"thread\":{\"dataset\":\"0e08aabb\"}}",
                "a claim and a new id: the old key goes");
    expect_held(signed_cfg, "{\"token\":\"t2\",\"hwid\":\"sunny-otter-7c2f\"}",
                "{\"token\":\"t2\",\"hwid\":\"sunny-otter-7c2f\",\"thread\":{\"dataset\":\"0e08aabb\"}}",
                "a token with a different id: the old key goes");
    expect_held(signed_cfg, "{\"claim\":\"CHIRP-NEW\",\"key\":\"" KEY_B "\"}",
                "{\"claim\":\"CHIRP-NEW\",\"key\":\"" KEY_B "\",\"hwid\":\"curious-otter-1a2b\",\"thread\":{\"dataset\":\"0e08aabb\"}}",
                "a claim that brings its own key: that key is kept");
    expect_held(signed_cfg, "{\"token\":\"t2\",\"hwid\":\"CURIOUS-OTTER-1A2B\"}",
                "{\"token\":\"t2\",\"hwid\":\"CURIOUS-OTTER-1A2B\",\"thread\":{\"dataset\":\"0e08aabb\"}}",
                "an id that differs only in case is another device: the key goes");
    expect_held(signed_cfg, "{\"thread\":{\"dataset\":\"0e08ccdd\"}}",
                "{\"thread\":{\"dataset\":\"0e08ccdd\"},\"token\":\"tok-old\",\"key\":\"" KEY44 "\",\"hwid\":\"curious-otter-1a2b\"}",
                "a network alone keeps the key");
    expect_held("{\"token\":\"tok-old\",\"key\":\"" KEY44 "\",\"thread\":{\"dataset\":\"0e08\"}}",
                "{\"token\":\"t2\",\"hwid\":\"" DIE_ID "\"}",
                "{\"token\":\"t2\",\"hwid\":\"" DIE_ID "\",\"key\":\"" KEY44 "\",\"thread\":{\"dataset\":\"0e08\"}}",
                "the die id it already answers to keeps the key");
    board_with(signed_cfg);
    push("{\"hwid\":\"\"}");
    verdict(strcmp(chirp_hwid(), DIE_ID) == 0, "an empty id puts the board back on its own id at once", chirp_hwid());
  }

  puts("\n--- the Thread object ---");
  expect_held(SET, "{\"thread\":{\"dataset\":\"0e08\",\"name\":\"x\",\"key\":\"nope\"}}",
              "{\"token\":\"tok\",\"thread\":{\"dataset\":\"0e08\"}}", "the dataset wins, and is all that is kept");
  expect_held(SET, "{\"thread\":{\"name\":\"chirp-lab\",\"key\":\"" TKEY "\",\"panid\":\"1234\",\"channel\":15,\"x\":1}}",
              "{\"token\":\"tok\",\"thread\":{\"name\":\"chirp-lab\",\"key\":\"" TKEY "\",\"panid\":\"1234\",\"channel\":15}}",
              "the named form keeps what it reads");
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"n\"}}", "missing-field", "a name without a key");
  check("{\"claim\":\"C\",\"thread\":{\"key\":\"" TKEY "\"}}", "missing-field", "a key without a name");
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"n\",\"key\":\"00112233\"}}", "bad-json", "a key too short");
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"0123456789abcdefg\",\"key\":\"" TKEY "\"}}", "bad-json", "a name of 17");
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"n\",\"key\":\"" TKEY "\",\"channel\":27}}", "bad-json", "channel 27");
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"n\",\"key\":\"" TKEY "\",\"panid\":\"12g4\"}}", "bad-json", "panid not hex");
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"n\",\"key\":\"" TKEY "\",\"xpanid\":\"1111\"}}", "bad-json", "xpanid short");
  check("{\"claim\":\"C\",\"thread\":{\"dataset\":\"0e0\"}}", "bad-json", "an odd-length dataset");
  check("{\"claim\":\"C\",\"thread\":\"not-an-object\"}", "bad-json", "thread must be an object");
  check("{\"claim\":123,\"thread\":{\"dataset\":\"0e08\"}}", "bad-json", "a claim that is not text");
  check("[1,2,3]", "bad-json", "not an object");
  check("{\"claim\":\"C\",\"thread\":{", "bad-json", "a truncated object");
  check("{\"claim\":\"C\",\"thread\":{\"dataset\":\"0e08\"}} {}", "bad-json", "two objects on one line");

  puts("\n--- a stored record ---");
  /* The record before v1.8 held every key a push carried; now only what the board takes. */
  expect_held("{\"token\":\"T\",\"ssid\":\"s\",\"level\":2,\"colour\":\"red\",\"thread\":{\"dataset\":\"0e08\"}}",
              "{\"hwid\":\"h\"}", "{\"token\":\"T\",\"hwid\":\"h\",\"thread\":{\"dataset\":\"0e08\"}}",
              "fields the board does not take are not carried over");
  expect("{\"token\":\"tttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttt\",\"thread\":{\"dataset\":\"0e08\"}}",
         "{\"hwid\":\"wba-2\"}", "CHIRP= err missing-field", "a stored token over the bound is not carried");

  puts("\n--- the longest record ---");
  {
    static char big[2048];
    char       *p = big;
    unsigned    n;
    int         i;

    p += sprintf(p, "{\"thread\":{\"dataset\":\"");
    for (i = 0; i < 508; i++) { *p++ = "0123456789abcdef"[i % 16]; }
    p += sprintf(p, "\"},\"claim\":\"");
    for (i = 0; i < 63; i++) { p += sprintf(p, "\\\""); }
    p += sprintf(p, "\",\"hwid\":\"");
    for (i = 0; i < 47; i++) { *p++ = 'h'; }
    p += sprintf(p, "\",\"host\":\"");
    for (i = 0; i < 63; i++) { *p++ = 'n'; }
    p += sprintf(p, "\",\"key\":\"" KEY44 "\"}");
    board_with(NULL);
    verdict(strcmp(push(big), "CHIRP= ok") == 0, "the longest push is taken", s_answer);
    n = s_cfg_len;
    printf("      the longest record is %u bytes; CHIRP_RECORD_MAX %u, the store %u\n",
           n, (unsigned)CHIRP_RECORD_MAX, (unsigned)CFG_MAX_PAYLOAD);
    verdict(n <= CHIRP_RECORD_MAX && CHIRP_RECORD_MAX <= CFG_MAX_PAYLOAD && n + 16u > CHIRP_RECORD_MAX,
            "  ... and fits CHIRP_RECORD_MAX, within 16 bytes of it, which fits the store", NULL);
    verdict(same_record(held_after_restart(), big), "  ... and the next start reads every value of it", NULL);
  }
}

/* ---- the store -------------------------------------------------------------------- */

static const char OLD[] = "{\"token\":\"tok-old\",\"thread\":{\"dataset\":\"0e08\"}}";
static const char NEW[] = "{\"thread\":{\"name\":\"chirp-lab\",\"key\":\"" TKEY "\"},\"token\":\"tok-new\","
                          "\"hwid\":\"board-new\",\"key\":\"" KEY44 "\"}";

static const char *reads(void)
{
  static char copy[CFG_MAX_PAYLOAD + 1];
  unsigned    n;
  power_on();
  if (cfg_load(copy, sizeof copy, &n) != 0) { copy[0] = '\0'; }
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

/* A record written the way flashcfg.c writes one: 'CWB1', version 3, the slot
 * tag, the length, seq, and the CRC-32 of seq, length and payload. */
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
  h[4] = 3u; h[5] = FAKE_SLOT;
  h[6] = (unsigned char)len; h[7] = (unsigned char)(len >> 8);
  memcpy(flash_mem[page], h, 16u);
  memset(flash_mem[page] + 16, 0, (len + 15u) & ~15u);
  memcpy(flash_mem[page] + 16, json, len);
}

static void fresh_old(void)
{
  unsigned n;
  char     tmp[CFG_MAX_PAYLOAD + 1];
  flash_blank();
  (void)cfg_load(tmp, sizeof tmp, &n);
  (void)cfg_store(OLD, sizeof OLD - 1u);
  power_on();
  (void)cfg_load(tmp, sizeof tmp, &n);
}

static void test_store(void)
{
  char     what[200], tmp[CFG_MAX_PAYLOAD + 1];
  unsigned n;
  int      total, k, kind, mode, good;

  puts("\n--- the store: two pages, the newest valid record wins ---");
  flash_blank();
  verdict(cfg_load(tmp, sizeof tmp, &n) == -1, "a blank store holds nothing", NULL);
  verdict(cfg_store(OLD, sizeof OLD - 1u) == 0 && strcmp(reads(), OLD) == 0 && cfg_page_addr() == FAKE_BASE,
          "a save is read back whole, from the first page", NULL);
  verdict(cfg_store(NEW, sizeof NEW - 1u) == 0 && strcmp(reads(), NEW) == 0 &&
          cfg_page_addr() == FAKE_BASE + CFG_PAGE_SIZE && blank_page(0),
          "a second save: the other page, and the old page erased", NULL);

  flash_blank();
  put_record(1, 41u, NEW);
  verdict(strcmp(reads(), NEW) == 0, "a record written by hand in the format is read", NULL);
  power_on();
  verdict(cfg_store(OLD, sizeof OLD - 1u) == 0 && blank_page(1), "the next save goes to the first page", NULL);
  put_record(1, 41u, NEW);
  verdict(strcmp(reads(), OLD) == 0, "  ... with seq 42, so it beats the 41 written back beside it", NULL);
  {
    unsigned seqs[3][2] = { {5u, 6u}, {7u, 6u}, {0xFFFFFFFFu, 0u} };
    for (int c = 0; c < 3; c++) {
      flash_blank();
      put_record(0, seqs[c][0], OLD);
      put_record(1, seqs[c][1], NEW);
      snprintf(what, sizeof what, "seq %u against seq %u: the newer wins", seqs[c][0], seqs[c][1]);
      verdict(strcmp(reads(), ((int)(seqs[c][1] - seqs[c][0]) > 0) ? NEW : OLD) == 0, what, NULL);
    }
  }
  flash_blank();
  put_record(0, 1u, OLD);
  put_record(1, 2u, NEW);
  flash_torn[1][0] = 1;
  verdict(strcmp(reads(), OLD) == 0, "a newer page that cannot be read (ECC): the older record is read", NULL);
  put_record(1, 2u, NEW);
  flash_mem[1][20] ^= 1u;
  verdict(strcmp(reads(), OLD) == 0, "a newer page whose CRC fails: the older record is read", NULL);
  put_record(1, 2u, NEW);
  flash_mem[1][5] = 1u;
  verdict(strcmp(reads(), OLD) == 0, "a newer page written under another bank geometry is not ours", NULL);
  flash_torn[0][1] = 1;
  flash_torn[1][1] = 1;
  verdict(cfg_load(tmp, sizeof tmp, &n) == -2, "neither page readable: nothing held, and said so (-2)", NULL);
  flash_blank();
  memcpy(flash_mem[1], "\x43\x57\x42\x31\x02\x00\x10\x00", 8u);    /* version 2, one page: not read */
  verdict(cfg_load(tmp, sizeof tmp, &n) == -1, "a record of the one-page store before (version 2) is not read", NULL);

  /* The steps one save takes. */
  fresh_old();
  steps = 0;
  (void)cfg_store(NEW, sizeof NEW - 1u);
  total = steps;
  printf("      a save of %u bytes over another takes %d flash steps\n", (unsigned)sizeof NEW - 1u, total);

  good = 1;
  for (kind = 0; kind < FAIL_KINDS; kind++) {
    for (k = 0; k < total; k++) {
      int r;
      const char *held;
      fresh_old();
      fail_at = k; fail_kind = kind;
      r = cfg_store(NEW, sizeof NEW - 1u);
      held = reads();
      if (strcmp(held, r == 0 ? NEW : OLD) != 0) {
        snprintf(what, sizeof what, "failure kind %d at step %d: answered %d, next start read %s", kind, k, r, held);
        verdict(0, what, NULL);
        good = 0;
      }
      power_on();
      (void)cfg_load(tmp, sizeof tmp, &n);
      if (cfg_store(OLD, sizeof OLD - 1u) != 0 || strcmp(reads(), OLD) != 0) { good = 0; }
    }
  }
  verdict(good, "one failed flash operation at any step: the answer is what the next start reads", NULL);

  good = 1;
  for (mode = CUT_TORN; mode <= CUT_CLEAN; mode++) {
    for (k = 0; k <= total; k++) {
      int r;
      const char *held;
      fresh_old();
      cut_at = k; cut_mode = mode;
      r = cfg_store(NEW, sizeof NEW - 1u);
      held = reads();
      if (!((strcmp(held, OLD) == 0 || strcmp(held, NEW) == 0) && (r != 0 || strcmp(held, NEW) == 0))) {
        snprintf(what, sizeof what, "power cut (%s) at step %d: answered %d, next start read %s",
                 mode == CUT_TORN ? "torn" : "clean", k, r, held);
        verdict(0, what, NULL);
        good = 0;
      }
      power_on();
      (void)cfg_load(tmp, sizeof tmp, &n);
      if (cfg_store(OLD, sizeof OLD - 1u) != 0 || strcmp(reads(), OLD) != 0) { good = 0; }
    }
  }
  verdict(good, "a power cut at any step, torn or clean: old or new, whole, and new when it said ok", NULL);

  board_with(OLD);
  fail_at = 0; fail_kind = FAIL_NOTHING;
  verdict(strcmp(push("{\"token\":\"tok-2\"}"), "CHIRP= err store-failed") == 0, "a store the flash refuses: store-failed",
          s_answer);
  verdict(strcmp(held_after_restart(), OLD) == 0, "  ... and the next start reads the old settings", NULL);
}

static void check_summary(const char *json)
{
  js_val t, v;
  unsigned len = (unsigned)strlen(json);
  char buf[64];

  if (js_obj_get(json, len, "thread", &t) != 1 || t.t != JS_OBJ) {
    printf("FAIL  summary: no thread object\n");
    fails++;
    return;
  }
  if (js_obj_get(t.p, t.n, "name", &v) == 1 && js_str_copy(&v, buf, sizeof buf) > 0) {
    printf("ok    summary name=%s\n", buf);
  }
  if (js_obj_get(t.p, t.n, "key", &v) == 1) {
    printf("ok    summary key present (%u hex chars)\n", v.n);
  }
  if (js_obj_get(t.p, t.n, "channel", &v) == 1) {
    long ch = 0;
    if (js_num_int(&v, &ch) == 0) { printf("ok    summary channel=%ld\n", ch); }
  }
}

/* ---------------------------------------------------------------- chirp_ds.c */

static void ds_expect(const char *json, int want_rc, const char *why)
{
  chirp_ds_t ds;
  int rc = chirp_ds_parse(json, (unsigned)strlen(json), &ds);
  if (rc != want_rc) {
    printf("FAIL  ds rc=%d want %d  %s\n     %s\n", rc, want_rc, why, json);
    fails++;
  } else {
    printf("ok    ds rc=%-2d %s\n", rc, why);
  }
}

static void ds_field(int cond, const char *why)
{
  if (cond) { printf("ok    ds %s\n", why); }
  else      { printf("FAIL  ds %s\n", why); fails++; }
}

static void hex_expect(const char *hex, int want, const char *why)
{
  unsigned char buf[8];
  int got = chirp_hex2bin(hex, (unsigned)strlen(hex), buf, sizeof buf);
  if (got != want) {
    printf("FAIL  hex got %d want %d  %s (%s)\n", got, want, why, hex);
    fails++;
  } else {
    printf("ok    hex %-3d %s\n", got, why);
  }
}

static void test_ds(const char *full, const char *dataset)
{
  chirp_ds_t ds;

  puts("\n--- chirp_hex2bin ---");
  hex_expect("00ff", 2, "lowercase");
  hex_expect("00FF", 2, "uppercase");
  hex_expect("0", -1, "odd length");
  hex_expect("0g", -1, "not hex");
  hex_expect("", -1, "empty");
  hex_expect("00112233445566778899", -1, "overflows an 8-byte buffer");
  hex_expect("0011223344556677", 8, "exactly fills the buffer");

  puts("\n--- chirp_ds_parse ---");
  ds_expect(full, 1, "discrete fields extracted");
  ds_expect(dataset, 1, "dataset form extracted");
  ds_expect("{\"claim\":\"C\",\"ssid\":\"s\"}", 0, "no thread object");

  /* discrete form: every field lands where chirp_prov.c expects it */
  memset(&ds, 0, sizeof ds);
  chirp_ds_parse(full, (unsigned)strlen(full), &ds);
  ds_field(!ds.have_tlv,                        "discrete: no TLV blob");
  ds_field(ds.have_key && ds.key[0] == 0x00 && ds.key[15] == 0xff, "discrete: key bytes");
  ds_field(ds.have_name && strcmp(ds.name, "chirp-lab") == 0,      "discrete: name");
  ds_field(ds.have_panid && ds.panid == 0x1234u,                   "discrete: panid 0x1234");
  ds_field(ds.have_xpanid && ds.xpanid[0] == 0x11 && ds.xpanid[7] == 0x22, "discrete: xpanid");
  ds_field(ds.have_channel && ds.channel == 15u,                   "discrete: channel 15");

  /* dataset form: TLV decoded, and it must shadow the discrete fields */
  memset(&ds, 0, sizeof ds);
  chirp_ds_parse(dataset, (unsigned)strlen(dataset), &ds);
  ds_field(ds.have_tlv,                         "dataset: TLV present");
  ds_field(ds.tlv_len == 72u,                   "dataset: 72 bytes decoded");
  ds_field(ds.tlv[0] == 0x0e && ds.tlv[1] == 0x08, "dataset: first TLV is Active Timestamp");
  {
    /* Check that the dataset's type-length-value entries end at the buffer boundary. */
    unsigned i = 0, n = 0;
    while (i + 2u <= ds.tlv_len) { i += 2u + ds.tlv[i + 1u]; n++; }
    ds_field(i == ds.tlv_len && n == 7u, "dataset: TLV chain walks to exactly 7 TLVs");
  }
  ds_field(!ds.have_key && !ds.have_name,       "dataset: discrete fields not populated");

  /* dataset wins even over broken discrete fields, as in chirpval.c */
  ds_expect("{\"claim\":\"C\",\"thread\":{\"dataset\":\"0e08\",\"name\":\"x\",\"key\":\"nope\"}}",
            1, "dataset wins over broken discrete fields");

  /* validation refuses these too; the extractor must not rely on it */
  ds_expect("{\"claim\":\"C\",\"thread\":{\"dataset\":\"0e0\"}}", -1, "odd-length dataset refused");
  ds_expect("{\"claim\":\"C\",\"thread\":{\"key\":\"00112233\"}}", -1, "short key refused");

  /* Valid hex can contain an invalid TLV length. OpenThread must reject this dataset. */
  {
    const char *ds_malformed =
      "{\"claim\":\"C\",\"thread\":{\"dataset\":\"0e080000000000010000000300001835"
      "060004001fffe00208111111112222222207081111111122222222051000112233445566778899aabbccddeeff"
      "030a63686972702d6c6162\"}}";
    chirp_ds_t bad;
    unsigned   i = 0, n = 0;

    check(ds_malformed, 0, "TLV-malformed dataset still passes validation (hex-only check)");
    ds_expect(ds_malformed, 1, "TLV-malformed dataset still decodes to bytes");
    chirp_ds_parse(ds_malformed, (unsigned)strlen(ds_malformed), &bad);
    while (i + 2u <= bad.tlv_len) { i += 2u + bad.tlv[i + 1u]; n++; }
    ds_field(i > bad.tlv_len, "TLV-malformed dataset overruns its chain (OT will refuse it)");
  }
}

/* The wire bytes are the contract; pin them byte for byte. */
static void test_frame(void)
{
  /* A placeholder digest: the layout does not depend on its value. test_sign()
   * checks a real signature in a real datagram. */
  static const char sig[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  unsigned char payload[CHIRP_FRAME_PAYLOAD_LEN];
  char          buf[192];
  unsigned      n, pn, state;

  puts("\n--- udp frame ---");

  pn = chirp_frame_payload(payload, sizeof payload, 214u);
  if (pn == 2u && payload[0] == 0x00u && payload[1] == 0xd6u) {
    printf("ok    payload: 21.4C -> 00 d6, big-endian tenths\n");
  } else {
    printf("FAIL  payload: 21.4C gave %u bytes\n", pn);
    fails++;
  }

  n = chirp_frame_build(buf, sizeof buf, "hw-01", "tok9", NULL, payload, pn);
  if (n == 20u && memcmp(buf, "CHIRP1 hw-01 tok9 \x00\xd6", 20u) == 0) {
    printf("ok    frame: L1, token alone\n");
  } else {
    printf("FAIL  frame: L1 form (n=%u)\n", n);
    fails++;
  }

  n = chirp_frame_build(buf, sizeof buf, "hw-01", "tok9", sig, payload, pn);
  if (n == 85u &&
      memcmp(buf, "CHIRP1 hw-01 tok9:", 18u) == 0 &&
      memcmp(buf + 18, sig, 64u) == 0 &&
      memcmp(buf + 82, " \x00\xd6", 3u) == 0) {
    printf("ok    frame: L2, token:hex in the SAME field, payload untouched\n");
  } else {
    printf("FAIL  frame: L2 form (n=%u)\n", n);
    fails++;
  }

  n = chirp_frame_build(buf, sizeof buf, "hw-01", "tok9", "", payload, pn);
  if (n == 20u && memcmp(buf, "CHIRP1 hw-01 tok9 \x00\xd6", 20u) == 0) {
    printf("ok    frame: an empty signature is no signature\n");
  } else {
    printf("FAIL  frame: empty signature (n=%u)\n", n);
    fails++;
  }

  pn = chirp_frame_payload(payload, sizeof payload, 400u);
  n  = chirp_frame_build(buf, sizeof buf, "hw-01", "", NULL, payload, pn);
  if (n == 17u && memcmp(buf, "CHIRP1 hw-01 - \x01\x90", 17u) == 0) {
    printf("ok    frame: empty token becomes the L0 marker\n");
  } else {
    printf("FAIL  frame: L0 form (n=%u)\n", n);
    fails++;
  }

  if (chirp_frame_build(buf, sizeof buf, "", "t", NULL, payload, pn) == 0u) {
    printf("ok    frame: empty hwid refused\n");
  } else {
    printf("FAIL  frame: empty hwid accepted\n");
    fails++;
  }

  if (chirp_frame_build(buf, 10u, "a-very-long-hardware-id", "t", NULL, payload, pn) == 0u) {
    printf("ok    frame: refuses to overflow the buffer\n");
  } else {
    printf("FAIL  frame: overflowed\n");
    fails++;
  }

  if (chirp_frame_build(buf, 24u, "hw-01", "tok9", sig, payload, pn) == 0u) {
    printf("ok    frame: a signature that would not fit is refused, not truncated\n");
  } else {
    printf("FAIL  frame: truncated a signature\n");
    fails++;
  }

  state = 200u;
  n = next_demo_temperature_tenths_c(&state);
  if (n == 201u) { printf("ok    ramp: first reading is 20.1C\n"); }
  else           { printf("FAIL  ramp: first reading %u\n", n); fails++; }

  state = 400u;
  n = next_demo_temperature_tenths_c(&state);
  if (n == 200u) { printf("ok    ramp: wraps 40.0C -> 20.0C\n"); }
  else           { printf("FAIL  ramp: wrap gave %u\n", n); fails++; }

  state = 0u;
  n = next_demo_temperature_tenths_c(&state);
  if (n == 200u) { printf("ok    ramp: out-of-band state restarts the ramp\n"); }
  else           { printf("FAIL  ramp: bad state gave %u\n", n); fails++; }
}

/* Signing checks. */

/* The contract's example key, the 32 ASCII bytes "0123456789abcdef" twice, in
 * the base64 form the config stores. Visibly not a real credential. */
static const char vector_key_b64[] = "MDEyMzQ1Njc4OWFiY2RlZjAxMjM0NTY3ODlhYmNkZWY=";

#define BODY_MAX 64u

static void hex_of(const unsigned char *d, unsigned n, char *out)
{
  static const char H[] = "0123456789abcdef";
  unsigned i;
  for (i = 0u; i < n; i++) {
    out[i * 2u]      = H[d[i] >> 4];
    out[i * 2u + 1u] = H[d[i] & 0xFu];
  }
  out[n * 2u] = '\0';
}

static void check_hex(const char *got, const char *want, const char *why)
{
  if (strcmp(got, want) != 0) {
    printf("FAIL  %s\n      want %s\n      got  %s\n", why, want, got);
    fails++;
  } else {
    printf("ok    %s\n", why);
  }
}

/* SHA-256 through the host stand-in, so the signer below is not trusted blind. */
static void check_sha(const char *msg, const char *want, const char *why)
{
  mbedtls_sha256_context c;
  unsigned char          d[32];
  char                   got[65];
  int                    rc;

  mbedtls_sha256_init(&c);
  rc = mbedtls_sha256_starts(&c, 0);
  if (rc == 0) { rc = mbedtls_sha256_update(&c, (const unsigned char *)msg, strlen(msg)); }
  if (rc == 0) { rc = mbedtls_sha256_finish(&c, d); }
  mbedtls_sha256_free(&c);
  if (rc != 0) {
    printf("FAIL  %s: sha256 returned %d\n", why, rc);
    fails++;
    return;
  }
  hex_of(d, 32u, got);
  check_hex(got, want, why);
}

/* chirp_sign_hex straight over msg, no prefix: the HMAC itself. */
static void check_hmac(const char *key_b64, const char *msg, const char *want, const char *why)
{
  char got[CHIRP_SIGN_HEX_LEN + 1];

  if (chirp_sign_hex(got, sizeof got, key_b64, (unsigned)strlen(key_b64),
                     (const unsigned char *)msg, (unsigned)strlen(msg)) != 0) {
    printf("FAIL  %s: chirp_sign_hex refused\n", why);
    fails++;
    return;
  }
  check_hex(got, want, why);
}

/* Build the signed input the way chirp_report.c does (chirp_sign_prefix, then
 * the payload), check those bytes as hex, then check the HMAC from
 * chirp_sign_hex. NULL means the message has no such field. */
static void check_signed(const char *key_b64, const char *nonce, const char *tag,
                         const void *body, unsigned blen,
                         const char *want_input_hex, const char *want_hmac,
                         const char *why)
{
  unsigned char input[CHIRP_SIGN_PREFIX_MAX + BODY_MAX];
  char          hex[2u * sizeof input + 1u];
  char          got[CHIRP_SIGN_HEX_LEN + 1];
  int           plen = chirp_sign_prefix((char *)input, CHIRP_SIGN_PREFIX_MAX, nonce, tag);

  if (plen < 0 || blen > BODY_MAX) {
    printf("FAIL  %s: the signed input did not fit\n", why);
    fails++;
    return;
  }
  memcpy(input + plen, body, blen);
  hex_of(input, (unsigned)plen + blen, hex);
  if (strcmp(hex, want_input_hex) != 0) {
    printf("FAIL  %s: signed input\n      want %s\n      got  %s\n", why, want_input_hex, hex);
    fails++;
    return;
  }
  if (chirp_sign_hex(got, sizeof got, key_b64, (unsigned)strlen(key_b64),
                     input, (unsigned)plen + blen) != 0) {
    printf("FAIL  %s: chirp_sign_hex refused\n", why);
    fails++;
    return;
  }
  check_hex(got, want_hmac, why);
}

static void test_sign(void)
{
  static const char nonce[] = "1757332800123";
  static const char body[]  = "temp=21.4";
  static const char moved[] = "1757332800123\ntag=snap\ntemp=21.4";

  puts("\n--- sha-256 host stand-in (FIPS 180-4 examples) ---");
  check_sha("abc",
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "sha256(\"abc\")");
  check_sha("",
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "sha256(\"\"), the empty message");
  check_sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
            "sha256(56 bytes), padding spills into a second block");

  puts("\n--- hmac-sha256 through chirp_sign_hex (RFC 4231) ---");
  check_hmac("CwsLCwsLCwsLCwsLCwsLCwsLCws=", "Hi There",
             "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
             "RFC 4231 case 1");
  check_hmac("SmVmZQ==", "what do ya want for nothing?",
             "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
             "RFC 4231 case 2, key shorter than the block");

  puts("\n--- signed input: <nonce> \\n tag=<tag> \\n <body> (contract examples) ---");
  check_signed(vector_key_b64, nonce, "snap", body, 9u,
               "313735373333323830303132330a7461673d736e61700a74656d703d32312e34",
               "1239148f97a361adfb7173445a84a68a4f0b3eb915d626b98e1080f7e6f78e81",
               "nonce and tag");
  check_signed(vector_key_b64, NULL, NULL, body, 9u,
               "0a7461673d0a74656d703d32312e34",
               "fd071ea6328d4ca04bf766d76c4298359a07db6b161fbc8446b336511954bda2",
               "neither: \"\\ntag=\\n\" + body, the shape this demo signs");
  check_signed(vector_key_b64, "", "", body, 9u,
               "0a7461673d0a74656d703d32312e34",
               "fd071ea6328d4ca04bf766d76c4298359a07db6b161fbc8446b336511954bda2",
               "empty strings sign the same as no fields");
  check_signed(vector_key_b64, NULL, "snap", body, 9u,
               "0a7461673d736e61700a74656d703d32312e34",
               "aaa23e9e54a70f6c76ff1bfdc282831c3c62c0d574f592fdd831b554bef3e946",
               "tag only: the empty nonce line is still there");
  check_signed(vector_key_b64, NULL, NULL, "", 0u,
               "0a7461673d0a",
               "e33be001a2833da36aae83c191aa790e65ef7d07bbc061f9b236eed063e0eb38",
               "empty body: only the two lines are signed");
  check_signed(vector_key_b64, NULL, NULL, "a\nb\n", 4u,
               "0a7461673d0a610a620a",
               "cb339ac72c21c9ce109fe397965bacd56fdc35ca8be8f2aad1dd0eb2e1dd311d",
               "multi-line body is signed byte for byte");
  check_signed(vector_key_b64, NULL, NULL, moved, (unsigned)strlen(moved),
               "0a7461673d0a313735373333323830303132330a7461673d736e61700a74656d703d32312e34",
               "054bf69a4a1eb145077ab94da1e61c29fff03f98e8d79d4f2352157b9177a32c",
               "the nonce and tag lines moved into the body");

  /* Moving the nonce and tag lines into the body must change the signature. */
  {
    unsigned char a[CHIRP_SIGN_PREFIX_MAX + BODY_MAX], b[CHIRP_SIGN_PREFIX_MAX + BODY_MAX];
    char          ha[CHIRP_SIGN_HEX_LEN + 1], hb[CHIRP_SIGN_HEX_LEN + 1];
    int           pa = chirp_sign_prefix((char *)a, CHIRP_SIGN_PREFIX_MAX, nonce, "snap");
    int           pb = chirp_sign_prefix((char *)b, CHIRP_SIGN_PREFIX_MAX, NULL, NULL);

    if (pa < 0 || pb < 0) {
      printf("FAIL  substitution: chirp_sign_prefix refused a prefix it has room for\n");
      fails++;
    } else {
      unsigned na = (unsigned)pa + 9u, nb = (unsigned)pb + (unsigned)strlen(moved);
      memcpy(a + pa, body, 9u);
      memcpy(b + pb, moved, strlen(moved));
      if (chirp_sign_hex(ha, sizeof ha, vector_key_b64, sizeof vector_key_b64, a, na) != 0 ||
          chirp_sign_hex(hb, sizeof hb, vector_key_b64, sizeof vector_key_b64, b, nb) != 0) {
        printf("FAIL  substitution: chirp_sign_hex refused\n");
        fails++;
      } else if ((na == nb && memcmp(a, b, na) == 0) || strcmp(ha, hb) == 0) {
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
    char     longest_nonce[CHIRP_NONCE_MAX + 1u], longest_tag[CHIRP_TAG_MAX + 1u];
    unsigned i;

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

  puts("\n--- the demo's own L2 datagram (the README quotes this) ---");
  {
    /* README example key: bytes 0x00 through 0x1f. Never use it as a device
     * credential. Payload 00 d6 is 214 tenths = 21.4 C. */
    static const char demo_key[] = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=";
    static const char want_sig[] =
      "d5c7080b4958b335e0e45c159209f4c88bdeea2f3f7e99c5dfbe25c8c05e3322";
    unsigned char payload[CHIRP_FRAME_PAYLOAD_LEN];
    unsigned char input[CHIRP_SIGN_PREFIX_MAX + CHIRP_FRAME_PAYLOAD_LEN];
    char          sig[CHIRP_SIGN_HEX_LEN + 1], buf[192];
    unsigned      pn = chirp_frame_payload(payload, sizeof payload, 214u);
    unsigned      n = 0u;
    int           plen;

    check_signed(demo_key, NULL, NULL, payload, pn, "0a7461673d0a00d6", want_sig,
                 "signature over \"\\ntag=\\n\" + payload 00d6");

    /* The same steps chirp_report.c takes, through to the datagram. */
    plen = chirp_sign_prefix((char *)input, CHIRP_SIGN_PREFIX_MAX, 0, 0);
    if (plen >= 0) {
      memcpy(input + plen, payload, pn);
      if (chirp_sign_hex(sig, sizeof sig, demo_key, sizeof demo_key,
                         input, (unsigned)plen + pn) == 0) {
        n = chirp_frame_build(buf, sizeof buf, "hw-01", "tok9", sig, payload, pn);
      }
    }
    if (n == 85u &&
        memcmp(buf, "CHIRP1 hw-01 tok9:", 18u) == 0 &&
        memcmp(buf + 18, want_sig, 64u) == 0 &&
        memcmp(buf + 82, " \x00\xd6", 3u) == 0) {
      printf("ok    datagram: CHIRP1 hw-01 tok9:%s <00 d6>\n", want_sig);
    } else {
      printf("FAIL  datagram: L2 frame with the real signature (n=%u)\n", n);
      fails++;
    }
  }
}

/* js_num_int must refuse, not wrap, a number past LONG_MAX. */
static void check_num(const char *text, int want_rc, long want, const char *why)
{
  js_val v;
  long got = 0;
  int rc;

  v.t = JS_NUM; v.p = text; v.n = (unsigned)strlen(text);
  rc = js_num_int(&v, &got);
  if (rc != want_rc || (rc == 0 && got != want)) {
    printf("FAIL  js_num_int(%s) = %d, %ld; want %d, %ld  %s\n", text, rc, got, want_rc, want, why);
    fails++;
  } else {
    printf("ok    js_num_int(%s) %s\n", text, why);
  }
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
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"n\",\"key\":\"00112233445566778899aabbccddeeff\","
        "\"channel\":4294967307}}", "bad-json", "channel 2^32 + 11 does not wrap to 11");
}

int main(int argc, char **argv)
{
  const char *full =
    "{\"host\":\"my-core.example.com\",\"claim\":\"CHIRP-PLACEHOLDER\",\"hwid\":\"wba65-demo-3f9a12\","
    "\"thread\":{\"name\":\"chirp-lab\",\"key\":\"00112233445566778899aabbccddeeff\","
    "\"panid\":\"1234\",\"xpanid\":\"1111111122222222\",\"channel\":15}}";

  /* A 72-byte dataset with seven complete TLVs.
   * The ds_malformed fixture exercises an invalid Network Name length. */
  const char *dataset =
    "{\"claim\":\"CHIRP-PLACEHOLDER\",\"thread\":{\"dataset\":\"0e080000000000010000000300001835"
    "060004001fffe00208111111112222222207081111111122222222051000112233445566778899aabbccddeeff"
    "030963686972702d6c6162\"}}";

  test_vectors(argc > 1 ? argv[1] : NULL);
  test_rules();
  test_store();
  check(full, 0, "v1.2 thread, discrete fields");
  check(dataset, 0, "v1.2 thread, dataset form");

  puts("\n--- summary fields (what the boot line prints) ---");
  check_summary(full);

  test_num_limits();
  test_ds(full, dataset);
  test_frame();
  test_sign();

  printf("\nvaltest: %d passed, %d failed\n", passes, fails);
  printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
