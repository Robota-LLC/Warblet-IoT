#include "chirp_prov.h"
#include "json.h"
#include "chirpval.h"
#include "flashcfg.h"
#include "chirp_send.h"
#include "chirp_sign.h"
#include "uart.h"
#include "dfu.h"
#include "board.h"

/* fw=: the release in which this demo's firmware last changed. The Makefile
 * passes it (FW_VERSION), and demo.json's firmware.fw says the same. */
#ifndef CHIRP_FW
#error "build with the Makefile: it passes CHIRP_FW, this demo's FW_VERSION"
#endif

#define DEMO_SLUG "demo-nucleo-wba65"

/* A line whose bytes stop for this long is given up: longer than the 1 s a
 * browser tab out of view waits between the pieces of a push. */
#define LINE_IDLE_MS 1500u

/* Before "CHIRP" nothing counts, so only enough to find it is kept. */
#define JUNK_KEPT 64u

/* The announce at its longest, within CHIRP-PROV v1.8's 512 bytes. */
_Static_assert(sizeof("CHIRP! v=2 hw= fw= key_set=0 t=" DEMO_SLUG " caps=") - 1u + CHIRP_HWID_LEN_MAX +
               sizeof(CHIRP_FW) - 1u + sizeof(CHIRP_CAPS) - 1u <= 512u,
               "the announce must fit in 512 bytes");

/* The store holds the longest record a push can make. */
_Static_assert(CFG_MAX_PAYLOAD >= CHIRP_RECORD_MAX, "the store must hold the longest record");

/* ------------------------------------------------------------------ small str */

static int s_eq(const char *a, const char *b)
{
  while (*a && *a == *b) { a++; b++; }
  return *a == '\0' && *b == '\0';
}

static int s_starts(const char *s, const char *pfx)
{
  while (*pfx) { if (*s++ != *pfx++) { return 0; } }
  return 1;
}

/* ------------------------------------------------------------------ hardware id */

/* The pushed `hwid` if there is one, else the low 24 bits of UID word 0
 * (the per-die part of the unique id). */
unsigned chirp_hwid(char *out, unsigned outsz)
{
  static const char H[] = "0123456789abcdef";
  static const char pfx[] = "wba65-demo-";
  const char *json;
  unsigned    jl = 0u, n = 0u, uid0;
  js_val      v;

  if (outsz == 0u) { return 0u; }

  json = cfg_json(&jl);
  if (json && js_obj_get(json, jl, "hwid", &v) == 1 && v.t == JS_STR && v.n > 0u) {
    int copied = js_str_copy(&v, out, outsz);
    if (copied > 0) { return (unsigned)copied; }
  }

  uid0 = *(volatile const unsigned *)UID_BASE;
  while (pfx[n] != '\0' && n + 1u < outsz) { out[n] = pfx[n]; n++; }
  for (int i = 5; i >= 0 && n + 1u < outsz; i--) {
    out[n++] = H[(uid0 >> (i * 4)) & 0xFu];
  }
  out[n] = '\0';
  return n;
}

static void put_hwid(void)
{
  char id[CHIRP_HWID_SIZE];
  unsigned n = chirp_hwid(id, sizeof id);
  uart_write(id, n);
}

/* ------------------------------------------------------------------ config summary */

static void put_key_fp(const char *secret, unsigned n)
{
  char fp[9];

  chirp_fingerprint(secret, n, fp);
  uart_puts("sha256:");
  uart_puts(fp);
}

static void summarize(const char *json, unsigned len)
{
  js_val v, t;

  uart_puts("CHIRP. cfg");

  if (js_obj_get(json, len, "host", &v) == 1 && v.t == JS_STR) {
    uart_puts(" host=");
    uart_write(v.p, v.n);
  }
  if (js_obj_get(json, len, "ssid", &v) == 1 && v.t == JS_STR) {
    uart_puts(" ssid=");
    uart_write(v.p, v.n);
  }

  /* Report whether a signing key is stored without printing it. */
  if (js_obj_get(json, len, "key", &v) == 1 && v.t == JS_STR) {
    uart_puts(" key=stored");
  }

  if (js_obj_get(json, len, "thread", &t) == 1 && t.t == JS_OBJ) {
    if (js_obj_get(t.p, t.n, "dataset", &v) == 1 && v.t == JS_STR) {
      uart_puts(" thread=dataset(");
      uart_put_uint(v.n / 2u);
      uart_puts("B) fp=");
      put_key_fp(v.p, v.n);
    } else {
      if (js_obj_get(t.p, t.n, "name", &v) == 1 && v.t == JS_STR) {
        uart_puts(" thread=");
        uart_write(v.p, v.n);
      }
      if (js_obj_get(t.p, t.n, "key", &v) == 1 && v.t == JS_STR) {
        uart_puts(" key=");
        put_key_fp(v.p, v.n);
      }
      if (js_obj_get(t.p, t.n, "channel", &v) == 1 && v.t == JS_NUM) {
        long ch = 0;
        if (js_num_int(&v, &ch) == 0) { uart_puts(" ch="); uart_put_uint((unsigned)ch); }
      }
      if (js_obj_get(t.p, t.n, "panid", &v) == 1 && v.t == JS_STR) {
        uart_puts(" panid=");
        uart_write(v.p, v.n);
      }
    }
    uart_puts(" (not joined: no radio stack in this demo)");
  }
  uart_line("");
}

/* ------------------------------------------------------------------ line handling */

/* CHIRP-PROV v1.8: what this firmware is and can do. v=2: it reads a Thread
 * dataset. No radios=: it drives no radio, and reports through the computer. */
static void announce(void)
{
  uart_puts("CHIRP! v=2 hw=");
  put_hwid();
  uart_puts(" fw=");
  uart_puts(CHIRP_FW);
  uart_puts(chirp_send_key_set() ? " key_set=1" : " key_set=0");
  uart_puts(" t=" DEMO_SLUG);
  uart_puts(" caps=");
  uart_line(CHIRP_CAPS);
}

/* Merge scratch, sized like the store's record. */
static char s_merged[CFG_MAX_PAYLOAD + 1];

static void handle_config(const char *json, unsigned len)
{
  const char *stored;
  unsigned    slen = 0u, mlen = 0u;
  char        id[CHIRP_HWID_SIZE];
  char        reply[CHIRP_REPLY_MAX];

  /* Checked and merged over what is stored (chirpval.c), or refused whole. */
  stored = cfg_json(&slen);
  (void)chirp_hwid(id, sizeof id);      /* the id the board answers to now */
  if (!chirp_merge(stored, slen, json, len, id, s_merged, sizeof s_merged, &mlen, reply)) {
    uart_line(reply);
    return;
  }

  /* ok only once the new record is stored and read back. */
  if (cfg_store(s_merged, mlen) != 0) {
    uart_line("CHIRP= err store-failed");
    return;
  }
  uart_line("CHIRP= ok");
  summarize(s_merged, mlen);
  /* Re-read the credentials now (contract §Elevation): the next uplink is
   * signed with no reboot. */
  chirp_send_reload();
}

static void handle_line(char *line, unsigned len)
{
  /* Strip trailing spaces and tabs. */
  while (len && (line[len - 1] == ' ' || line[len - 1] == '\t')) {
    line[--len] = '\0';
  }

  if (s_eq(line, "CHIRP?")) { announce(); return; }

  if (s_starts(line, "CHIRP+")) {
    char *json = line + 6;
    unsigned jl = len - 6u;
    while (jl && (*json == ' ' || *json == '\t')) { json++; jl--; }
    handle_config(json, jl);
    return;
  }

  if (s_eq(line, "CHIRP~ dfu")) {
    uart_line("CHIRP~ ok dfu");
    uart_line("CHIRP. jumping to system bootloader at 0x0BF90000");
    dfu_enter_system_bootloader();
    return;                              /* unreachable */
  }

  uart_line("CHIRP= err unsupported");
}

/* ------------------------------------------------------------------ the line */

static char     s_line[CHIRP_LINE_MAX + 1];
static unsigned s_len;
static int      s_marked;    /* "CHIRP" has come: s_line starts there */
static int      s_skip;      /* the rest of a line that was too long: dropped to its end */
static unsigned s_last_ms;   /* when the last byte came */

/* One byte off the console. A line counts from its first "CHIRP": stray bytes
 * in front are dropped and do not spoil it. A line longer than CHIRP_LINE_MAX
 * from there is thrown away whole, through its line ending, so no part of it
 * is read as a command and it gets no answer. A line whose bytes stop for
 * LINE_IDLE_MS is given up. CR, LF and CRLF each end a line. */
static void feed(char c, unsigned now_ms)
{
  if (s_marked && !s_skip && (unsigned)(now_ms - s_last_ms) >= LINE_IDLE_MS) {
    s_len = 0u;                          /* the line stopped coming: it will not end now */
    s_marked = 0;
  }
  s_last_ms = now_ms;
  if (c == '\r' || c == '\n') {
    if (s_marked && !s_skip) {
      s_line[s_len] = '\0';
      handle_line(s_line, s_len);
    }
    s_len = 0u;
    s_marked = s_skip = 0;
    return;
  }
  if (s_skip) { return; }
  if (s_len >= CHIRP_LINE_MAX) {
    s_len = 0u;
    s_marked = 0;
    s_skip = 1;
    return;
  }
  if (!s_marked && s_len >= JUNK_KEPT) {
    for (unsigned i = 0u; i < 4u; i++) { s_line[i] = s_line[s_len - 4u + i]; }
    s_len = 4u;                          /* enough to find a "CHIRP" split across */
  }
  s_line[s_len++] = c;
  if (!s_marked && c == 'P' && s_len >= 5u && s_starts(s_line + s_len - 5u, "CHIRP")) {
    for (unsigned i = 0u; i < 5u; i++) { s_line[i] = "CHIRP"[i]; }
    s_len = 5u;                          /* the line starts here */
    s_marked = 1;
  }
}

/* ------------------------------------------------------------------ api */

void chirp_prov_init(void)
{
  unsigned len = 0;

  uart_line("");
  uart_puts("CHIRP. chirpwba-demo boot hw=");
  put_hwid();
  uart_puts(" cfgpage=0x");
  uart_put_hex(cfg_page_addr(), 8);
  uart_line("");

  int loaded = cfg_load();
  if (loaded == 0) {
    const char *json = cfg_json(&len);
    summarize(json, len);
  } else if (loaded == -2) {
    uart_line("CHIRP. cfg pages unreadable (flash ECC error); waiting for USB setup");
  } else {
    uart_line("CHIRP. cfg none");
  }
  uart_line("CHIRP. ready (send CHIRP? / CHIRP+ <json> / CHIRP~ dfu)");
}

void chirp_prov_poll(unsigned now_ms)
{
  int c;
  while ((c = uart_getc()) >= 0) { feed((char)c, now_ms); }
}
